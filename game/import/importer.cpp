// ============================================================================
// importer.cpp
// ============================================================================
#include "importer.hpp"
#include "ch_patch.hpp"
#include "zlib_util.hpp"
#include "../../engine/engine_core.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>

namespace ch::import {

namespace fs = std::filesystem;

namespace {
constexpr const char* kJpExe = "static/jp.exe";
constexpr const char* kTwExe = "static/tw.exe";
constexpr const char* kChData = "static/ch_data.bin";
constexpr const char* kUpdatePrefix = "cross hermit update/update/";

std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}

fs::path root() { return fs::path(legacy::vfs::dataRoot()); }
fs::path commonPath(const std::string& rel) { return root() / "common" / rel; }

std::vector<std::uint8_t> readAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + p.string());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), {});
}

void writeAll(const fs::path& p, std::span<const std::uint8_t> data) {
    fs::create_directories(p.parent_path());
    const fs::path tmp = p.string() + ".part";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) throw std::runtime_error("cannot write " + p.string());
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) throw std::runtime_error("cannot write " + p.string() + " (disk full?)");
    }
    fs::rename(tmp, p);
}

bool sameFile(const fs::path& p, std::span<const std::uint8_t> data) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) != data.size()) return false;
    const auto have = readAll(p);
    return std::equal(have.begin(), have.end(), data.begin(), data.end());
}

void moveFile(const fs::path& from, const fs::path& to) {
    fs::create_directories(to.parent_path());
    std::error_code ec;
    fs::remove(to, ec);
    fs::rename(from, to);
}

std::set<std::string> markers() {
    std::set<std::string> out;
    std::ifstream f(root() / "installed.txt");
    for (std::string line; std::getline(f, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) out.insert(line);
    }
    return out;
}

void setMarker(const std::string& v, bool on) {
    auto m = markers();
    if (on) m.insert(v); else m.erase(v);
    fs::create_directories(root());
    std::ofstream f(root() / "installed.txt", std::ios::trunc);
    for (const auto& x : m) f << x << '\n';
}

// A file of a release: in common/ when the other release has the same bytes.
void writeVariant(const std::string& variant, const std::string& rel, std::span<const std::uint8_t> data) {
    const std::string other = variant == "jp" ? "tw" : "jp";
    const fs::path mine = root() / variant / rel, theirs = root() / other / rel, shared = commonPath(rel);
    std::error_code ec;
    if (fs::is_regular_file(shared, ec)) {
        if (sameFile(shared, data)) {
            fs::remove(mine, ec);
            return;
        }
        moveFile(shared, theirs);              // it was the other release's
    } else if (sameFile(theirs, data)) {
        moveFile(theirs, shared);
        fs::remove(mine, ec);
        return;
    }
    writeAll(mine, data);
}

const chpatch::Patch& patch() {
    static const chpatch::Patch p = chpatch::parse(legacy::vfs::readResource("ch_data.chpatch"));
    return p;
}

// "data/..." for the release files the game reads, or "".
std::string dataPath(const std::string& archivePath) {
    const std::string p = lower(archivePath);
    return p.starts_with("data/") ? p : std::string();
}
} // namespace

const char* kindName(Kind k) {
    switch (k) {
    case Kind::JpDisc: return "Japanese disc";
    case Kind::TwDisc: return "Traditional Chinese disc";
    case Kind::JpUpdate: return "Japanese 1.2 update";
    case Kind::JpExe: return "Japanese 1.2 Cross Hermit.exe";
    case Kind::TwExe: return "Traditional Chinese CROSS HERMIT.EXE";
    case Kind::Unknown: break;
    }
    return "not recognised";
}

Input identify(const std::string& path) {
    Input in{path};
    try {
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            in.note = "a folder: choose the disc image (.iso), or data.z / cd1 from the disc";
            return in;
        }
        auto s = std::make_shared<FileSource>(path);
        if (isIso(*s)) {
            for (const auto& e : isoFiles(*s)) {
                const std::string n = lower(e.path);
                if (n == "data.z" && isIs3(SliceSource(s, e.offset, e.size))) { in.kind = Kind::JpDisc; break; }
                if (n == "cd1" && isTTime(SliceSource(s, e.offset, e.size))) { in.kind = Kind::TwDisc; break; }
            }
            in.note = in.kind == Kind::Unknown ? "a disc image, but not a Cross Hermit disc" : "disc image";
            return in;
        }
        if (isIs3(*s)) { in.kind = Kind::JpDisc; in.note = "data.z"; return in; }
        if (isTTime(*s)) { in.kind = Kind::TwDisc; in.note = "cd1"; return in; }
        std::uint8_t mz[2] = {};
        if (s->size() >= 2) s->read(0, mz, 2);
        if (mz[0] == 'M' && mz[1] == 'Z') {
            if (isZip(*s))
                for (const auto& e : zipFiles(*s))
                    if (lower(e.path) == std::string(kUpdatePrefix) + "cross hermit.exe") {
                        in.kind = Kind::JpUpdate;
                        in.note = "update";
                        return in;
                    }
            if (s->size() < (64u << 20)) {
                const auto bytes = s->bytes(0, std::size_t(s->size()));
                if (chpatch::isSource(patch(), 0, bytes)) { in.kind = Kind::JpExe; in.note = "exe"; return in; }
                if (chpatch::isSource(patch(), 1, bytes)) { in.kind = Kind::TwExe; in.note = "exe"; return in; }
            }
            in.note = "a program, but not the 1.2 update or a release's Cross Hermit.exe";
            return in;
        }
        in.note = "not a Cross Hermit file";
    } catch (const std::exception& ex) {
        in.note = ex.what();
    }
    return in;
}

Installed installed() {
    Installed i;
    std::error_code ec;
    const auto m = markers();
    i.jpExe = fs::is_regular_file(commonPath(kJpExe), ec);
    const bool data = fs::is_regular_file(commonPath(kChData), ec);
    i.jp = i.jpExe && data && m.count("jp");
    i.tw = i.jpExe && data && m.count("tw") && fs::is_regular_file(commonPath(kTwExe), ec);
    return i;
}

// --------------------------------------------------------------- Importer ---
struct Importer::State {
    std::vector<std::shared_ptr<const Source>> keep;
    std::optional<TTimeReader> tw;
    std::shared_ptr<const Source> twSource;
};

Importer::Importer() : st_(std::make_unique<State>()) {}
Importer::~Importer() = default;

const Input& Importer::add(const std::string& path) {
    Input in = identify(path);
    if (in.kind != Kind::Unknown)
        std::erase_if(inputs_, [&](const Input& x) { return x.kind == in.kind; });
    inputs_.push_back(std::move(in));
    return inputs_.back();
}

bool Importer::ready(std::string* missing) const {
    auto has = [&](Kind k) { return std::any_of(inputs_.begin(), inputs_.end(), [k](const Input& i) { return i.kind == k; }); };
    const Installed have = installed();
    const bool jpExe = have.jpExe || has(Kind::JpUpdate) || has(Kind::JpExe);
    // An exe alone refreshes an installed release's static data.
    const bool refresh = (have.jp || have.tw) && (has(Kind::JpUpdate) || has(Kind::JpExe) || has(Kind::TwExe));
    std::string why;
    if (!has(Kind::JpDisc) && !has(Kind::TwDisc) && !refresh)
        why = "a disc image (the Japanese or the Traditional Chinese release)";
    else if (has(Kind::JpDisc) && !has(Kind::JpUpdate))
        why = "the 1.2 update (Cross Hermit Update 1.2.exe) for the Japanese disc";
    else if (!jpExe)
        why = "the Japanese 1.2 update (Cross Hermit Update 1.2.exe): its exe is needed for either release";
    if (missing) *missing = why;
    return why.empty();
}

void Importer::plan() {
    jobs_.clear();
    auto find = [&](Kind k) -> const Input* {
        for (const auto& i : inputs_) if (i.kind == k) return &i;
        return nullptr;
    };
    // A source opened once; a disc image's archive is a slice of it.
    auto open = [&](const Input& in, const char* inner) -> std::shared_ptr<const Source> {
        auto f = std::make_shared<FileSource>(in.path);
        st_->keep.push_back(f);
        if (!isIso(*f)) return f;
        for (const auto& e : isoFiles(*f))
            if (lower(e.path) == inner) {
                auto s = std::make_shared<SliceSource>(f, e.offset, e.size);
                st_->keep.push_back(s);
                return s;
            }
        throw std::runtime_error(std::string("no ") + inner + " on the disc");
    };

    std::vector<Entry> updateData;
    std::shared_ptr<const Source> update;
    if (const Input* u = find(Kind::JpUpdate)) {
        update = open(*u, "");
        for (const auto& e : zipFiles(*update)) {
            const std::string p = lower(e.path);
            if (!p.starts_with(kUpdatePrefix)) continue;
            const std::string rel = p.substr(std::strlen(kUpdatePrefix));
            if (rel == "cross hermit.exe") {
                jobs_.push_back({"Japanese 1.2 exe", e.packed + 1, [this, update, e] {
                    const auto exe = zipExtract(*update, e);
                    if (!chpatch::isSource(patch(), 0, exe))
                        throw std::runtime_error("the update's Cross Hermit.exe is not the expected 1.2 version");
                    writeAll(commonPath(kJpExe), exe);
                    return true;
                }});
            } else if (!dataPath(rel).empty()) {
                Entry x = e;
                x.path = dataPath(rel);
                updateData.push_back(x);
            }
        }
    } else if (const Input* x = find(Kind::JpExe)) {
        jobs_.push_back({"Japanese 1.2 exe", 1, [p = x->path] { writeAll(commonPath(kJpExe), readAll(p)); return true; }});
    }
    if (const Input* x = find(Kind::TwExe))
        jobs_.push_back({"Traditional Chinese exe", 1, [p = x->path] { writeAll(commonPath(kTwExe), readAll(p)); return true; }});

    if (const Input* jp = find(Kind::JpDisc)) {
        auto z = open(*jp, "data.z");
        jobs_.push_back({"Japanese disc", 1, [] { setMarker("jp", false); return true; }});
        for (const auto& e : is3Files(*z)) {
            const std::string rel = dataPath(e.path);
            if (rel.empty()) continue;
            jobs_.push_back({"Japanese disc: " + rel, e.packed + 1, [z, e, rel] {
                writeVariant("jp", rel, is3Extract(*z, e));
                return true;
            }});
        }
        for (const auto& e : updateData)
            jobs_.push_back({"Japanese 1.2 update: " + e.path, e.packed + 1, [update, e] {
                writeVariant("jp", e.path, zipExtract(*update, e));
                return true;
            }});
        jobs_.push_back({"Japanese disc", 1, [] { setMarker("jp", true); return true; }});
    }

    if (const Input* tw = find(Kind::TwDisc)) {
        st_->twSource = open(*tw, "cd1");
        jobs_.push_back({"Traditional Chinese disc", 1, [] { setMarker("tw", false); return true; }});
        jobs_.push_back({"Traditional Chinese disc", st_->twSource->size(), [this] {
            if (!st_->tw) st_->tw.emplace(*st_->twSource);
            std::string path;
            std::vector<std::uint8_t> data;
            const auto want = [](const std::string& p) {
                const std::string l = lower(p);
                return l == "cross hermit.exe" || !dataPath(l).empty();
            };
            if (!st_->tw->next(path, data, want)) return true;
            partial_ = double(st_->tw->position()) / double(std::max<std::uint64_t>(st_->twSource->size(), 1));
            status_ = "Traditional Chinese disc: " + lower(path);
            const std::string l = lower(path);
            if (l == "cross hermit.exe") {
                if (!chpatch::isSource(patch(), 1, data))
                    throw std::runtime_error("the disc's CROSS HERMIT.EXE is not the expected version");
                writeAll(commonPath(kTwExe), data);
            } else if (!dataPath(l).empty()) {
                writeVariant("tw", l, data);
            }
            return false;
        }});
        jobs_.push_back({"Traditional Chinese disc", 1, [] { setMarker("tw", true); return true; }});
    }

    // Last: the TW build's data, from the exes now in place.
    jobs_.push_back({"Static data", 0x100000, [] {
        const auto jp = readAll(commonPath(kJpExe));
        std::error_code ec;
        std::vector<std::uint8_t> tw;
        if (fs::is_regular_file(commonPath(kTwExe), ec)) tw = readAll(commonPath(kTwExe));
        writeAll(commonPath(kChData), chpatch::apply(patch(), jp, tw));
        return true;
    }});
    totalWeight_ = 0;
    for (const auto& j : jobs_) totalWeight_ += j.weight;
}

void Importer::start() {
    error_.clear();
    finished_ = false;
    std::string why;
    if (!ready(&why)) {
        error_ = "Still needed: " + why;
        return;
    }
    try {
        plan();
    } catch (const std::exception& ex) {
        error_ = ex.what();
        return;
    }
    next_ = 0;
    doneWeight_ = 0;
    partial_ = 0;
    running_ = true;
}

bool Importer::step(double budgetMs) {
    if (!running_) return false;
    const auto t0 = std::chrono::steady_clock::now();
    try {
        while (next_ < jobs_.size()) {
            Job& j = jobs_[next_];
            if (status_.empty() || j.weight > 1) status_ = j.label;
            if (j.run()) {
                doneWeight_ += j.weight;
                partial_ = 0;
                ++next_;
            }
            if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() >= budgetMs)
                return true;
        }
    } catch (const std::exception& ex) {
        error_ = std::string(ex.what()) + " (" + (next_ < jobs_.size() ? jobs_[next_].label : std::string()) + ")";
        running_ = false;
        return false;
    }
    running_ = false;
    finished_ = true;
    status_ = "Done";
    jobs_.clear();
    st_ = std::make_unique<State>();
    return false;
}

double Importer::progress() const {
    if (finished_) return 1.0;
    if (totalWeight_ == 0) return 0.0;
    double done = double(doneWeight_);
    if (next_ < jobs_.size()) done += partial_ * double(jobs_[next_].weight);
    return std::min(1.0, done / double(totalWeight_));
}

int importCli(const std::vector<std::string>& paths) {
    Importer imp;
    for (const auto& p : paths) {
        const Input& in = imp.add(p);
        std::printf("%s: %s (%s)\n", p.c_str(), kindName(in.kind), in.note.c_str());
    }
    std::printf("data folder: %s\n", legacy::vfs::dataRoot().c_str());
    imp.start();
    if (!imp.error().empty()) {
        std::fprintf(stderr, "%s\n", imp.error().c_str());
        return 1;
    }
    int shown = -1;
    while (imp.step(200)) {
        const int pct = int(imp.progress() * 100);
        if (pct != shown) {
            shown = pct;
            std::printf("\r%3d%%  %-70.70s", pct, imp.status().c_str());
            std::fflush(stdout);
        }
    }
    std::printf("\n");
    if (!imp.error().empty()) {
        std::fprintf(stderr, "import failed: %s\n", imp.error().c_str());
        return 1;
    }
    const Installed i = installed();
    std::printf("installed:%s%s\n", i.jp ? " Japanese" : "", i.tw ? " Traditional Chinese" : "");
    return 0;
}

} // namespace ch::import
