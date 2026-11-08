// ============================================================================
// language_pack.cpp
// ============================================================================
#include "language_pack.hpp"
#include "../bank_format.hpp"
#include "../import/archives.hpp"
#include "../import/zlib_util.hpp"
#include "../text/sjis.hpp"
#include "../../engine/engine_core.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>

namespace fs = std::filesystem;

namespace ch::lang {

namespace {
constexpr std::string_view kSaved = "language.txt";
constexpr std::size_t kMaxFiles = 20000, kMaxNameBytes = 24;
constexpr std::uint64_t kMaxFileBytes = 64ull << 20, kMaxPackBytes = 512ull << 20;

fs::path langRoot() { return fs::path(legacy::vfs::dataRoot()) / "lang"; }

std::vector<std::uint8_t> readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

void appendUtf8(std::string& out, unsigned c) {
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xc0 | c >> 6); out += char(0x80 | (c & 0x3f)); }
    else { out += char(0xe0 | c >> 12); out += char(0x80 | (c >> 6 & 0x3f)); out += char(0x80 | (c & 0x3f)); }
}

// The value of "key" in a flat JSON object of strings; "" if it has none.
std::string jsonString(std::string_view json, std::string_view key) {
    const std::string quoted = "\"" + std::string(key) + "\"";
    std::size_t at = 0;
    while ((at = json.find(quoted, at)) != std::string_view::npos) {
        at += quoted.size();
        std::size_t i = at;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        if (i >= json.size() || json[i] != ':') continue;         // that was a value
        ++i;
        while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i]))) ++i;
        if (i >= json.size() || json[i] != '"') return {};
        std::string out;
        for (++i; i < json.size() && json[i] != '"'; ++i) {
            if (json[i] != '\\' || i + 1 >= json.size()) { out += json[i]; continue; }
            const char e = json[++i];
            if (e == 'n') out += '\n';
            else if (e == 't') out += '\t';
            else if (e == 'u' && i + 4 < json.size()) {
                unsigned c = 0;
                std::from_chars(json.data() + i + 1, json.data() + i + 5, c, 16);
                appendUtf8(out, c);
                i += 4;
            } else out += e;
        }
        return out;
    }
    return {};
}

bool validId(const std::string& id) {
    if (id.empty() || id.size() > 16 || id == "jp" || id == "tw" || id == "common") return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

// The manifest's fields, or why they will not do.
bool readManifest(std::string_view json, Pack& p, std::string& error) {
    error.clear();
    p = {jsonString(json, "id"), jsonString(json, "name"), jsonString(json, "base"),
         jsonString(json, "author"), jsonString(json, "version")};
    if (!validId(p.id)) error = "language.json: \"id\" must be 1 to 16 of a-z 0-9 - _ and not jp, tw or common";
    else if (p.name.empty() || p.name.size() > kMaxNameBytes) error = "language.json: \"name\" must be 1 to 24 bytes";
    else if (p.base != "jp") error = "language.json: \"base\" must be \"jp\"";
    return error.empty();
}

// Where an entry goes in the pack folder, "" if it is not part of a pack.
// Paths are lower case like the game's lookups. Nothing leaves the folder.
std::string packPath(std::string path, const std::string& top) {
    if (!path.starts_with(top)) return {};
    path.erase(0, top.size());
    if (path.empty() || path.back() == '/' || path.front() == '/' || path.find('\\') != std::string::npos) return {};
    for (std::size_t a = 0; a < path.size();) {
        const std::size_t b = std::min(path.find('/', a), path.size());
        const std::string_view part = std::string_view(path).substr(a, b - a);
        if (part.empty() || part == "." || part == "..") return {};
        a = b + 1;
    }
    for (char& c : path) c = char(std::tolower(static_cast<unsigned char>(c)));
    if (path == "language.json") return path;
    for (const char* dir : {"data/", "text/", "images/"})
        if (path.starts_with(dir)) return path;
    return {};
}

// The manifest entry: at the root, or in one top folder (a zipped folder).
const import::Entry* findManifest(const std::vector<import::Entry>& files, std::string& top) {
    const import::Entry* found = nullptr;
    for (const auto& e : files) {
        const auto slash = e.path.find('/');
        const std::string_view leaf = slash == std::string::npos ? std::string_view(e.path) : std::string_view(e.path).substr(slash + 1);
        std::string lower(leaf);
        for (char& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
        if (lower != "language.json") continue;
        if (slash == std::string::npos) { top.clear(); return &e; }
        if (!found) { found = &e; top = e.path.substr(0, slash + 1); }
    }
    return found;
}

std::unordered_map<std::string, std::string>& table() {
    static std::unordered_map<std::string, std::string> t;
    return t;
}

std::string unescape(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out += s[i]; continue; }
        const char e = s[++i];
        out += e == 'n' ? '\n' : e == 't' ? '\t' : e == 'r' ? '\r' : e;
    }
    return out;
}

// original<TAB>translation. # comments; a blank translation leaves the original.
bool isTextKey(std::string_view s) {
    return s.size() == 17 && s[0] == '@' && s.find_first_not_of("0123456789abcdef", 1) == std::string_view::npos;
}

void loadTextFile(const fs::path& p) {
    const auto bytes = readAll(p);
    std::string_view s(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (s.starts_with("\xef\xbb\xbf")) s.remove_prefix(3);
    while (!s.empty()) {
        const std::size_t end = std::min(s.find('\n'), s.size());
        std::string_view line = s.substr(0, end);
        s.remove_prefix(std::min(end + 1, s.size()));
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        const std::size_t tab = line.find('\t');
        if (line.empty() || line.front() == '#' || tab == std::string_view::npos || tab + 1 == line.size()) continue;
        const std::string key = unescape(line.substr(0, tab));
        table()[isTextKey(key) ? key : textKey(key)] = unescape(line.substr(tab + 1));
    }
}

// Bank files: any images/<that file>/<frame>.png are painted on as it is read.
constexpr std::string_view kOpeningSheets = "opdata.bin";

void paintImages(const fs::path& pack, const std::string& rel, std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    const fs::path dir = pack / "images" / rel;
    if (!fs::is_directory(dir, ec)) return;
    std::vector<FramePaint> paints;
    for (const auto& f : fs::directory_iterator(dir, ec)) {
        if (f.path().extension() != ".png") continue;
        std::string stem = f.path().stem().string();
        PaintKind kind = PaintKind::Over;
        for (const auto& [suffix, k] : {std::pair{std::string_view(".erase"), PaintKind::Erase}, {".diff", PaintKind::Diff}})
            if (stem.ends_with(suffix)) {
                kind = k;
                stem.resize(stem.size() - suffix.size());
            }
        int frame = -1;
        const auto r = std::from_chars(stem.data(), stem.data() + stem.size(), frame);
        if (r.ptr != stem.data() + stem.size() || frame < 0) continue;
        const auto png = readAll(f.path());
        Image image = decodePng(png);
        if (image.empty()) SDL_Log("[lang] %s frame %d: not an 8-bit RGB or RGBA png", rel.c_str(), frame);
        else paints.push_back({frame, kind, std::move(image)});
    }
    // A frame's erasing, then its diff, then what is painted on it; new frames in order.
    std::sort(paints.begin(), paints.end(),
              [](const FramePaint& a, const FramePaint& b) { return std::tie(a.kind, a.frame) < std::tie(b.kind, b.frame); });
    std::string error;
    if (paints.empty()) return;
    if (!paintFrames(bytes, paints, error)) SDL_Log("[lang] %s: %s", rel.c_str(), error.c_str());
    // Painting takes long enough to stop the game and its sound, so it's done once.
    else if (!rel.ends_with(kOpeningSheets)) legacy::vfs::cacheAsset(rel, bytes);
}
} // namespace

std::vector<Pack> packs() {
    std::vector<Pack> out;
    std::error_code ec;
    for (const auto& d : fs::directory_iterator(langRoot(), ec)) {
        if (!d.is_directory(ec) || d.path().filename().string().starts_with(".")) continue;
        const auto bytes = readAll(d.path() / "language.json");
        Pack p;
        std::string error;
        if (readManifest(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()), p, error) &&
            p.id == d.path().filename().string())
            out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const Pack& a, const Pack& b) { return a.name < b.name; });
    return out;
}

bool isPackFile(const std::string& path) {
    try {
        const import::FileSource src(path);
        if (!import::isZip(src)) return false;
        std::string top;
        return findManifest(import::zipFiles(src), top) != nullptr;
    } catch (const std::exception&) {
        return false;
    }
}

bool installPack(const std::string& path, Pack& out, std::string& error) {
    std::error_code ec;
    const fs::path temp = langRoot() / ".installing";
    fs::remove_all(temp, ec);
    try {
        const import::FileSource src(path);
        const auto files = import::zipFiles(src);
        std::string top;
        const import::Entry* manifest = findManifest(files, top);
        if (!manifest) { error = "no language.json"; return false; }
        if (manifest->size > 0x10000) { error = "language.json is too big"; return false; }
        const auto json = import::zipExtract(src, *manifest);
        if (!readManifest(std::string_view(reinterpret_cast<const char*>(json.data()), json.size()), out, error)) return false;
        if (files.size() > kMaxFiles) { error = "too many files"; return false; }
        std::uint64_t total = 0;
        for (const auto& e : files) {
            const std::string rel = packPath(e.path, top);
            if (rel.empty()) continue;                            // folders, a README, anything else
            total += e.size;
            if (e.size > kMaxFileBytes || total > kMaxPackBytes) { error = e.path + ": too big"; fs::remove_all(temp, ec); return false; }
            const fs::path to = temp / rel;
            fs::create_directories(to.parent_path(), ec);
            const auto bytes = import::zipExtract(src, e);
            std::ofstream f(to, std::ios::binary);
            f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
            if (!f) { error = "cannot write " + rel; fs::remove_all(temp, ec); return false; }
        }
    } catch (const std::exception& ex) {
        error = ex.what();
        fs::remove_all(temp, ec);
        return false;
    }
    const fs::path dest = langRoot() / out.id;
    fs::remove_all(dest, ec);
    fs::rename(temp, dest, ec);
    if (ec) { error = ec.message(); fs::remove_all(temp, ec); return false; }
    return true;
}

bool removePack(const std::string& id) {
    if (!validId(id)) return false;
    std::error_code ec;
    return fs::remove_all(langRoot() / id, ec) > 0 && !ec;
}

std::vector<Choice> choices(const import::Installed& have) {
    std::vector<Choice> out;
    if (have.jp) out.push_back({"jp", "日本語"});
    if (have.tw) out.push_back({"tw", "繁體中文"});
    if (have.jp)
        for (const Pack& p : packs()) out.push_back({p.id, p.name});
    return out;
}

std::string preferred(const import::Installed& have) {
    const auto saved = legacy::vfs::readSave(kSaved);
    std::string s = saved ? std::string(saved->begin(), saved->end()) : std::string();
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    const auto all = choices(have);
    if (std::any_of(all.begin(), all.end(), [&](const Choice& c) { return c.id == s; })) return s;
    return have.jp || !have.tw ? "jp" : "tw";
}

void savePreferred(const std::string& id) {
    const std::string s = id + "\n";
    legacy::vfs::writeSave(kSaved, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
}

GameVersion versionOf(const std::string& id) { return id == "tw" ? GameVersion::Tw : GameVersion::Jp; }

namespace {
using LayoutKey = std::array<int, 5>;
std::map<std::string, std::map<LayoutKey, std::vector<SpritePart>>>& layouts() {
    static std::map<std::string, std::map<LayoutKey, std::vector<SpritePart>>> l;
    return l;
}

void loadLayout(const fs::path& file, const std::string& bank) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        LayoutKey key{};
        SpritePart part;
        if (row >> key[0] >> key[1] >> key[2] >> key[3] >> key[4]
                >> part.frame >> part.x0 >> part.y0 >> part.x1 >> part.y1 >> part.dx >> part.dy)
            layouts()[bank][key].push_back(part);
        else
            SDL_Log("[lang] %s: bad layout line: %s", file.string().c_str(), line.c_str());
    }
}
} // namespace

const std::vector<SpritePart>* spriteParts(const std::string& bank, int frame, int x0, int y0, int x1, int y1) {
    const auto b = layouts().find(bank);
    if (b == layouts().end()) return nullptr;
    const auto it = b->second.find({frame, x0, y0, x1, y1});
    return it != b->second.end() ? &it->second : nullptr;
}

void activate(const std::string& id) {
    table().clear();
    layouts().clear();
    if (id.empty() || !validId(id)) {
        legacy::vfs::setOverlay({});
        legacy::vfs::setAssetFilter({});
        setTextTable(nullptr);
        return;
    }
    const fs::path pack = langRoot() / id;
    std::error_code ec;
    for (const auto& f : fs::directory_iterator(pack / "text", ec))
        if (f.path().extension() == ".tsv") loadTextFile(f.path());
    legacy::vfs::setOverlay("lang/" + id);
    legacy::vfs::setAssetFilter([pack](const std::string& rel, std::vector<std::uint8_t>& bytes) { paintImages(pack, rel, bytes); });
    setTextTable(&table());
    // Paint now, while the game is starting, not when a screen first needs the bank.
    std::set<fs::path> banks;
    for (const auto& f : fs::recursive_directory_iterator(pack / "images", ec)) {
        if (f.path().filename() == "layout.txt")
            loadLayout(f.path(), f.path().parent_path().lexically_relative(pack / "images").generic_string());
        if (f.path().extension() == ".png") banks.insert(f.path().parent_path());
    }
    for (const fs::path& bank : banks) {
        // The opening's 96 MB of sheets are read once, behind its loading text.
        if (bank.filename() == kOpeningSheets) continue;
        try { legacy::vfs::readAsset(bank.lexically_relative(pack / "images").generic_string()); }
        catch (const std::exception&) {}   // not imported yet: painted when first read
    }
    SDL_Log("[lang] %s: %zu strings", id.c_str(), table().size());
}

// ------------------------------------------------------------------ images ---
Image decodePng(std::span<const std::uint8_t> p) {
    static constexpr std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (p.size() < 33 || std::memcmp(p.data(), kSig, 8) != 0) return {};
    const auto be32 = [&](std::size_t o) {
        return std::uint32_t(p[o]) << 24 | std::uint32_t(p[o + 1]) << 16 | std::uint32_t(p[o + 2]) << 8 | p[o + 3];
    };
    std::uint32_t w = 0, h = 0;
    int channels = 0;
    std::vector<std::uint8_t> packed;
    for (std::size_t at = 8; at + 12 <= p.size();) {
        const std::uint32_t len = be32(at);
        if (len > p.size() - at - 12) return {};
        const char* type = reinterpret_cast<const char*>(p.data() + at + 4);
        const std::size_t data = at + 8;
        if (std::memcmp(type, "IHDR", 4) == 0) {
            if (len != 13) return {};
            w = be32(data);
            h = be32(data + 4);
            channels = p[data + 9] == 6 ? 4 : p[data + 9] == 2 ? 3 : 0;
            if (p[data + 8] != 8 || channels == 0 || p[data + 12] != 0 || w == 0 || h == 0 || w > 4096 || h > 4096) return {};
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            packed.insert(packed.end(), p.begin() + std::ptrdiff_t(data), p.begin() + std::ptrdiff_t(data + len));
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        at = data + len + 4;
    }
    if (channels == 0 || packed.empty()) return {};
    const std::size_t stride = std::size_t(w) * std::size_t(channels);
    std::vector<std::uint8_t> raw;
    try {
        raw = import::inflateZlib(packed, (stride + 1) * h);
    } catch (const std::exception&) {
        return {};
    }
    if (raw.size() < (stride + 1) * h) return {};

    Image img{int(w), int(h), std::vector<std::uint32_t>(std::size_t(w) * h)};
    std::vector<std::uint8_t> prev(stride, 0), cur(stride);
    for (std::uint32_t y = 0; y < h; ++y) {
        const std::uint8_t* row = raw.data() + std::size_t(y) * (stride + 1);
        const std::uint8_t filter = row[0];
        if (filter > 4) return {};
        for (std::size_t i = 0; i < stride; ++i) {
            const int a = i >= std::size_t(channels) ? cur[i - std::size_t(channels)] : 0;
            const int b = prev[i];
            const int c = i >= std::size_t(channels) ? prev[i - std::size_t(channels)] : 0;
            int predict = 0;
            switch (filter) {
                case 1: predict = a; break;
                case 2: predict = b; break;
                case 3: predict = (a + b) / 2; break;
                case 4: {
                    const int pa = std::abs(b - c), pb = std::abs(a - c), pc = std::abs(a + b - 2 * c);
                    predict = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                    break;
                }
                default: break;
            }
            cur[i] = std::uint8_t(row[1 + i] + predict);
        }
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::uint8_t* t = cur.data() + std::size_t(x) * std::size_t(channels);
            const std::uint32_t alpha = channels == 4 ? t[3] : 0xff;
            img.argb[std::size_t(y) * w + x] = alpha << 24 | std::uint32_t(t[0]) << 16 | std::uint32_t(t[1]) << 8 | t[2];
        }
        prev.swap(cur);
    }
    return img;
}

bool paintFrames(std::vector<std::uint8_t>& bank, const std::vector<FramePaint>& paints, std::string& error) {
    const auto entries = splitBank(bank);
    std::map<int, Image> painted;
    std::size_t count = entries.size();
    for (const FramePaint& p : paints) {
        // The frame just past the bank's last is a new one, for a pack's own sheet.
        if (p.kind == PaintKind::Over && std::size_t(p.frame) == count) {
            painted.emplace(p.frame, p.image);
            ++count;
            continue;
        }
        if (p.frame < 0 || std::size_t(p.frame) >= count) {
            error = "the bank has no frame " + std::to_string(p.frame);
            return false;
        }
        auto it = painted.find(p.frame);
        if (it == painted.end()) {
            Image decoded = decodeBankEntry(entries[std::size_t(p.frame)], &error);
            if (decoded.empty()) return false;
            it = painted.emplace(p.frame, std::move(decoded)).first;
        }
        Image& img = it->second;
        const Image& over = p.image;
        if (img.w != over.w || img.h != over.h) {
            error = "frame " + std::to_string(p.frame) + "'s png must be " + std::to_string(img.w) + "x" + std::to_string(img.h);
            return false;
        }
        for (std::size_t i = 0; i < img.argb.size(); ++i) {
            const std::uint32_t s = over.argb[i], d = img.argb[i];
            const std::uint32_t sa = s >> 24, da = d >> 24;
            if (sa == 0) continue;
            if (p.kind == PaintKind::Erase) { img.argb[i] = 0; continue; }
            if (p.kind == PaintKind::Diff) {
                std::uint32_t out = 0xff000000u;
                for (int shift : {16, 8, 0}) out |= (((d >> shift) + (s >> shift)) & 0xff) << shift;
                img.argb[i] = out;
                continue;
            }
            const std::uint32_t oa = sa + da * (255 - sa) / 255;
            std::uint32_t out = oa << 24;
            for (int shift : {16, 8, 0}) {
                const std::uint32_t sc = s >> shift & 0xff, dc = d >> shift & 0xff;
                out |= ((sc * sa + dc * da * (255 - sa) / 255) / oa) << shift;
            }
            img.argb[i] = out;
        }
    }

    std::vector<std::uint8_t> rebuilt(8 + count * 4);
    const auto put32 = [&](std::size_t at, std::uint32_t v) {
        for (int k = 0; k < 4; ++k) rebuilt[at + std::size_t(k)] = std::uint8_t(v >> (k * 8));
    };
    for (std::size_t i = 0; i < count; ++i) {
        put32(8 + i * 4, std::uint32_t(rebuilt.size()));
        if (const auto it = painted.find(int(i)); it != painted.end()) {
            const std::vector<std::uint8_t> entry = encodeRgbaEntry(it->second);
            rebuilt.insert(rebuilt.end(), entry.begin(), entry.end());
        } else {
            rebuilt.insert(rebuilt.end(), entries[i].begin(), entries[i].end());
        }
    }
    put32(0, std::uint32_t(rebuilt.size()));
    put32(4, std::uint32_t(count));
    bank = std::move(rebuilt);
    return true;
}

} // namespace ch::lang
