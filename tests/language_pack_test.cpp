// ============================================================================
// language_pack_test.cpp — installing packs, their lookups, text and images.
// ============================================================================
#include "../game/lang/language_pack.hpp"
#include "../game/text/sjis.hpp"
#include "../engine/engine_core.hpp"

#include <zlib.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;
using ch::lang::PaintKind::Erase, ch::lang::PaintKind::Diff, ch::lang::PaintKind::Over;

int failures = 0;
void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

Bytes bytesOf(const std::string& s) { return {s.begin(), s.end()}; }
void put(Bytes& b, std::uint32_t v, int n) { for (int k = 0; k < n; ++k) b.push_back(std::uint8_t(v >> (k * 8))); }
void putBe32(Bytes& b, std::uint32_t v) { for (int k = 3; k >= 0; --k) b.push_back(std::uint8_t(v >> (k * 8))); }

// A zip of stored files.
void writeZip(const fs::path& to, const std::vector<std::pair<std::string, Bytes>>& files) {
    Bytes out, central;
    for (const auto& [name, data] : files) {
        const std::uint32_t at = std::uint32_t(out.size()), size = std::uint32_t(data.size());
        const std::uint32_t crc = std::uint32_t(crc32(0, data.data(), uInt(size)));
        const auto header = [&](Bytes& b) {
            put(b, 0, 2); put(b, 0, 2); put(b, 0, 2); put(b, 0, 2);   // flags, method, time, date
            put(b, crc, 4); put(b, size, 4); put(b, size, 4);
            put(b, std::uint32_t(name.size()), 2); put(b, 0, 2);
        };
        put(out, 0x04034b50, 4); put(out, 20, 2); header(out);
        out.insert(out.end(), name.begin(), name.end());
        out.insert(out.end(), data.begin(), data.end());
        put(central, 0x02014b50, 4); put(central, 20, 2); put(central, 20, 2); header(central);
        put(central, 0, 2); put(central, 0, 2); put(central, 0, 2); put(central, 0, 4); put(central, at, 4);
        central.insert(central.end(), name.begin(), name.end());
    }
    const std::uint32_t dir = std::uint32_t(out.size());
    out.insert(out.end(), central.begin(), central.end());
    put(out, 0x06054b50, 4); put(out, 0, 2); put(out, 0, 2);
    put(out, std::uint32_t(files.size()), 2); put(out, std::uint32_t(files.size()), 2);
    put(out, std::uint32_t(central.size()), 4); put(out, dir, 4); put(out, 0, 2);
    std::ofstream f(to, std::ios::binary);
    f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
}

// RGBA rows, filter 0.
Bytes png(int w, int h, const std::vector<std::uint32_t>& argb) {
    Bytes raw;
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            const std::uint32_t c = argb[std::size_t(y * w + x)];
            raw.push_back(std::uint8_t(c >> 16)); raw.push_back(std::uint8_t(c >> 8));
            raw.push_back(std::uint8_t(c)); raw.push_back(std::uint8_t(c >> 24));
        }
    }
    uLongf packedSize = compressBound(uLong(raw.size()));
    Bytes packed(packedSize);
    compress(packed.data(), &packedSize, raw.data(), uLong(raw.size()));
    packed.resize(packedSize);
    Bytes out{0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    const auto chunk = [&](const char* type, const Bytes& d) {
        putBe32(out, std::uint32_t(d.size()));
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), d.begin(), d.end());
        putBe32(out, 0);
    };
    Bytes ihdr;
    putBe32(ihdr, std::uint32_t(w));
    putBe32(ihdr, std::uint32_t(h));
    for (int v : {8, 6, 0, 0, 0}) ihdr.push_back(std::uint8_t(v));
    chunk("IHDR", ihdr);
    chunk("IDAT", packed);
    chunk("IEND", {});
    return out;
}

// A bank of two frames made with the port's own entry type.
Bytes bank2x1() {
    const Bytes a = ch::encodeRgbaEntry({2, 1, {0xFF102030u, 0xFF405060u}});
    const Bytes b = ch::encodeRgbaEntry({1, 1, {0xFF777777u}});
    Bytes out;
    const std::uint32_t first = 16;
    put(out, first + std::uint32_t(a.size() + b.size()), 4);
    put(out, 2, 4);
    put(out, first, 4);
    put(out, first + std::uint32_t(a.size()), 4);
    out.insert(out.end(), a.begin(), a.end());
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

const std::string kManifest = R"({"id": "en", "name": "English", "base": "jp", "author": "t", "version": "1"})";
} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "ch_language_pack_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "jp/data");
    fs::create_directories(root / "common/data");
    legacy::vfs::setDataRoot(root.string() + "/");
    legacy::vfs::setVariant("jp");
    const auto write = [&](const fs::path& p, const Bytes& b) {
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    };
    write(root / "jp/data/a.txt", bytesOf("jp a"));
    write(root / "jp/data/b.txt", bytesOf("jp b"));
    write(root / "common/data/c.txt", bytesOf("common c"));
    write(root / "jp/data/bank.bin", bank2x1());

    // A zipped folder, with a README and an escape attempt that must be ignored.
    const fs::path zip = root / "pack.zip";
    writeZip(zip, {{"english/language.json", bytesOf(kManifest)},
                   {"english/README.txt", bytesOf("hello")},
                   {"english/data/A.TXT", bytesOf("en a")},
                   {"english/../../evil.txt", bytesOf("no")},
                   {"english/data/../../../evil2.txt", bytesOf("no")},
                   {"english/text/port.tsv", bytesOf("\xef\xbb\xbf# comment\n言語\tLanguage\n音声\t\n二行\tOne\\nTwo\r\n言語\\n無い\tA passage\\nin two\r\n")},
                   {"english/images/data/bank.bin/0.png", png(2, 1, {0xFFFF0000u, 0x00000000u})}});
    check(ch::lang::isPackFile(zip.string()), "a pack is recognised");
    ch::lang::Pack p;
    std::string error;
    check(ch::lang::installPack(zip.string(), p, error), error.c_str());
    check(p.id == "en" && p.name == "English", "the manifest is read");
    check(!fs::exists(root / "evil.txt") && !fs::exists(root / "evil2.txt") && !fs::exists(root.parent_path() / "evil.txt"), "nothing leaves the folder");
    check(!fs::exists(root / "lang/en/readme.txt"), "other files are left out");
    const auto all = ch::lang::packs();
    check(all.size() == 1 && all[0].id == "en", "the pack is listed");

    // The picker: a pack needs its base.
    ch::import::Installed have;
    have.jp = true;
    check(ch::lang::choices(have).size() == 2, "jp and the pack");
    have.jp = false;
    have.tw = true;
    check(ch::lang::choices(have).size() == 1, "no pack without jp");
    have.jp = true;

    // Lookups fall through pack, base, common.
    ch::lang::activate("en");
    const auto text = [](const char* path) { const Bytes b = legacy::vfs::readAsset(path); return std::string(b.begin(), b.end()); };
    check(text("data\\a.txt") == "en a", "the pack's file wins");
    check(text("data\\b.txt") == "jp b", "the base fills in");
    check(text("data\\c.txt") == "common c", "then common");
    check(ch::lang::versionOf("en") == ch::GameVersion::Jp, "a pack runs on jp");

    // Text. "言語" and "音声" in Shift-JIS.
    check(ch::sjisToUtf8("\x8c\xbe\x8c\xea") == "Language", "decoded text is translated");
    check(ch::sjisToUtf8("\x89\xb9\x90\xba") == "音声", "a blank translation leaves the original");
    check(ch::tr("言語") == "Language" && ch::tr("二行") == "One\nTwo" && ch::tr("無い") == "無い", "the port's labels");

    // A passage is looked up whole, whatever its lines' own translations; "言語" + "無い".
    check(ch::translatePassage({"\x8c\xbe\x8c\xea", "\x96\xb3\x82\xa2"}) == "A passage\nin two", "a passage");
    check(ch::translatePassage({"\x96\xb3\x82\xa2"}).empty(), "no such passage");

    // The image: red over the first texel, the transparent one leaves the second.
    const Bytes painted = legacy::vfs::readAsset("data\\bank.bin");
    const auto frames = ch::splitBank(painted);
    check(frames.size() == 2, "the bank keeps its frames");
    const ch::Image first = ch::decodeBankEntry(frames[0]), second = ch::decodeBankEntry(frames[1]);
    check(first.w == 2 && first.argb[0] == 0xFFFF0000u && first.argb[1] == 0xFF405060u, "the png is painted on");
    check(second.argb.size() == 1 && second.argb[0] == 0xFF777777u, "other frames are untouched");

    // Half-transparent paint, and the wrong size.
    Bytes bank = bank2x1();
    check(ch::lang::paintFrames(bank, {{1, Over, {1, 1, {0x80FFFFFFu}}}}, error), "half alpha");
    const std::uint32_t mixed = ch::decodeBankEntry(ch::splitBank(bank)[1]).argb[0];
    check((mixed >> 24) == 0xff && (mixed & 0xff) > 0xb0 && (mixed & 0xff) < 0xc0, "half alpha mixes");
    check(!ch::lang::paintFrames(bank, {{1, Over, {2, 2, {0, 0, 0, 0}}}}, error), "a wrong size is refused");
    check(!ch::lang::paintFrames(bank, {{5, Over, {1, 1, {0}}}}, error), "a missing frame is refused");

    // Erasing leaves the frame see-through, and what follows stays half-transparent on it.
    Bytes erased = bank2x1();
    check(ch::lang::paintFrames(erased, {{0, Erase, {2, 1, {0xFF000000u, 0}}}, {0, Over, {2, 1, {0x80FFFFFFu, 0}}}, {1, Erase, {1, 1, {0xFF000000u}}}}, error), "erase");
    const auto afterErase = ch::splitBank(erased);
    const ch::Image e0 = ch::decodeBankEntry(afterErase[0]), e1 = ch::decodeBankEntry(afterErase[1]);
    check((e0.argb[0] >> 24) == 0x80 && (e0.argb[0] & 0xff) == 0xff, "paint on an erased texel keeps its alpha");
    check(e0.argb[1] == 0xFF405060u, "a blank erase texel keeps the frame");
    check(e1.argb[0] == 0u, "erased texels are transparent");
    check(ch::lang::decodePng(bytesOf("not a png")).empty(), "garbage is not a png");

    // A diff adds to the frame's colour, wrapping, makes the texel opaque, and skips its unmarked texels.
    Bytes diffed = bank2x1();
    check(ch::lang::paintFrames(diffed, {{0, Erase, {2, 1, {0xFF000000u, 0}}}, {0, Diff, {2, 1, {0xFF0110F0u, 0x00FFFFFFu}}}}, error), "diff");
    const ch::Image d0 = ch::decodeBankEntry(ch::splitBank(diffed)[0]);
    check(d0.argb[0] == 0xFF0110F0u && d0.argb[1] == 0xFF405060u, "a diff texel is opaque, and blank ones keep the frame");
    Bytes wrapped = bank2x1();
    check(ch::lang::paintFrames(wrapped, {{0, Diff, {2, 1, {0xFF0110F0u, 0}}}}, error), "diff");
    check(ch::decodeBankEntry(ch::splitBank(wrapped)[0]).argb[0] == 0xFF113020u, "a diff wraps");

    // A painted bank is kept, so it isn't painted again; ending the pack forgets it.
    legacy::vfs::cacheAsset("data/a.txt", bytesOf("cached"));
    check(text("data\\a.txt") == "cached", "a kept file is read instead");

    ch::lang::activate({});
    check(text("data\\a.txt") == "jp a" && ch::sjisToUtf8("\x8c\xbe\x8c\xea") == "言語", "ending a pack restores the base");
    check(ch::translatePassage({"\x8c\xbe\x8c\xea", "\x96\xb3\x82\xa2"}).empty(), "and its passages");

    // Bad manifests change nothing.
    const auto refused = [&](const std::string& json, const char* what) {
        writeZip(zip, {{"language.json", bytesOf(json)}});
        ch::lang::Pack q;
        std::string why;
        check(!ch::lang::installPack(zip.string(), q, why) && !why.empty(), what);
    };
    refused(R"({"id": "jp", "name": "x", "base": "jp"})", "a release's id");
    refused(R"({"id": "../x", "name": "x", "base": "jp"})", "a path as id");
    refused(R"({"id": "x", "name": "", "base": "jp"})", "no name");
    refused(R"({"id": "x", "name": "x", "base": "tw"})", "another base");
    check(ch::lang::packs().size() == 1, "refused packs are not installed");
    writeZip(zip, {{"data/a.txt", bytesOf("x")}});
    check(!ch::lang::isPackFile(zip.string()), "a zip without a manifest is not a pack");

    // Replacing, removing.
    writeZip(zip, {{"language.json", bytesOf(R"({"id": "en", "name": "English 2", "base": "jp"})")}});
    check(ch::lang::installPack(zip.string(), p, error), "replacing");
    check(ch::lang::packs()[0].name == "English 2" && !fs::exists(root / "lang/en/data/a.txt"), "replacing drops the old files");
    check(ch::lang::removePack("en") && ch::lang::packs().empty(), "removing");

    fs::remove_all(root, ec);
    if (failures == 0) std::puts("language pack: ok");
    return failures == 0 ? 0 : 1;
}
