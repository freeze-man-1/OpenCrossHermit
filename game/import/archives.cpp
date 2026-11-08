// ============================================================================
// archives.cpp
// ============================================================================
#include "archives.hpp"
#include "zlib_util.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace ch::import {

namespace {
std::uint16_t le16(const std::uint8_t* p) { return std::uint16_t(p[0] | p[1] << 8); }
std::uint32_t le32(const std::uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | std::uint32_t(p[3]) << 24; }
std::uint32_t le32(const Source& s, std::uint64_t at) {
    std::uint8_t b[4];
    s.read(at, b, 4);
    return le32(b);
}
} // namespace

// --------------------------------------------------------------- sources ---
FileSource::FileSource(const std::string& path) {
    f_ = std::fopen(path.c_str(), "rb");
    if (!f_) throw std::runtime_error("cannot open " + path);
#ifdef _WIN32
    _fseeki64(f_, 0, SEEK_END);
    size_ = std::uint64_t(_ftelli64(f_));
#else
    fseeko(f_, 0, SEEK_END);
    size_ = std::uint64_t(ftello(f_));
#endif
}

FileSource::~FileSource() { if (f_) std::fclose(f_); }

void FileSource::read(std::uint64_t offset, void* dst, std::size_t n) const {
    if (offset > size_ || n > size_ - offset) throw std::runtime_error("read past the end of the file");
#ifdef _WIN32
    _fseeki64(f_, std::int64_t(offset), SEEK_SET);
#else
    fseeko(f_, off_t(offset), SEEK_SET);
#endif
    if (n && std::fread(dst, 1, n, f_) != n) throw std::runtime_error("read error");
}

void SliceSource::read(std::uint64_t offset, void* dst, std::size_t n) const {
    if (offset > size_ || n > size_ - offset) throw std::runtime_error("read past the end of the archive");
    base_->read(offset_ + offset, dst, n);
}

// -------------------------------------------------------------- ISO 9660 ---
bool isIso(const Source& s) {
    if (s.size() < 0x8800) return false;
    std::uint8_t b[6];
    s.read(0x8000, b, 6);
    return b[0] == 1 && std::memcmp(b + 1, "CD001", 5) == 0;
}

std::vector<Entry> isoFiles(const Source& s) {
    constexpr std::uint64_t kSector = 2048;
    const auto pvd = s.bytes(0x8000, kSector);
    std::vector<Entry> out;
    struct Dir { std::string path; std::uint32_t lba, size; int depth; };
    std::vector<Dir> todo{{"", le32(&pvd[156 + 2]), le32(&pvd[156 + 10]), 0}};
    while (!todo.empty()) {
        const Dir d = todo.back();
        todo.pop_back();
        if (d.depth > 16 || d.size > (16u << 20)) continue;
        const auto data = s.bytes(std::uint64_t(d.lba) * kSector, d.size);
        for (std::size_t at = 0; at < data.size();) {
            const std::uint8_t len = data[at];
            if (len == 0) {                       // records do not cross sectors
                at = (at / kSector + 1) * kSector;
                continue;
            }
            if (at + 33 > data.size() || at + len > data.size()) break;
            const std::uint8_t* r = &data[at];
            const std::uint8_t nameLen = r[32];
            std::string name(reinterpret_cast<const char*>(r + 33), std::min<std::size_t>(nameLen, len - 33u));
            at += len;
            if (name.size() == 1 && (name[0] == 0 || name[0] == 1)) continue;   // . and ..
            if (const auto semi = name.find(';'); semi != std::string::npos) name.resize(semi);
            if (!name.empty() && name.back() == '.') name.pop_back();
            const std::string path = d.path.empty() ? name : d.path + "/" + name;
            if (r[25] & 2) todo.push_back({path, le32(r + 2), le32(r + 10), d.depth + 1});
            else out.push_back({path, std::uint64_t(le32(r + 2)) * kSector, le32(r + 10), le32(r + 10)});
        }
    }
    return out;
}

// ------------------------------------------------------- InstallShield 3 ---
namespace {
constexpr std::uint32_t kIs3Signature = 0x8c655d13;
constexpr std::uint64_t kIs3Data = 255;
} // namespace

bool isIs3(const Source& s) { return s.size() > kIs3Data && le32(s, 0) == kIs3Signature; }

std::vector<Entry> is3Files(const Source& s) {
    const auto head = s.bytes(0, 51);
    const std::uint32_t toc = le32(&head[41]);
    const unsigned dirCount = le16(&head[49]);
    if (toc >= s.size()) throw std::runtime_error("data.z: bad table of contents");
    const auto t = s.bytes(toc, std::size_t(s.size() - toc));
    std::size_t at = 0;
    auto need = [&](std::size_t n) { if (n > t.size() - at) throw std::runtime_error("data.z: truncated table of contents"); };
    std::vector<std::pair<std::string, unsigned>> dirs;
    for (unsigned i = 0; i < dirCount; ++i) {
        need(6);
        const unsigned files = le16(&t[at]), chunk = le16(&t[at + 2]), nameLen = le16(&t[at + 4]);
        need(std::max<std::size_t>(chunk, 6u + nameLen));
        std::string name(reinterpret_cast<const char*>(&t[at + 6]), nameLen);
        while (!name.empty() && name.back() == '\0') name.pop_back();
        dirs.emplace_back(name, files);
        at += chunk;
    }
    std::vector<Entry> out;
    std::uint64_t data = kIs3Data;
    for (const auto& [dir, files] : dirs) {
        for (unsigned i = 0; i < files; ++i) {
            need(30);
            const std::uint32_t size = le32(&t[at + 3]), packed = le32(&t[at + 7]);
            const unsigned chunk = le16(&t[at + 23]), nameLen = t[at + 29];
            need(std::max<std::size_t>(chunk, 30u + nameLen));
            std::string name(reinterpret_cast<const char*>(&t[at + 30]), nameLen);
            while (!name.empty() && name.back() == '\0') name.pop_back();
            std::string path = dir.empty() ? name : dir + "\\" + name;
            std::replace(path.begin(), path.end(), '\\', '/');
            out.push_back({path, data, size, packed});
            data += packed;
            at += chunk;
        }
    }
    return out;
}

std::vector<std::uint8_t> is3Extract(const Source& s, const Entry& e) {
    const auto packed = s.bytes(e.offset, std::size_t(e.packed));
    auto out = blast(packed, std::size_t(e.size));
    if (out.size() != e.size) throw std::runtime_error("data.z: " + e.path + " has the wrong size");
    return out;
}

// ----------------------------------------------------------------- blast ---
// PKWare DCL implode decoder after Mark Adler's blast.c (zlib contrib).
namespace {
constexpr int kMaxBits = 13;

struct Huffman {
    std::array<short, kMaxBits + 1> count{};
    std::vector<short> symbol;
};

Huffman construct(std::span<const std::uint8_t> rep) {
    std::array<short, 256> length{};
    int n = 0;
    for (const std::uint8_t v : rep)
        for (int left = (v >> 4) + 1; left > 0 && n < 256; --left) length[std::size_t(n++)] = short(v & 15);
    Huffman h;
    for (int i = 0; i < n; ++i) h.count[std::size_t(length[std::size_t(i)])]++;
    if (h.count[0] == n) return h;
    int left = 1;
    for (int len = 1; len <= kMaxBits; ++len) {
        left = (left << 1) - h.count[std::size_t(len)];
        if (left < 0) throw std::runtime_error("blast: bad code table");
    }
    std::array<short, kMaxBits + 1> offs{};
    for (int len = 1; len < kMaxBits; ++len) offs[std::size_t(len + 1)] = short(offs[std::size_t(len)] + h.count[std::size_t(len)]);
    h.symbol.assign(std::size_t(n), 0);
    for (int i = 0; i < n; ++i)
        if (length[std::size_t(i)]) h.symbol[std::size_t(offs[std::size_t(length[std::size_t(i)])]++)] = short(i);
    return h;
}

constexpr std::uint8_t kLitLen[] = {
    11, 124, 8, 7, 28, 7, 188, 13, 76, 4, 10, 8, 12, 10, 12, 10, 8, 23, 8,
    9, 7, 6, 7, 8, 7, 6, 55, 8, 23, 24, 12, 11, 7, 9, 11, 12, 6, 7, 22, 5,
    7, 24, 6, 11, 9, 6, 7, 22, 7, 11, 38, 7, 9, 8, 25, 11, 8, 11, 9, 12,
    8, 12, 5, 38, 5, 38, 5, 11, 7, 5, 6, 21, 6, 10, 53, 8, 7, 24, 10, 27,
    44, 253, 253, 253, 252, 252, 252, 13, 12, 45, 12, 45, 12, 61, 12, 45,
    44, 173};
constexpr std::uint8_t kLenLen[] = {2, 35, 36, 53, 38, 23};
constexpr std::uint8_t kDistLen[] = {2, 20, 53, 230, 247, 151, 248};
constexpr short kBase[16] = {3, 2, 4, 5, 6, 7, 8, 9, 10, 12, 16, 24, 40, 72, 136, 264};
constexpr std::uint8_t kExtra[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8};

struct Bits {
    std::span<const std::uint8_t> in;
    std::size_t at = 0;
    std::uint32_t buf = 0;
    int cnt = 0;
    int get(int need) {
        while (cnt < need) {
            if (at >= in.size()) throw std::runtime_error("blast: out of input");
            buf |= std::uint32_t(in[at++]) << cnt;
            cnt += 8;
        }
        const int v = int(buf & ((1u << need) - 1));
        buf >>= need;
        cnt -= need;
        return v;
    }
    int decode(const Huffman& h) {   // codes are stored bit-inverted
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= get(1) ^ 1;
            const int count = h.count[std::size_t(len)];
            if (code < first + count) return h.symbol[std::size_t(index + (code - first))];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        throw std::runtime_error("blast: bad code");
    }
};
} // namespace

std::vector<std::uint8_t> blast(std::span<const std::uint8_t> in, std::size_t sizeHint) {
    static const Huffman lit = construct(kLitLen), len = construct(kLenLen), dist = construct(kDistLen);
    Bits b{in};
    const int coded = b.get(8), dict = b.get(8);
    if (coded > 1) throw std::runtime_error("blast: bad literal flag");
    if (dict < 4 || dict > 6) throw std::runtime_error("blast: bad dictionary size");
    std::vector<std::uint8_t> out;
    out.reserve(sizeHint);
    for (;;) {
        if (b.get(1)) {
            const int sym = b.decode(len);
            const int n = kBase[sym] + b.get(kExtra[sym]);
            if (n == 519) break;                   // end of stream
            const int shift = n == 2 ? 2 : dict;
            const std::size_t d = std::size_t((b.decode(dist) << shift) + b.get(shift) + 1);
            if (d > out.size()) throw std::runtime_error("blast: distance too far back");
            for (int i = 0; i < n; ++i) out.push_back(out[out.size() - d]);
        } else {
            out.push_back(std::uint8_t(coded ? b.decode(lit) : b.get(8)));
        }
    }
    return out;
}

// ---------------------------------------------------------------- T-TIME ---
bool isTTime(const Source& s) {
    if (s.size() < 0xa8) return false;
    std::uint8_t b[6];
    s.read(0, b, 6);
    return std::memcmp(b, "T-TIME", 6) == 0;
}

// Records of {u8 tag, u32 length, zlib data}: 1 the folder, 2 a file name
// (with the folder or under it), 3 a chunk of the file, 4 (no data) the end
// of the file; the archive ends with tag 5.
bool TTimeReader::next(std::string& path, std::vector<std::uint8_t>& data,
                       const std::function<bool(const std::string&)>& want) {
    bool inFile = false, keep = false;
    data.clear();
    while (at_ + 1 <= s_.size()) {
        std::uint8_t tag;
        s_.read(at_, &tag, 1);
        if (tag == 4) {
            ++at_;
            if (inFile) return true;
            continue;
        }
        if (tag < 1 || tag > 3 || at_ + 5 > s_.size()) break;
        const std::uint32_t n = le32(s_, at_ + 1);
        const std::uint64_t body = at_ + 5;
        if (body + n > s_.size()) throw std::runtime_error("cd1: truncated");
        at_ = body + n;
        if (tag == 3 && !keep) continue;
        const auto packed = s_.bytes(body, n);
        auto plain = inflateZlib(packed);
        if (tag == 3) {
            data.insert(data.end(), plain.begin(), plain.end());
            continue;
        }
        std::string name(reinterpret_cast<const char*>(plain.data()), plain.size());
        name.resize(std::strlen(name.c_str()));
        if (tag == 1) {
            dir_ = name;
            continue;
        }
        auto lower = [](std::string x) {
            for (char& c : x) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            return x;
        };
        std::string full;
        if (name.starts_with('\\') || name.find(':') != std::string::npos ||
            (!dir_.empty() && lower(name).starts_with(lower(dir_))))
            full = name;
        else {
            std::string d = dir_;
            while (!d.empty() && d.back() == '\\') d.pop_back();
            full = d + "\\" + name;
        }
        std::replace(full.begin(), full.end(), '\\', '/');
        while (full.starts_with('/')) full.erase(0, 1);
        path = full;
        inFile = true;
        keep = want(path);
    }
    return false;
}

// ------------------------------------------------------------------- zip ---
namespace {
std::uint64_t findEocd(const Source& s) {
    const std::uint64_t tail = std::min<std::uint64_t>(s.size(), 0x10000 + 22);
    const auto b = s.bytes(s.size() - tail, std::size_t(tail));
    for (std::size_t i = b.size() >= 22 ? b.size() - 22 : 0;; --i) {
        if (b[i] == 'P' && b[i + 1] == 'K' && b[i + 2] == 5 && b[i + 3] == 6) return s.size() - tail + i;
        if (i == 0) break;
    }
    return ~std::uint64_t(0);
}
} // namespace

bool isZip(const Source& s) { return s.size() >= 22 && findEocd(s) != ~std::uint64_t(0); }

std::vector<Entry> zipFiles(const Source& s) {
    const std::uint64_t eocd = findEocd(s);
    if (eocd == ~std::uint64_t(0)) throw std::runtime_error("not a zip archive");
    const auto e = s.bytes(eocd, 22);
    const unsigned count = le16(&e[10]);
    const std::uint32_t cdSize = le32(&e[12]), cdOffset = le32(&e[16]);
    // In a self-extracting exe the offsets count from the start of the zip.
    const std::uint64_t shift = eocd - (std::uint64_t(cdOffset) + cdSize);
    const auto cd = s.bytes(cdOffset + shift, cdSize);
    std::vector<Entry> out;
    std::size_t at = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (at + 46 > cd.size() || le32(&cd[at]) != 0x02014b50) throw std::runtime_error("zip: bad central directory");
        const unsigned method = le16(&cd[at + 10]);
        const std::uint32_t packed = le32(&cd[at + 20]), size = le32(&cd[at + 24]);
        const unsigned nameLen = le16(&cd[at + 28]), extraLen = le16(&cd[at + 30]), commentLen = le16(&cd[at + 32]);
        const std::uint32_t local = le32(&cd[at + 42]);
        if (at + 46 + nameLen > cd.size()) throw std::runtime_error("zip: bad central directory");
        std::string name(reinterpret_cast<const char*>(&cd[at + 46]), nameLen);
        at += 46 + nameLen + extraLen + commentLen;
        if (name.empty() || name.back() == '/') continue;
        const auto lh = s.bytes(local + shift, 30);
        if (le32(&lh[0]) != 0x04034b50) throw std::runtime_error("zip: bad local header");
        const std::uint64_t data = local + shift + 30 + le16(&lh[26]) + le16(&lh[28]);
        out.push_back({name, data, size, packed, int(method)});
    }
    return out;
}

std::vector<std::uint8_t> zipExtract(const Source& s, const Entry& e) {
    auto packed = s.bytes(e.offset, std::size_t(e.packed));
    if (e.method == 0) return packed;
    if (e.method == 8) return inflateRaw(packed, std::size_t(e.size));
    throw std::runtime_error("zip: unsupported compression in " + e.path);
}

} // namespace ch::import
