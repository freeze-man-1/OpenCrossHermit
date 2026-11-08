// ============================================================================
// sjis.hpp — Shift-JIS (code page 932) to UTF-8
// ============================================================================
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ch {

// Unmappable bytes become U+FFFD. Half-width katakana (0xa1-0xdf) is mapped.
[[nodiscard]] std::string sjisToUtf8(std::string_view sjis);
// Big5 (code page 950), the TW build's own strings.
[[nodiscard]] std::string big5ToUtf8(std::string_view big5);
// Text from the running release's data files (scripts, YsText): Big5 for
// the TW release, Shift-JIS for the JP one (legacy::vfs::variant()).
[[nodiscard]] std::string gameTextToUtf8(std::string_view raw);

// A language pack's translations, keyed by textKey of the Japanese: sjisToUtf8
// returns the translation of whatever it decodes. Null for none.
void setTextTable(const std::unordered_map<std::string, std::string>* table);
// "@" and FNV-1a 64 of the UTF-8 in hex. A pack keyed this way holds no game text.
[[nodiscard]] std::string textKey(std::string_view utf8);
// A passage of game text, by its raw Shift-JIS lines: the translation of them as one,
// with its own line breaks, or empty if the table has none.
[[nodiscard]] std::string translatePassage(const std::vector<std::string>& raw);
// The port's own Japanese labels go through the same table.
[[nodiscard]] std::string tr(std::string_view japanese);
// Changes with the table, for anything that keeps decoded text.
[[nodiscard]] int textGeneration();

} // namespace ch
