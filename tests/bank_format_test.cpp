// Decodes the real Title.bin. Usage: bank_format_test <imported data folder>
// (exit 77 without it).
// Expected values were measured independently from the file with Python.
#include "../game/bank_format.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>
#include <iterator>

int main(int argc, char** argv) {
    if (argc < 2) { std::puts("SKIP: no data folder given"); return 77; }
    std::ifstream f(std::string(argv[1]) + "/jp/data/tactics/title.bin", std::ios::binary);
    if (!f) f.open(std::string(argv[1]) + "/common/data/tactics/title.bin", std::ios::binary);
    if (!f) { std::puts("SKIP: no imported game data"); return 77; }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    const auto entries = ch::splitBank(data);
    assert(entries.size() == 8);

    const int want[8][2] = {{32, 32}, {1024, 769}, {310, 93}, {1024, 768},
                            {1024, 768}, {1024, 768}, {270, 120}, {400, 222}};
    std::vector<ch::Image> frames;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::string why;
        frames.push_back(ch::decodeBankEntry(entries[i], &why));
        std::printf("frame %zu: %dx%d %s\n", i, frames[i].w, frames[i].h, why.c_str());
        assert(frames[i].w == want[i][0] && frames[i].h == want[i][1]);
    }
    std::size_t opaque = 0;
    for (auto p : frames[1].argb) opaque += (p >> 24) != 0;
    std::printf("background opaque texels: %zu\n", opaque);
    assert(opaque == 525583);
    assert((frames[0].argb[0] >> 24) == 0xFF);              // arrow tip (hotspot) is opaque
    assert((frames[0].argb[32 * 32 - 1] >> 24) == 0);        // far corner is colour-keyed
    std::puts("OK");
}
