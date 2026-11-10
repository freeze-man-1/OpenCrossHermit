// ============================================================================
// language_pack.hpp — extra languages, added as .zip files on the game files
// screen and kept in <data>/lang/<id>/ (edit that folder to work on a pack).
//
//   language.json                       id, name, base ("jp"), author, version
//   data/...                            replaces the game file at that path
//   text/*.tsv                          original<TAB>translation, UTF-8
//   images/data/.../file.bin/<n>.png    painted over frame n of that bank
//
// A pack runs on its base release: lookups go pack, then base, then common,
// and whatever it leaves out stays in the base language. The game logic is
// the base's, so saves and network play are unaffected.
// ============================================================================
#pragma once
#include "../bank_format.hpp"
#include "../game_context.hpp"
#include "../import/importer.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ch::lang {

struct Pack { std::string id, name, base, author, version; };
// Something the player can pick: a release ("jp", "tw") or a pack.
struct Choice { std::string id, name; };

[[nodiscard]] std::vector<Pack> packs();
// Is this file a zip with a language.json, at its root or in one top folder?
[[nodiscard]] bool isPackFile(const std::string& path);
// Checks and installs (replacing an installed pack of that id). On failure
// nothing changes and `error` says why.
bool installPack(const std::string& path, Pack& out, std::string& error);
bool removePack(const std::string& id);

// The releases and the packs whose base is installed.
[[nodiscard]] std::vector<Choice> choices(const import::Installed& have);
// The saved choice if it can still be played, else as before packs: Japanese, else Chinese.
[[nodiscard]] std::string preferred(const import::Installed& have);
void savePreferred(const std::string& id);
[[nodiscard]] GameVersion versionOf(const std::string& id);
// Makes a pack's files, text and images take effect; a release's id or "" ends that.
// The pack's images are painted on a thread of their own, so the game runs meanwhile.
void activate(const std::string& id);
// Stops that painting (before the files it reads change). activate() calls it first.
void stopPainting();

// A pack's images/<game file>/layout.txt moves sprites whose new text doesn't fit the old
// rectangle: one sprite of the original becomes these parts. dx, dy are the part's offset
// from the old rectangle's top left, in texels.
struct SpritePart {
    int frame = 0, x0 = 0, y0 = 0, x1 = 0, y1 = 0, dx = 0, dy = 0;
};
[[nodiscard]] const std::vector<SpritePart>* spriteParts(const std::string& bank, int frame, int x0, int y0, int x1, int y1);

// 8-bit RGB / RGBA, not interlaced. Empty on anything else.
[[nodiscard]] Image decodePng(std::span<const std::uint8_t> png);
// One change to a bank's frame, the same size as it. Where `image` isn't transparent:
//   Erase  the frame's texels become transparent
//   Diff   its colour is added to theirs, mod 256, and they are opaque: a pack can rebuild
//          the art under a word without holding any of the game's texels
//   Over   it is blended on
// paintFrames applies them in the order given; packs give each frame's in this order.
enum class PaintKind : std::uint8_t { Erase, Diff, Over };
struct FramePaint {
    int       frame = 0;
    PaintKind kind = PaintKind::Over;
    Image     image;
};
// Applies them in order and rewrites the bank once. On false it is unchanged.
bool paintFrames(std::vector<std::uint8_t>& bank, const std::vector<FramePaint>& paints, std::string& error);

} // namespace ch::lang
