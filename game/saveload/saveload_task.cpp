// ============================================================================
// saveload_task.cpp
// ============================================================================
#include "saveload_task.hpp"
#include "../state/exe_image.hpp"
#include "../state/game_logic.hpp"
#include "../text/sjis.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <optional>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
struct WebSave { std::string name, path; };
std::vector<WebSave>& webSaves() { static std::vector<WebSave> q; return q; }
} // namespace

// Called by web/shell.html for each save the player chose (written to
// /import/; an empty path means the file was too big to be a save).
extern "C" EMSCRIPTEN_KEEPALIVE void ch_web_add_save(const char* name, const char* path) {
    webSaves().push_back({name, path});
}
#endif

namespace ch {

using ysub::drawRect;
using ysub::fill;

namespace {
constexpr const char* kFolder = "SaveData";        // s_SaveData__00598694
constexpr std::uint32_t kMode   = 0x7e11a8;         // s16: 0/1/5/6 load, 2/3 save
constexpr std::uint32_t kResult = 0x7e11aa;         // s16: 0 cancelled, 1 loaded, 3 loaded (mode 6)
constexpr std::uint32_t kSe = 5;

// Messages (DAT_005c82a8.. in ch.exe, 0x53d020.. in the JP exe).
struct Text {
    std::string checking;                           // kinds 0 / 1
    std::array<std::string, 3> enterName;           // kind 2
    std::array<std::string, 2> imeOff;              // kind 3
    std::string badChars;
    std::string badName;
    std::string prefix;                             // default file name
};
// The strings FUN_004d8620 and its helpers push: JP exe / ch.exe addresses.
struct TextVa {
    std::uint32_t checking;
    std::array<std::uint32_t, 3> enterName;
    std::array<std::uint32_t, 2> imeOff;
    std::uint32_t badChars, badName, prefix;
};
constexpr TextVa kJp{0x53d0a4, {0x53d084, 0x53d064, 0x53d040}, {0x53d020, 0x53d040}, 0x53d104, 0x53d0dc, 0x53d124};
constexpr TextVa kTw{0x5c8304, {0x5c82f8, 0x5c82e4, 0x5c82d0}, {0x5c82bc, 0x5c82a8}, 0x5c8348, 0x5c8330, 0x5c8368};

const Text& texts(GameVersion v) {
    static std::array<std::optional<Text>, 2> cache;
    static int generation = textGeneration();
    if (generation != textGeneration()) {
        generation = textGeneration();
        cache = {};
    }
    auto& t = cache[std::size_t(v)];
    if (!t) {
        const TextVa& a = v == GameVersion::Tw ? kTw : kJp;
        auto str = [v](std::uint32_t va) { return exe::text(v, va); };
        // Both builds write the 14 date digits at byte 16 of the prefix
        // ("ＣＨセーブデータXXXXXXXXXXXXXX"), so the prefix is its first 16 bytes.
        const std::string_view raw = exe::own(v).cstr(a.prefix).substr(0, 16);
        t = Text{str(a.checking), {str(a.enterName[0]), str(a.enterName[1]), str(a.enterName[2])},
                 {str(a.imeOff[0]), str(a.imeOff[1])}, str(a.badChars), str(a.badName),
                 v == GameVersion::Tw ? big5ToUtf8(raw) : sjisToUtf8(raw)};
    }
    return *t;
}

// Rect-table rows (DAT_00625618 + 16 i).
constexpr std::uint32_t kCursor = 0x625618, kLoadBg = 0x625648, kLoadScroll = 0x625658, kLoadOk = 0x625698,
                        kLoadCancel = 0x6256a8, kSaveBg = 0x6256b8, kSaveScroll = 0x6256c8, kSaveBtn = 0x625708,
                        kDeleteBtn = 0x625718, kSaveCancel = 0x625728, kOverwrite = 0x625758, kDeleteAsk = 0x625788,
                        kMessage = 0x6257b8, kError = 0x6257c8;

bool endsWithSav(const std::string& f) {
    if (f.size() < 4) return false;
    std::string e = f.substr(f.size() - 4);
    for (char& c : e) c = char(std::tolower(static_cast<unsigned char>(c)));
    return e == ".sav";
}

// FUN_00429b40: a usable Windows file name.
bool validName(const std::string& n) {
    if (n.empty()) return false;
    return n.find_first_of("\\/:*?\"<>|") == std::string::npos;
}

// The web build's buttons: in the gaps beside the save screen's buttons,
// outside the load screen's two.
constexpr int kWebW = 0x44, kWebH = 0x1f;
constexpr int kWebSaveX[2] = {0x174, 0x24d}, kWebSaveY = 0xf3;
constexpr int kWebLoadX[2] = {0xec, 0x2d4}, kWebLoadY = 0x272;

#ifdef __EMSCRIPTEN__
constexpr int kNoticeFrames = 300;
constexpr std::size_t kMaxSaves = 200;

struct WebText {
    const char* download; const char* upload;
    const char* added; const char* same; const char* full;
    const char* size; const char* format; const char* checksum; const char* contents;
};
constexpr WebText kWebJp{"ダウンロード", "アップロード",
    "追加しました", "同じデータがすでにあります", "セーブデータが多すぎます（200個まで）",
    "セーブデータではありません（サイズが違います）", "セーブデータではありません（形式が違います）",
    "セーブデータが壊れています（チェックサム不一致）", "セーブデータの内容が正しくありません"};
constexpr WebText kWebTw{"下載", "上傳",
    "已加入", "已有相同的存檔", "存檔過多（上限 200 個）",
    "這不是存檔（檔案大小不符）", "這不是存檔（格式不符）",
    "存檔已損壞（校驗碼不符）", "存檔內容不正確"};

// An uploaded file's name as a save name: no folders or extension, nothing
// the save screen would refuse, no longer than it allows.
std::string uploadName(std::string n) {
    if (const auto s = n.find_last_of("/\\"); s != std::string::npos) n.erase(0, s + 1);
    if (endsWithSav(n)) n.resize(n.size() - 4);
    for (char& c : n)
        if (static_cast<unsigned char>(c) < 0x20 || std::string_view("\\/:*?\"<>|").find(c) != std::string_view::npos) c = '_';
    if (n.size() > 0x40) {
        std::size_t cut = 0x40;                                      // not inside a UTF-8 character
        while (cut > 0 && (static_cast<unsigned char>(n[cut]) & 0xc0) == 0x80) --cut;
        n.resize(cut);
    }
    return n;
}
#endif

} // namespace

SaveLoadTask::~SaveLoadTask() {
    ctx_.sprites->unloadBank(kBank);
#ifdef __EMSCRIPTEN__
    EM_ASM({ Module.chSaveScreen = false; });
#endif
}

ysub::Rect SaveLoadTask::rect(std::uint32_t va) const { return ysub::rectAt(va, ctx_.version); }

bool SaveLoadTask::button(int x, int y, int w, int h) const {
    return (ysub::hit(ctx_, x, y, w, h).pressed & legacy::kMouseLeft) != 0;
}

// FUN_004d9d50
Co<> SaveLoadTask::start(std::uint32_t) {
    GameWork& w = ctx_.work;
    // FUN_004d7830
    w.setAreaBgmOn(true);
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\SLProg\\SLProg.bin"));
    shownMessage_ = -1;
    message_.setFont(15, 5);
    list_.setFont(12, 0x50);
    nameLine_.setFont(12, 1);
    flashes_.clear();
#ifdef __EMSCRIPTEN__
    EM_ASM({ Module.chSaveScreen = true; });
#endif

    const int mode = w.s16(kMode);
    switch (mode) {
        case 0: case 1: case 5: case 6: co_await loadScreen(); break;
        case 2: case 3: co_await saveScreen(); break;
        default: break;
    }
    co_await yieldFrame();
    switch (w.s16(kResult)) {
        case 0:
            switch (mode) {
                case 1: case 2: ctx_.scenes.request(SceneId::HanKojin); break;
                case 3: ctx_.scenes.request(SceneId::NetJoin); break;
                case 5: break;
                case 6: w.set16(0x7e11ac, 0); break;
                default: ctx_.scenes.request(SceneId::Title); break;
            }
            break;
        case 1: ctx_.scenes.request(SceneId::HanKojin); break;
        case 3: w.set16(0x7e11ac, 1); break;
        case 4:
            if (mode == 2) ctx_.scenes.request(SceneId::HanKojin);
            else if (mode == 3) ctx_.scenes.request(SceneId::NetJoin);
            else if (mode != 4) ctx_.scenes.request(SceneId::Title);
            break;
        default: break;
    }
    w.set16(0x7e11ae, 0);
}

// ------------------------------------------------------------------- list ---
// FUN_004d6cb0 / FUN_004d73c0: every valid save with its in-game date (year
// = 0x7a528c, month, week) and file time, newest first.
void SaveLoadTask::scan(int visible) {
    entries_.clear();
    list_.reset(visible);
    for (const auto& f : legacy::vfs::listSaveFolder(kFolder)) {
        if (!endsWithSav(f.name) || entries_.size() == 200) continue;
        const auto bytes = legacy::vfs::readSaveFile(kFolder, f.name);
        if (!bytes || !logic::saveValid(*bytes)) continue;
        const auto s16 = [&](std::uint32_t va) {
            const std::size_t o = va - 0x7a50e8;
            return int(std::int16_t((*bytes)[o] | (*bytes)[o + 1] << 8));
        };
        int month = s16(0x7a528e);
        if (month > 12) month -= 12;
        const std::time_t t = static_cast<std::time_t>(f.mtime);
        std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt, &t);
#else
        localtime_r(&t, &lt);
#endif
        Entry e;
        e.file = f.name;
        e.name = f.name.substr(0, f.name.size() - 4);
        const int year = s16(0x7a528c);
        char date[48], when[48];
        std::snprintf(date, sizeof date, "%c%c年 %02d月 %d週", year / 10 ? char('0' + year / 10 % 10) : ' ',
                      char('0' + year % 10), month % 100, s16(0x7a5290) % 10);
        std::snprintf(when, sizeof when, "%04d/%02d/%02d %02d:%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                      lt.tm_hour, lt.tm_min);
        e.date = date;
        e.time = when;
        e.stamp = (std::int64_t(lt.tm_year + 1900) % 100) * 0x1ea6e00 + std::int64_t(lt.tm_mon + 1) * 0x28de80 +
                  std::int64_t(lt.tm_mday) * 0x15180 + lt.tm_hour * 0xe10 + lt.tm_min * 0x3c + lt.tm_sec;
        entries_.push_back(std::move(e));
    }
    std::stable_sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) { return a.stamp > b.stamp; });
    for (const Entry& e : entries_) list_.addUtf8(e.name);   // the list keeps the scroll position
}

// FUN_004d7a80: kind 0 at the top (load), 1..3 lower (save).
void SaveLoadTask::messageBox(int kind) {
    const Text& t = texts(ctx_.version);
    if (shownMessage_ != kind) {
        message_.reset(5);
        switch (kind) {
            case 0: case 1: message_.addUtf8("   "); message_.addUtf8(t.checking); break;
            case 2: for (const auto& l : t.enterName) message_.addUtf8(l); break;
            case 3: for (const auto& l : t.imeOff) message_.addUtf8(l); break;
            default: break;
        }
    }
    shownMessage_ = kind;
    const bool top = kind == 0;
    message_.draw(ctx_, top ? 0x17f : 0x17e, top ? 0x16c : 0x19c, 0x53);
    flashes_.add(rect(kMessage), 0x54, top ? 0x17a : 0x179, top ? 0x162 : 0x192, 1);
    fill(ctx_, 0xaa, top ? 0xb7 : 300, 0x2b7, top ? 0x1b2 : 0x13d, 0x55, 0xc0101010u);
}

// The original's row is one string in an 11 px fixed-pitch font: name in
// columns 0..0x3f, game date from column 0x4a, file time from 0x5d.
void SaveLoadTask::drawRows(int y, int visible) {
    constexpr double kColumn = 5.5;
    for (int r = 0; r < visible && top_ + r < int(entries_.size()); ++r) {
        const Entry& e = entries_[std::size_t(top_ + r)];
        const int ry = y + r * 0xe;
        const auto put = [&](int col, const std::string& t) {
            ctx_.draw.text({.x = 0xb0 + int(col * kColumn), .y = ry, .priority = 0x59, .argb = 0xFFFFFFFFu,
                            .text = t, .fontPx = 12});
        };
        put(0, e.name);
        put(0x4a, e.date);
        put(0x5d, e.time);
    }
}

void SaveLoadTask::drawLoad() {
    drawRows(0xbe, 0x1e);
    if (selected_ == -1) fill(ctx_, 0x13f, 0x271, 0x8d, 0x1f, 0x52, 0xa0010101u);
    webDraw({kWebLoadX[0], kWebLoadX[1], kWebLoadY});
    const ysub::Rect bg = rect(kLoadBg);
    drawRect(ctx_, bg, 0x5d, 0, 0);
    drawRect(ctx_, rect(kCursor), 0x50, ctx_.mouse().x, ctx_.mouse().y);
    flashes_.draw(ctx_);
}

void SaveLoadTask::drawSave() {
    nameLine_.draw(ctx_, 0xb0, 0xba, 0x59);
    drawRows(0x132, 0x16);
    if (selected_ == -1) fill(ctx_, 0x1bc, 0xf3, 0x8d, 0x1f, 0x52, 0xa0010101u);
    webDraw({kWebSaveX[0], kWebSaveX[1], kWebSaveY});
    drawRect(ctx_, rect(kSaveBg), 0x5d, 0, 0);
    drawRect(ctx_, rect(kCursor), 0x50, ctx_.mouse().x, ctx_.mouse().y);
    flashes_.draw(ctx_);
}

Co<> SaveLoadTask::fade(bool in, bool save) {
    for (int i = 0; i < 0x1e; ++i) {
        const int a = in ? 0xff - i * 0xff / 0x1d : 0xff - (0x1d - i) * 0xff / 0x1d;
        fill(ctx_, 0, 0, 0x400, 0x300, 0x51, std::uint32_t(a) << 24 | 0x10101u);
        if (save) drawSave(); else drawLoad();
        co_await yieldFrame();
    }
}

// Scroll buttons at x 0x34b (top, up, down, bottom); returns the button + 1.
int SaveLoadTask::scrollButtons(int y0, int pitch, int h, std::uint32_t flashVa) {
    constexpr std::array<int, 4> kCmd{2, 0, 1, 3};
    for (int i = 0; i < 4; ++i) {
        if (!button(0x34b, i * pitch + y0, 0x14, h)) continue;
        ctx_.audio->sePlay(kSe);
        top_ = list_.scroll(kCmd[std::size_t(i)]);
        flashes_.add(rect(flashVa + std::uint32_t(i) * 0x10), 0x5a, 0x349, i * pitch + y0 - 2, 8);
        return i + 1;
    }
    return 0;
}

// ------------------------------------------------------------ load screen ---
Co<> SaveLoadTask::loadScreen() {
    GameWork& w = ctx_.work;
    selected_ = -1;
    co_await fade(true, false);
    for (int i = 0; i < 0x20; ++i) { messageBox(0); drawLoad(); co_await yieldFrame(); }
    scan(0x1e);
    selected_ = -1;
    top_ = 0;
    webLive_ = true;
    for (;;) {
        (void)webInput({kWebLoadX[0], kWebLoadX[1], kWebLoadY}, 0x1e);
        for (int r = 0; r < 0x1e && r + top_ <= int(entries_.size()) - 1; ++r) {
            const ysub::Hit h = ysub::hit(ctx_, 0xb0, r * 0xe + 0xbe, 0x294, 0xd);
            if (h.over) {
                fill(ctx_, 0xad, r * 0xe + 0xbe, 0x298, 0xd, 0x58, 0x8020a000u, kBlendAdd);
                if (h.pressed & legacy::kMouseLeft) { ctx_.audio->sePlay(kSe); selected_ = r + top_; }
            }
            if (selected_ == r + top_) fill(ctx_, 0xad, r * 0xe + 0xbe, 0x298, 0xd, 0x57, 0xa0c0a000u, kBlendAdd);
        }
        if (button(0x237, 0x272, 0x8d, 0x1f)) {                    // cancel
            webLive_ = false;
            ctx_.audio->sePlay(1);
            const ysub::Rect r = rect(kLoadCancel);
            flashes_.add(r, 0x5a, 0x22d, 0x270, 8);
            co_await fade(false, false);
            w.set16(kResult, 0);
            co_return;
        }
        if (button(0x140, 0x272, 0x8d, 0x1f)) {
            ctx_.audio->sePlay(kSe);
            if (selected_ >= 0) {
                webLive_ = false;
                flashes_.add(rect(kLoadOk), 0x5a, 0x136, 0x270, 8);
                w.setAreaBgmOn(false);
                // FUN_00450230 builds the SaveData path; after the load
                // FUN_004e1d70 is called, which does nothing.
                const auto bytes = legacy::vfs::readSaveFile(kFolder, entries_[std::size_t(selected_)].file);
                if (bytes && logic::loadGame(w, *bytes)) {
                    const int mode = w.s16(kMode);
                    if (mode == 0 || mode == 1 || mode == 5) w.set16(kResult, 1);
                    else if (mode == 6) w.set16(kResult, 3);
                }
                drawLoad();
                co_await yieldFrame();
                co_await fade(false, false);
                co_return;
            }
        }
        (void)scrollButtons(0xb7, 0x6d, 0x69, kLoadScroll);
        drawLoad();
        co_await yieldFrame();
    }
}

// -------------------------------------------------------------- web build ---
#ifdef __EMSCRIPTEN__
void SaveLoadTask::webUpload(const std::string& name, const std::string& path, std::string& newFile) {
    const WebText& t = ctx_.version == GameVersion::Tw ? kWebTw : kWebJp;
    std::string base = uploadName(name);
    if (base.empty()) base = defaultName();
    std::vector<std::uint8_t> bytes;
    if (!path.empty()) {
        std::ifstream in(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(in), {});
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    const auto notice = [&](const std::string& shown, const char* what, bool ok) {
        notices_.push_back({shown + "：" + tr(what), ok});
        SDL_Log("[save] upload %s: %s", shown.c_str(), what);
    };
    switch (logic::saveCheck(bytes)) {
        case logic::SaveProblem::None: break;
        case logic::SaveProblem::Size: notice(base, t.size, false); return;
        case logic::SaveProblem::Format: notice(base, t.format, false); return;
        case logic::SaveProblem::Checksum: notice(base, t.checksum, false); return;
        case logic::SaveProblem::Contents: notice(base, t.contents, false); return;
    }
    // Never replaces a save: a different one with the same name gets a number.
    std::string stem = base;
    for (int n = 2; legacy::vfs::saveFileExists(kFolder, stem + ".SAV"); ++n) {
        const auto old = legacy::vfs::readSaveFile(kFolder, stem + ".SAV");
        if (old && *old == bytes) {
            notice(stem, t.same, true);
            newFile = stem + ".SAV";
            return;
        }
        stem = base + " (" + std::to_string(n) + ")";
    }
    if (legacy::vfs::listSaveFolder(kFolder).size() >= kMaxSaves) { notice(base, t.full, false); return; }
    legacy::vfs::writeSaveFile(kFolder, stem + ".SAV", bytes);
    notice(stem, t.added, true);
    newFile = stem + ".SAV";
}

bool SaveLoadTask::webInput(const WebButtons& b, int visible) {
    // On the release: the file dialog would swallow the mouse-up of a press.
    const auto clicked = [&](int x) {
        return ysub::hit(ctx_, x, b.y, kWebW, kWebH).over && (ctx_.mouse().released & legacy::kMouseLeft) != 0;
    };
    if (clicked(b.download) && selected_ != -1) {
        ctx_.audio->sePlay(kSe);
        const std::string& file = entries_[std::size_t(selected_)].file;
        if (const auto bytes = legacy::vfs::readSaveFile(kFolder, file))
            EM_ASM({ Module.chDownload(HEAPU8.slice($0, $0 + $1), UTF8ToString($2)); },
                   bytes->data(), int(bytes->size()), file.c_str());
    }
    if (clicked(b.upload)) {
        ctx_.audio->sePlay(kSe);
        EM_ASM({ Module.chPickSave(); });
    }
    if (webSaves().empty()) return false;

    std::string newFile;
    if (noticeFrames_ == 0) notices_.clear();
    for (const WebSave& s : webSaves()) webUpload(s.name, s.path, newFile);
    webSaves().clear();
    noticeFrames_ = kNoticeFrames;
    if (newFile.empty()) return false;
    scan(visible);
    top_ = 0;
    selected_ = -1;
    for (std::size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].file == newFile) selected_ = int(i);
    return true;
}

void SaveLoadTask::webDraw(const WebButtons& b) {
    const WebText& t = ctx_.version == GameVersion::Tw ? kWebTw : kWebJp;
    const auto draw = [&](int x, const std::string& label, bool enabled) {
        const bool lit = enabled && webLive_ && ysub::hit(ctx_, x, b.y, kWebW, kWebH).over;
        fill(ctx_, x, b.y, kWebW, kWebH, 0x5b, lit ? 0xe0405068u : 0xe0202838u);
        ysub::box(ctx_, x, b.y, kWebW, kWebH, 0x5a, 0xffa0a8b8u);
        ctx_.draw.text({.x = x + kWebW / 2, .y = b.y + kWebH / 2, .priority = 0x59, .argb = 0xFFFFFFFFu,
                        .text = label, .fontPx = std::uint16_t(ctx_.version == GameVersion::Tw ? 12 : 10), .centred = true});
        if (!enabled || !webLive_) fill(ctx_, x, b.y, kWebW + 1, kWebH + 1, 0x52, 0xa0010101u);
    };
    draw(b.download, tr(t.download), selected_ != -1);
    draw(b.upload, tr(t.upload), true);

    if (!webLive_) noticeFrames_ = 0;                                // not over a dialog
    if (noticeFrames_ == 0) return;
    --noticeFrames_;
    constexpr std::size_t kShown = 6;
    const std::size_t first = notices_.size() > kShown ? notices_.size() - kShown : 0;
    const int h = int(notices_.size() - first) * 0x12 + 0xc, y0 = 0x1a0;
    fill(ctx_, 0xad, y0, 0x298, h, 0x53, 0xf0101820u);
    ysub::box(ctx_, 0xad, y0, 0x298, h, 0x52, 0xffa0a8b8u);
    for (std::size_t i = first; i < notices_.size(); ++i)
        ctx_.draw.text({.x = 0xad + 0x298 / 2, .y = y0 + 0xf + int(i - first) * 0x12, .priority = 0x52,
                        .argb = notices_[i].ok ? 0xFFA0F0A0u : 0xFFFF9090u, .text = notices_[i].text,
                        .fontPx = 12, .centred = true});
}
#else
bool SaveLoadTask::webInput(const WebButtons&, int) { return false; }
void SaveLoadTask::webDraw(const WebButtons&) {}
void SaveLoadTask::webUpload(const std::string&, const std::string&, std::string&) {}
#endif

std::string SaveLoadTask::defaultName() const {
    const Text& t = texts(ctx_.version);
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &now);
#else
    localtime_r(&now, &lt);
#endif
    char d[32];
    std::snprintf(d, sizeof d, "%04d%02d%02d%02d%02d%02d", lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
                  lt.tm_hour, lt.tm_min, lt.tm_sec);
    return t.prefix + d;
}

// ------------------------------------------------------------ save screen ---
// States follow FUN_004d8620: list, name entry (IME), overwrite / delete
// confirmations, the bad-name dialog; saving returns to the list.
Co<> SaveLoadTask::saveScreen() {
    GameWork& w = ctx_.work;
    const Text& t = texts(ctx_.version);
    name_ = defaultName();
    nameLine_.reset(1);
    nameLine_.addUtf8(name_);
    selected_ = -1;
    co_await fade(true, true);
    for (;;) {                                                       // (re)list
        for (int i = 0; i < 0x20; ++i) { messageBox(1); drawSave(); co_await yieldFrame(); }
        scan(0x16);
        selected_ = -1;
        top_ = 0;
        enum class S { List, Exists, Overwrite, Delete, Write, Remove, Edit, Error, Done } st = S::List;
        while (st != S::Write && st != S::Remove && st != S::Done) {
            const auto dims = [&] {
                fill(ctx_, 0xe3, 0xf3, 0x8d, 0x1f, 0x52, 0xa0010101u);
                if (selected_ != -1) fill(ctx_, 0x1bc, 0xf3, 0x8d, 0x1f, 0x52, 0xa0010101u);
                fill(ctx_, 0x295, 0xf3, 0x8d, 0x1f, 0x52, 0xa0010101u);
                fill(ctx_, 0xaa, 300, 0x2b7, 0x13d, 0x55, 0xc0101010u);
            };
            const auto dialog = [&](std::uint32_t va, S yes) {          // yes / no at (0x179, 0x192)
                drawRect(ctx_, rect(va), 0x54, 0x179, 0x192);
                if (button(0x179 + 0x1c, 0x192 + 0x34, 0x66, 0x1d)) { ctx_.audio->sePlay(kSe); st = yes; }
                if (button(0x179 + 0x8c, 0x192 + 0x34, 0x66, 0x1d)) { ctx_.audio->sePlay(1); st = S::List; }
                dims();
            };
            webLive_ = st == S::List;
            switch (st) {
                case S::List: {
                    if (webInput({kWebSaveX[0], kWebSaveX[1], kWebSaveY}, 0x16) && selected_ != -1) {
                        const std::string& f = entries_[std::size_t(selected_)].file;
                        name_ = f.substr(0, f.size() - 4);
                        nameLine_.reset(1);
                        nameLine_.addUtf8(name_);
                    }
                    for (int r = 0; r < 0x16 && r + top_ <= int(entries_.size()) - 1; ++r) {
                        const ysub::Hit h = ysub::hit(ctx_, 0xb0, r * 0xe + 0x132, 0x294, 0xd);
                        if (h.over) {
                            fill(ctx_, 0xad, r * 0xe + 0x132, 0x298, 0xd, 0x58, 0x8020a000u, kBlendAdd);
                            if (h.pressed & legacy::kMouseLeft) {
                                selected_ = r + top_;
                                ctx_.audio->sePlay(kSe);
                                const std::string& f = entries_[std::size_t(selected_)].file;
                                name_ = f.substr(0, f.size() - 4);
                                nameLine_.reset(1);
                                nameLine_.addUtf8(name_);
                            }
                        }
                        if (selected_ == r + top_) fill(ctx_, 0xad, r * 0xe + 0x132, 0x298, 0xd, 0x57, 0xa0c0a000u, kBlendAdd);
                    }
                    if (button(0xaa, 0xb7, 0x2b5, 0x12)) {            // edit the name
                        ctx_.audio->sePlay(kSe);
                        nameLine_.reset(1);
                        selected_ = -1;
                        ctx_.engine.input().setTextInput(true);
                        st = S::Edit;
                        break;
                    }
                    if (button(0xe3, 0xf3, 0x8d, 0x1f)) {              // save
                        ctx_.audio->sePlay(kSe);
                        flashes_.add(rect(kSaveBtn), 0x5a, 0xd9, 0xf1, 8);
                        st = S::Exists;
                    }
                    if (button(0x1bc, 0xf3, 0x8d, 0x1f) && selected_ != -1) {   // delete
                        flashes_.add(rect(kDeleteBtn), 0x5a, 0x1b2, 0xf1, 8);
                        ctx_.audio->sePlay(kSe);
                        st = S::Delete;
                    }
                    if (button(0x295, 0xf3, 0x8d, 0x1f)) {             // back
                        ctx_.audio->sePlay(1);
                        flashes_.add(rect(kSaveCancel), 0x5a, 0x28b, 0xf1, 8);
                        w.set16(kResult, 0);
                        st = S::Done;
                        break;
                    }
                    (void)scrollButtons(0x12e, 0x4f, 0x4b, kSaveScroll);
                    break;
                }
                case S::Exists:
                    st = legacy::vfs::saveFileExists(kFolder, name_ + ".SAV") ? S::Overwrite : S::Write;
                    break;
                case S::Overwrite: dialog(kOverwrite, S::Write); break;
                case S::Delete: dialog(kDeleteAsk, S::Remove); break;
                case S::Edit: {
                    fill(ctx_, 0xaa, 0xb7, 0x2b5, 0x12, 0x5b, 0x3820a000u, kBlendAdd);
                    const auto& in = ctx_.engine.input().text();
                    name_ += in.typed;
                    for (int k = 0; k < in.backspaces && !name_.empty(); ++k) {
                        std::size_t cut = name_.size() - 1;                 // drop one UTF-8 character
                        while (cut > 0 && (static_cast<unsigned char>(name_[cut]) & 0xc0) == 0x80) --cut;
                        name_.erase(cut);
                    }
                    if (name_.size() > 0x40) name_.resize(0x40);
                    if (in.enter) {
                        ctx_.engine.input().setTextInput(false);
                        nameLine_.reset(1);
                        if (!validName(name_)) {
                            message_.reset(5);
                            message_.addUtf8(t.badChars);
                            message_.addUtf8(t.badName);
                            shownMessage_ = -1;
                            ctx_.audio->sePlay(1);
                            st = S::Error;
                        } else {
                            nameLine_.addUtf8(name_);
                            ctx_.audio->sePlay(kSe);
                            st = S::List;
                        }
                        break;
                    }
                    if (button(0x295, 0xf3, 0x8d, 0x1f)) {
                        ctx_.engine.input().setTextInput(false);
                        ctx_.audio->sePlay(1);
                        flashes_.add(rect(kSaveCancel), 0x5a, 0x28b, 0xf1, 8);
                        w.set16(kResult, 0);
                        st = S::Done;
                        break;
                    }
                    messageBox(2);
                    ctx_.draw.text({.x = 0xb0, .y = 0xba, .priority = 0x59, .argb = 0xFFFFFFFFu,
                                    .text = name_ + "_", .fontPx = 12});
                    fill(ctx_, 0xe3, 0xf3, 0x8d, 0x1f, 0x52, 0xa0010101u);
                    break;
                }
                case S::Error:
                    shownMessage_ = -1;
                    drawRect(ctx_, rect(kError), 0x54, 0x179, 0x192);
                    if (button(0x179 + 0x1c, 0x192 + 0x34, 0xd7, 0x1d)) { ctx_.audio->sePlay(kSe); st = S::List; }
                    message_.draw(ctx_, 0x179 + 5, 0x192 + 10, 0x53);
                    dims();
                    break;
                default: break;
            }
            drawSave();
            co_await yieldFrame();
        }
        webLive_ = false;
        if (st == S::Done) { co_await fade(false, true); co_return; }
        if (st == S::Write) {                                        // FUN_004d7720
            const std::vector<std::uint8_t> bytes = logic::saveGame(w);
            legacy::vfs::writeSaveFile(kFolder, name_ + ".SAV", bytes);
            SDL_Log("[save] %s.SAV", name_.c_str());
        } else {
            legacy::vfs::removeSaveFile(kFolder, entries_[std::size_t(selected_)].file);   // FUN_0042afc0
        }
        shownMessage_ = -1;
    }
}

} // namespace ch
