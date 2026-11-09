// ============================================================================
// net_task.cpp — NetTask: the network game's screens on the relay
// ============================================================================
#include "net_task.hpp"
#include "../saveload/saveload_task.hpp"
#include "../state/exe_image.hpp"
#include "../state/game_logic.hpp"
#include "../ysub/chat_box.hpp"
#include "../ysub/ysub.hpp"

#include <algorithm>

namespace ch {

using legacy::kMouseLeft;

namespace {
using R = NetTask::Rect;
// The screens' tables (x, y, w, h): the buttons and their pressed images.
constexpr R kNameButtons[2] = {{407, 401, 96, 25}, {520, 401, 95, 25}};                    // DAT_00605a48
constexpr R kNameBox = {0, 0, 270, 107}, kNameOk = {0, 108, 136, 30}, kNameCancel = {136, 108, 134, 30};   // 0x605a58..
constexpr R kMenuButtons[3] = {{384, 349, 255, 28}, {384, 382, 255, 28}, {384, 415, 255, 28}};   // DAT_00605a70
constexpr R kMenuBox = {0, 0, 293, 142};                                                     // DAT_00605a88
constexpr R kMenuRows[3] = {{0, 142, 293, 35}, {0, 177, 293, 33}, {0, 210, 293, 44}};
constexpr int kMenuRowY[3] = {0x20, 0x43, 100};
constexpr R kListButtons[15] = {{403, 432, 85, 25}, {516, 432, 95, 25}, {400, 471, 221, 27}, {400, 513, 221, 27},
                                {613, 289, 21, 31}, {613, 323, 21, 31}, {613, 357, 21, 31}, {613, 391, 21, 31},
                                {389, 290, 222, 19}, {389, 309, 222, 19}, {389, 328, 222, 19}, {389, 347, 222, 19},
                                {389, 366, 222, 19}, {389, 385, 222, 19}, {389, 404, 222, 19}};   // DAT_00605aa8
constexpr R kListBox = {0, 0, 260, 338};                                                     // DAT_00605b70
constexpr R kListPressed[8] = {{0, 338, 127, 30}, {127, 338, 133, 30}, {0, 368, 260, 39}, {0, 407, 260, 37},
                               {1, 445, 21, 31}, {25, 445, 21, 31}, {49, 445, 21, 31}, {73, 445, 21, 31}};
constexpr int kListPressedAt[8][2] = {{0, 0xd8}, {0x7f, 0xd8}, {0, 0xff}, {0, 0x126},
                                      {0xe6, 0x49}, {0xe6, 0x6b}, {0xe6, 0x8d}, {0xe6, 0xaf}};
// The room (bank 0x5a frame 5).
constexpr R kRoomButtons[6] = {{563, 725, 452, 35}, {297, 729, 237, 35}, {6, 732, 251, 29},
                               {563, 685, 237, 35}, {5, 74, 213, 34}, {5, 109, 214, 35}};   // DAT_00605d18
constexpr R kRoomPressed[6] = {{1, 946, 452, 35}, {1, 872, 237, 35}, {1, 769, 250, 29},
                               {1, 909, 237, 35}, {1, 800, 213, 34}, {1, 835, 213, 35}};   // DAT_00605d48
constexpr R kRoomBack = {0, 0, 1024, 768};                                                  // DAT_006065d8
constexpr R kMapScroll[8] = {{762, 420, 21, 31}, {762, 454, 21, 31}, {762, 488, 21, 31}, {762, 522, 21, 31},
                             {986, 420, 21, 31}, {986, 454, 21, 31}, {986, 488, 21, 31}, {986, 522, 21, 31}};   // DAT_00605d78
constexpr R kMapRows[2][7] = {{{571, 425, 185, 18}, {571, 443, 185, 18}, {571, 461, 185, 18}, {571, 479, 185, 18},
                               {571, 497, 185, 18}, {571, 515, 185, 18}, {571, 533, 185, 18}},
                              {{795, 425, 185, 18}, {795, 443, 185, 18}, {795, 461, 185, 18}, {795, 479, 185, 18},
                               {795, 497, 185, 18}, {795, 515, 185, 18}, {795, 533, 185, 18}}};   // DAT_00605db8 / 0x605e08
constexpr R kSideTabs[4] = {{562, 73, 120, 25}, {676, 73, 110, 25}, {789, 73, 110, 25}, {902, 73, 111, 25}};   // DAT_00605e78 + 4
constexpr R kSideTabOn[4] = {{253, 769, 114, 27}, {367, 769, 113, 27}, {480, 769, 113, 27}, {593, 769, 114, 27}};   // DAT_00605ef8
constexpr R kUnitScroll[4] = {{511, 74, 22, 88}, {511, 164, 22, 88}, {511, 254, 21, 88}, {511, 344, 22, 88}};   // DAT_00606498
constexpr R kFace = {1, 1, 48, 48};                                                          // DAT_00606578
constexpr R kJobs[5] = {{482, 849, 18, 18}, {502, 849, 18, 18}, {522, 849, 18, 18}, {542, 849, 18, 18}, {562, 849, 18, 18}};
constexpr R kReadyMarks[4] = {{593, 827, 20, 21}, {615, 827, 20, 21}, {637, 827, 20, 21}, {659, 827, 20, 21}};   // DAT_006065b8
constexpr R kSideMarks[5] = {{483, 827, 20, 21}, {505, 827, 20, 21}, {527, 827, 20, 21}, {549, 827, 20, 21},
                             {571, 827, 20, 21}};                                           // DAT_00606680
// DAT_006065e0: each player's row of team cells (1..4, ?), 5 a row.
constexpr R kTeamCells[20] = {{730, 586, 20, 21}, {753, 586, 20, 21}, {776, 586, 20, 21}, {799, 586, 20, 21}, {822, 586, 20, 21},
                              {730, 610, 20, 21}, {753, 610, 20, 21}, {776, 610, 20, 21}, {799, 610, 20, 21}, {822, 610, 20, 21},
                              {730, 634, 20, 21}, {753, 634, 20, 21}, {776, 634, 20, 21}, {799, 634, 20, 21}, {822, 634, 20, 21},
                              {730, 658, 20, 21}, {753, 658, 20, 21}, {776, 658, 20, 21}, {799, 658, 20, 21}, {822, 658, 20, 21}};
constexpr R kChatButtons[2] = {{306, 445, 114, 29}, {420, 445, 114, 29}};                // DAT_00605cf8
constexpr R kChatOn[2] = {{253, 827, 114, 29}, {367, 827, 114, 29}};                      // DAT_00605d08
constexpr R kMapScrollOn[8] = {{709, 769, 21, 31}, {733, 769, 21, 31}, {757, 769, 21, 31}, {781, 769, 21, 31},
                               {709, 769, 21, 31}, {733, 769, 21, 31}, {757, 769, 21, 31}, {781, 769, 21, 31}};   // DAT_00605e58
constexpr int kVisibleMaps = 7;
constexpr int kInfoRows = 12;                // the description's rows
constexpr R kInfoArrows[4] = {{993, 105, 22, 52}, {993, 159, 22, 52}, {993, 213, 22, 52}, {993, 267, 22, 52}};   // DAT_00605e78

// Not the original's: the server the rooms are on (the relay's place).
// The configured Apinator regions, then "Custom" (a relay of one's own,
// tools/netserver/relay.mjs, or any ws(s):// / apinator: URL). The choice
// is kept in the save folder (net.cfg); CH_RELAY overrides it.
constexpr const char* kServerFile = "net.cfg";
constexpr const char* kDefaultRelay = "ws://127.0.0.1:8787/";
constexpr R kServerRow = {366, 466, 293, 24};
constexpr R kServerUrl = {366, 494, 293, 24};

bool builtIn(const std::string& url) {
    for (const auto& c : net::builtInServers()) if (c.url == url) return true;
    return false;
}
std::string serverLabel(const std::string& url) {
    for (const auto& c : net::builtInServers()) if (c.url == url) return c.label;
    return "Custom";
}
const std::string& serverUrl(net::Session& s) {
    if (s.server.empty()) {
        if (const char* u = SDL_getenv("CH_RELAY")) s.server = u;
        else if (const auto saved = legacy::vfs::readSave(kServerFile); saved && !saved->empty())
            s.server.assign(saved->begin(), saved->end());
        else if (const auto list = net::builtInServers(); !list.empty()) s.server = list.front().url;
        else s.server = kDefaultRelay;
        while (!s.server.empty() && (s.server.back() == '\n' || s.server.back() == '\r')) s.server.pop_back();
    }
    return s.server;
}
// "12 online · 3 rooms" from the server's live counts (Stats), or how the
// connection stands.
std::string serverCounts(const net::Session& s) {
    using St = net::Session::State;
    if (s.state() == St::Failed) return "offline";
    if (s.state() == St::Offline || s.state() == St::Connecting || s.playersOnline() < 0) return "connecting...";
    std::string t = std::to_string(s.playersOnline()) + " online";
    if (s.roomsOpen() >= 0) t += "  \u00b7  " + std::to_string(s.roomsOpen()) + (s.roomsOpen() == 1 ? " room" : " rooms");
    return t;
}

void saveServer(net::Session& s, std::string url) {
    s.server = std::move(url);
    legacy::vfs::writeSave(kServerFile, std::span(reinterpret_cast<const std::uint8_t*>(s.server.data()), s.server.size()));
    // The next room list connects to it.
    if (s.state() != net::Session::State::InRoom && s.state() != net::Session::State::InGame) s.disconnect();
}
} // namespace

// FUN_004442f0: the sounds, the two banks, the IME box. (The IME box's
// text, FUN_00426160 / FUN_00426500 / FUN_00426d90, is the engine's text
// input, typing(); FUN_00441270 / FUN_00441540 make the message box's font
// and sprites, here the draw list's text and bank 2; FUN_00428120 is a list
// object's constructor, std::vector's here; FUN_004109e0 ends a screen
// fade, fade() / fadeRoom(). The chat log box's D3DX font, FUN_00425620 ->
// FUN_004096d0 (FUN_00409440: is it made; FUN_004094c0 its colour flag), is
// the draw list's text; the IME box (made by FUN_00425780 / FUN_00425800,
// released by FUN_004257c0 -> FUN_004258c0 / FUN_00425c10) is the engine's
// text input, setTextInput(false) here.)
NetTask::~NetTask() {
    ctx_.engine.input().setTextInput(false);
    ctx_.audio->seStopAll();
    ctx_.sprites->unloadBank(kBank);
    ctx_.sprites->unloadBank(kFaces);
    ctx_.sprites->unloadBank(kMesBank);              // FUN_00441360 -> FUN_004413f0
}

// ------------------------------------------------------------- drawing ---
// FUN_0044cb40 / FUN_0044cba0 (and netmessage's FUN_00441ab0): one sub-rect
// of a bank's frame at a point, its sprite's colour as set before.
void NetTask::sub(int frame, const Rect& r, int x, int y, int prio, std::uint8_t alpha, std::uint8_t rgb) {
    ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kBank), .frame = static_cast<std::uint16_t>(frame), .sx = r.x,
                         .sy = r.y, .w = r.w, .h = r.h, .alpha = alpha, .rgb = rgb},
                  prio, x, y);
}

void NetTask::text(int x, int y, const std::string& s, int prio, std::uint32_t argb, int px) {
    if (!s.empty()) ctx_.draw.text({.x = x, .y = y, .priority = static_cast<std::uint16_t>(prio), .argb = argb, .text = s, .fontPx = static_cast<std::uint16_t>(px)});
}

// FUN_0044ca80 (the pointer, frame 0) and FUN_0044c9a0 (the backdrop,
// frame 7, and the dark strip down its right edge).
void NetTask::backdrop() {
    sub(0, {0, 0, 0, 0}, ctx_.mouse().x, ctx_.mouse().y, 1);
    ctx_.draw.fill({.x = 0x3f7, .y = 0, .w = 9, .h = 0x300, .priority = 100, .argb = 0xa0000000u});
    sub(7, {0, 0, 0, 0}, 0, 0, 100);
    drawStatus();
}

// FUN_00441630 / FUN_004418f0 (mesbox.cpp): message `set` of DAT_005ffa90
// (kind 0 an OK box; 2..8 lines) centred on (0x200, 0x180), from
// data\Tactics\MesBox.bin (bank 2, FUN_00441470): the box (DAT_005ffc50),
// its lines at +0x18 a line 0x12 apart (FUN_00409810), the OK button lit
// under the pointer (DAT_005ffc38). Clicking OK (DAT_005ffc20) closes it.
void NetTask::message(int set) {
    mesSet_ = set;
}

bool NetTask::messageOpen() {
    if (mesSet_ < 0) return false;
    const GameVersion v = ctx_.version;
    const int x = 0x200 - 200, y = 0x180 - 0x55;
    // DAT_005ffa90 (JP exe 0x4e5d48): 8-byte records, +1 the line count, +4 the line pointers.
    const exe::Image& img = exe::own(v);
    const std::uint32_t rec = (v == GameVersion::Jp ? 0x4e5d48u : 0x5ffa90u) + std::uint32_t(mesSet_) * 8;
    const int lines = img.u8(rec + 1);
    const std::uint32_t ptrs = img.u32(rec + 4);
    const auto& m = ctx_.mouse();
    const bool onOk = m.x > x + 93 && m.x < x + 93 + 214 && m.y > y + 132 && m.y < y + 132 + 28;
    ysub::drawSub(ctx_, {.bank = kMesBank, .frame = 0, .sx = 0, .sy = 0, .w = 401, .h = 171}, 3, x, y);
    if (onOk) ysub::drawSub(ctx_, {.bank = kMesBank, .frame = 0, .sx = 0, .sy = 171, .w = 400, .h = 29}, 2, x, y + 0x84);
    for (int i = 0; i < lines; ++i) {
        const std::uint32_t p = img.u32(ptrs + std::uint32_t(i) * 4);
        const std::string s = p ? exe::text(v, p) : std::string();
        text(x + 0x18, y + 0x18 + i * 0x12, s, 2, 0xffffffff, 16);
    }
    if (onOk && (m.pressed & kMouseLeft)) {
        ctx_.audio->sePlay(5);
        mesSet_ = -1;
    }
    return true;
}

void NetTask::drawStatus() {
    if (statusTime_ > 0 && SDL_getenv("CH_NET_TRACE") && statusShown_ != status_) SDL_Log("[net] note: %s", (statusShown_ = status_).c_str());
    if (statusTime_ > 0) {
        --statusTime_;
        ctx_.draw.fill({.x = 0x140, .y = 0x150, .w = 0x180, .h = 0x40, .priority = 2, .argb = 0xc0000000u});
        ctx_.draw.text({.x = 0x200, .y = 0x170, .priority = 1, .argb = 0xffffffff, .text = status_, .fontPx = 16, .centred = true});
    }
}

// FUN_0044cc80 -> FUN_0044cce0: `digits` places of `value` (0..999999) in
// the room sheet's 8 x 12 digits (frame 5, DAT_006066a8), 8 apart, the
// leading zeros left out (the last place always drawn).
void NetTask::number(int x, int y, int prio, int value, int digits) {
    static constexpr int kDigitX[10] = {554, 482, 490, 498, 506, 514, 522, 530, 538, 546};
    value = std::clamp(value, 0, 999999);
    int d[6];
    for (int i = 5, v = value; i >= 0; --i, v /= 10) d[i] = v % 10;
    bool lead = false;
    for (int i = 0; i < digits; ++i) {
        const int place = 6 - digits + i;
        lead = lead || d[place] != 0;
        if (lead || place == 5) sub(5, {kDigitX[d[place]], 869, 8, 12}, x + 8 * i, y, prio);
    }
}

// FUN_0044c710: which of the rectangles holds the pointer, -1 none.
int NetTask::hit(const Rect* rects, int n) const {
    const auto& m = ctx_.mouse();
    for (int i = 0; i < n; ++i)
        if (rects[i].x < m.x && m.x < rects[i].x + rects[i].w && rects[i].y < m.y && m.y < rects[i].y + rects[i].h) return i;
    return -1;
}

// The IME box's text (FUN_00425a10 ..): here whatever the system's input
// method types, as UTF-8, `most` characters at most (not bytes, so every
// language gets the same room).
void NetTask::typing(std::string& line, std::size_t most) {
    const auto& t = ctx_.engine.input().text();
    for (int i = 0; i < t.backspaces && !line.empty(); ++i) net::popUtf8(line);
    net::appendUtf8(line, t.typed, most);
}

// --------------------------------------------------------------- flow ---
// FUN_00444330
Co<> NetTask::start(std::uint32_t param) {
    // FUN_00444620: the two banks; FUN_00444590 the first map.
    ctx_.sprites->loadBank(kBank, ctx_.readData("data\\Tactics\\NetData.bin"));
    ctx_.sprites->loadBank(kFaces, ctx_.readData("data\\Tactics\\NetUnitFace.bin"));
    ctx_.sprites->loadBank(kMesBank, ctx_.readData("data\\Tactics\\MesBox.bin"));   // FUN_004415e0 -> FUN_00441470
    // FUN_0044d040: the search's sound (SE group 0x13).
    static constexpr std::string_view kWait[] = {"data\\Sound\\plw\\NetSnd\\wait.WAV"};
    ctx_.audio->seLoadGroup(0x13, kWait);
    net::Session& s = ctx_.net;
    int state = param != 0 && s.state() == net::Session::State::InRoom ? 10 : 0;
    if (param != 0 && state == 0) state = 6;       // back here without a room: the menu
    for (;;) {
        switch (state) {
            case 0:
                co_await fade(true);
                state = 5;
                break;
            case 5: state = co_await nameScreen(); break;
            case 6: state = co_await menuScreen(); break;
            case 7: state = co_await listScreen(false); break;
            case 8: state = co_await listScreen(true); break;
            case 10: state = co_await roomScreen(); break;
            case 11:
                // FUN_00448650 -> FUN_00448a30: the battle work, then task 0x10.
                if (!netgame::writeBattleWork(ctx_, s)) {
                    status_ = "The room's settings cannot be played.";
                    statusTime_ = 180;
                    s.leaveRoom();
                    state = 6;
                    break;
                }
                ctx_.scenes.request(SceneId::Tactics);
                co_return;
            case 2:
                co_await fade(false);
                s.disconnect();
                ctx_.scenes.request(SceneId::Title);   // FUN_00439e30(1)
                co_return;
            default: state = 6; break;
        }
    }
}

// FUN_00444820 / FUN_00444960: the screen from or to black in 60 frames.
Co<> NetTask::fade(bool in) {
    for (int i = 0; i <= 0x3c; ++i) {
        const int a = in ? 0xff - i * 0xff / 0x3c : i * 0xff / 0x3c;
        ctx_.draw.fill({.x = 0, .y = 0, .w = 0x403, .h = 0x300, .priority = 2, .argb = std::uint32_t(a) << 24});
        backdrop();
        co_await yieldFrame();
    }
}

// FUN_00444a90: the name (DAT_00809694, kept between visits).
Co<int> NetTask::nameScreen() {
    net::Session& s = ctx_.net;
    std::string name = s.playerName.empty() ? (SDL_getenv("CH_NET_NAME") ? SDL_getenv("CH_NET_NAME") : "Player") : s.playerName;
    ctx_.engine.input().setTextInput(true);
    for (;;) {
        typing(name, 15);
        const int h = hit(kNameButtons, 2);
        const bool click = ctx_.mouse().pressed & kMouseLeft;
        const bool enter = ctx_.engine.input().text().enter;
        if ((click && h == 0) || enter) {
            if (!name.empty()) {
                s.playerName = name;
                ctx_.audio->sePlay(5);
                ctx_.engine.input().setTextInput(false);
                co_await yieldFrame();             // the click is not the menu's too (its third button lies under this one)
                co_return 6;
            }
        } else if (click && h == 1) {
            ctx_.audio->sePlay(2);
            ctx_.engine.input().setTextInput(false);
            co_return 2;
        }
        // FUN_00444d40: the box at (0x179, 0x14b), the pressed button over it.
        sub(1, kNameBox, 0x179, 0x14b, 0x5a);
        if ((ctx_.mouse().held & kMouseLeft) && h == 0) sub(1, kNameOk, 0x179, 0x14b + 0x45, 0x55);
        if ((ctx_.mouse().held & kMouseLeft) && h == 1) sub(1, kNameCancel, 0x179 + 0x88, 0x14b + 0x45, 0x55);
        text(0x1bd, 0x171, name + ((SDL_GetTicks() / 400) % 2 ? "_" : ""), 0x54, 0xffffffff, 16);   // FUN_00426820
        backdrop();
        co_await yieldFrame();
    }
}

// FUN_00444e60: 0 the room list, 1 a room by its invite code (the
// original's two connection kinds), 2 back to the title.
Co<int> NetTask::menuScreen() {
    net::Session& s = ctx_.net;
    const int x = 0x200 - kMenuBox.w / 2, y = 0x180 - kMenuBox.h / 2;
    std::string custom = builtIn(serverUrl(s)) ? std::string(kDefaultRelay) : serverUrl(s);
    bool editing = false;
    // Connected already here, for the server's live counts (the room list
    // goes on with the same connection); a failed one is tried again every
    // 10 s, or at once for another server.
    std::uint32_t lastTry = 0;
    const auto stopEditing = [&] {
        if (!editing) return;
        editing = false;
        ctx_.engine.input().setTextInput(false);
        if (custom.empty()) custom = kDefaultRelay;
        saveServer(s, custom);
    };
    for (;;) {
        const int h = hit(kMenuButtons, 3);
        const bool click = ctx_.mouse().pressed & kMouseLeft;
        if (h != -1 && click) {
            stopEditing();
            ctx_.audio->sePlay(h == 2 ? 2 : 5);
            co_return h == 0 ? 7 : h == 1 ? 8 : 2;
        }
        using St = net::Session::State;
        if (!editing && (s.state() == St::Offline || (s.state() == St::Failed && SDL_GetTicks() - lastTry > 10000))) {
            lastTry = SDL_GetTicks();
            s.connect(serverUrl(s), s.playerName);
        }
        // The server: a click steps through the regions and "Custom"; the
        // custom URL is typed in its own box (Enter or a click elsewhere ends).
        const bool isCustom = !builtIn(serverUrl(s));
        const int onRow = hit(&kServerRow, 1), onUrl = hit(&kServerUrl, 1);
        if (editing) {
            if (ctx_.engine.input().text().selectAll) custom.clear();   // Ctrl+A: type over it
            typing(custom, 200);
            if (ctx_.engine.input().text().enter || (click && onUrl == -1)) stopEditing();
        } else if (click && onRow == 0) {
            ctx_.audio->sePlay(5);
            const auto list = net::builtInServers();
            std::size_t at = list.size();   // "Custom"
            for (std::size_t i = 0; i < list.size(); ++i) if (list[i].url == serverUrl(s)) at = i;
            saveServer(s, at + 1 < list.size() ? list[at + 1].url : at + 1 == list.size() ? custom : list.empty() ? custom : list.front().url);
        } else if (click && onUrl == 0 && isCustom) {
            ctx_.audio->sePlay(5);
            editing = true;
            if (custom == kDefaultRelay) custom.clear();   // left empty, it comes back
            ctx_.engine.input().setTextInput(true);
        }
        ctx_.draw.fill({.x = kServerRow.x, .y = kServerRow.y, .w = kServerRow.w, .h = kServerRow.h, .priority = 0x5a,
                        .argb = onRow == 0 ? 0xc0303850u : 0xc0000000u});
        text(kServerRow.x + 8, kServerRow.y + 4, "Server:", 0x54, 0xffa0a0a0u);
        text(kServerRow.x + 80, kServerRow.y + 4, serverLabel(serverUrl(s)) + "   >", 0x54);
        if (!editing) text(kServerRow.x + 190, kServerRow.y + 5, serverCounts(s), 0x54, 0xffa0d0a0u, 12);
        if (!builtIn(serverUrl(s)) || editing) {
            ctx_.draw.fill({.x = kServerUrl.x, .y = kServerUrl.y, .w = kServerUrl.w, .h = kServerUrl.h, .priority = 0x5a,
                            .argb = editing ? 0xe0202030u : 0xc0000000u});
            // The end of a long URL is what is being typed.
            std::string shown = editing ? custom + ((SDL_GetTicks() / 400) % 2 ? "_" : "") : custom;
            while (net::utf8Length(shown) > 34) {
                std::size_t n = 1;
                while (n < shown.size() && (std::uint8_t(shown[n]) & 0xc0) == 0x80) ++n;
                shown.erase(0, n);
            }
            text(kServerUrl.x + 8, kServerUrl.y + 4, shown, 0x54, editing ? 0xffffffffu : 0xffc0c0c0u, 12);
        }
        sub(2, kMenuBox, x, y, 0x5a);   // FUN_00445000
        if (h != -1 && (ctx_.mouse().held & kMouseLeft))
            sub(2, kMenuRows[h], x, y + kMenuRowY[h], 0x55);
        backdrop();
        co_await yieldFrame();
    }
}

// FUN_00445150 / FUN_00446780: the relay's rooms ("Now Search..."
// FUN_00445920 asks for the list), up to 7 rows; join the one picked, make
// one (FUN_00445800 -> FUN_0044c8b0, then the room), or back. With byCode
// the rows give way to an invite code typed in.
Co<int> NetTask::listScreen(bool byCode) {
    net::Session& s = ctx_.net;
    using St = net::Session::State;
    if (s.state() == St::Offline || s.state() == St::Failed) s.connect(serverUrl(s), s.playerName);
    std::string code;
    if (byCode) ctx_.engine.input().setTextInput(true);
    bool asked = false;
    rooms_.init(0, 7);                                  // FUN_00445eb0
    const int x = 0x200 - kListBox.w / 2, y = 0x180 - kListBox.h / 2;
    std::uint32_t errors = s.errorCount();
    bool failShown = false;
    for (;;) {
        if (s.state() == St::Lobby && !asked) {   // FUN_00445920: "Now Search..." and its sound
            asked = true;
            s.listRooms();
            ctx_.audio->sePlayIn(0x13, 0, -1);
        }
        if (s.state() == St::InRoom) {
            ctx_.engine.input().setTextInput(false);
            ctx_.audio->sePlay(5);
            co_return 10;
        }
        // The original's messages for what went wrong: 1 no connection, 4
        // joining refused (6 no such code, 7 another build of the game).
        if (s.state() == St::Failed && !failShown) {
            failShown = true;
            message(1);
        } else if (s.errorCount() != errors) {
            errors = s.errorCount();
            const std::string& e = s.lastError();
            message(e.find("build") != std::string::npos ? 7 : e.find("no such") != std::string::npos ? 6 : s.lastErrorCode() == 2 ? 4 : 3);
        }
        if (messageOpen()) {
            backdrop();
            co_await yieldFrame();
            continue;
        }
        if (byCode) typing(code, 8);
        const int h = hit(kListButtons, 15);
        const bool click = ctx_.mouse().pressed & kMouseLeft;
        const int rows = int(s.rooms().size());
        if (rows != rooms_.count()) rooms_.init(rows, 7);  // FUN_00445f00: a new list
        const auto join = [&] {                         // FUN_00445c10: the cursor's session
            if (byCode && !code.empty()) s.joinRoom(code);
            else if (!byCode && rooms_.index() < rows) s.joinRoom(s.rooms()[std::size_t(rooms_.index())].code);
        };
        if (ctx_.engine.input().text().enter) join();
        if (click && h != -1) {
            ctx_.audio->sePlay(h == 3 ? 2 : 5);
            switch (h) {
                case 0:                                         // search again
                    if (s.state() == St::Failed || s.state() == St::Offline) { s.connect(serverUrl(s), s.playerName); failShown = false; }
                    asked = false;
                    break;
                case 1: join(); break;
                case 2:                                         // make a room
                    if (s.state() == St::Lobby) s.createRoom(s.playerName + "'s room", 4, true);
                    break;
                case 3:
                    ctx_.engine.input().setTextInput(false);
                    co_return 6;
                case 4: rooms_.first(); break;
                case 5: rooms_.up(); break;
                case 6: rooms_.down(); break;
                case 7: rooms_.last(); break;
                default:
                    if (!byCode) rooms_.setRow(h - 8);
                    break;
            }
        }
        // FUN_00446340: the box, a pressed button over it, then the rows (FUN_004465c0).
        sub(3, kListBox, x, y, 0x5a);
        text(x + 8, y + kListBox.h + 6, serverLabel(serverUrl(s)) + ":  " + serverCounts(s), 0x54, 0xffa0d0a0u, 12);   // not the original's
        if (h >= 0 && h < 8 && (ctx_.mouse().held & kMouseLeft))
            sub(3, kListPressed[h],
                x + kListPressedAt[h][0], y + kListPressedAt[h][1], 0x55);
        if (s.state() == St::Connecting) {
            text(0x1be, 0x157, "Now Search...", 0x58);
        } else if (byCode) {
            text(392, 293, "Invite code:", 0x54, 0xffffffff, 14);
            text(392, 315, code + ((SDL_GetTicks() / 400) % 2 ? "_" : ""), 0x54, 0xffffffff, 18);
        } else {
            for (int i = 0; i < rooms_.disp() && rooms_.top() + i < rows; ++i) {
                const int at = rooms_.top() + i;
                const auto& r = s.rooms()[std::size_t(at)];
                if (at == rooms_.index()) ctx_.draw.fill({.x = 389, .y = 290 + 19 * i, .w = 222, .h = 19, .priority = 0x56, .argb = 0x60ffffffu});
                char line[96];
                std::snprintf(line, sizeof line, "%s  %d/%d%s", r.name.c_str(), r.players, r.max, r.inGame ? "  (playing)" : "");
                text(392, 292 + 19 * i, line, 0x54, r.inGame ? 0xff909090u : 0xffffffffu, 13);
            }
        }
        // FUN_0044d1d0: the game's version (FUN_004e2c30: 0x10002).
        char ver[32];
        std::snprintf(ver, sizeof ver, "VERSION %2d.%2d", 1, 2);
        text(0x374, 0x2e4, ver, 0x55, 0xff6060a0u, 12);
        backdrop();
        co_await yieldFrame();
    }
}

// ---------------------------------------------------------------- room ---
netgame::Options NetTask::options() const {
    netgame::Options o;
    o.map = localMap_;
    o.side = teams_;
    return o;
}

void NetTask::sendOptions() { ctx_.net.setOptions(netgame::encodeOptions(options())); }

int NetTask::pickedCount() const {
    int n = 0;
    for (const auto p : ctx_.net.picked) n += p != 0;
    return n;
}

// FUN_00442f40: the picked units, for the others' battles.
void NetTask::sendPick() {
    net::Session& s = ctx_.net;
    std::vector<netgame::Record> units;
    for (std::size_t i = 0; i < s.roster.size(); ++i)
        if (i < s.picked.size() && s.picked[i]) units.push_back(s.roster[i]);
    s.setPlayerData(netgame::encodePick(units));
}

// FUN_00447d00: the room.
Co<int> NetTask::roomScreen() {
    net::Session& s = ctx_.net;
    using St = net::Session::State;
    const GameVersion v = ctx_.version;
    // FUN_0044a960: the ready-made units until a save's party is loaded;
    // FUN_0044aaf0 sends what is picked.
    if (s.roster.empty()) {
        for (const auto& r : netgame::defaultRoster(v)) s.roster.push_back(r);
        s.rosterFromSave = false;
    }
    s.picked.resize(s.roster.size(), 0);
    netgame::Options shown;
    if (!netgame::decodeOptions(s.room().options, shown)) shown = {};
    localMap_ = shown.map;
    teams_ = shown.side;
    // FUN_00449fd0: each side's description from its first line.
    const auto resetInfo = [&] { infoTop_.fill(0); };
    // FUN_0044a640: the map's list and its cursor on it.
    const auto showMap = [&] {
        for (int l = 0; l < 2; ++l) {
            const std::vector<int> maps = netgame::mapList(l == 1, ctx_.work, v);
            const auto it = std::find(maps.begin(), maps.end(), localMap_);
            if (it == maps.end()) continue;
            mapType_ = l;
            mapList_[std::size_t(l)].select(int(it - maps.begin()));
        }
    };
    for (int l = 0; l < 2; ++l)                         // FUN_00449650
        mapList_[std::size_t(l)].init(int(netgame::mapList(l == 1, ctx_.work, v).size()), kVisibleMaps);
    showMap();
    resetInfo();
    if (s.isHost()) sendOptions();
    // At most the map's limit is picked (the first ones).
    const auto clampPicks = [&] {
        const int most = netgame::mapEntry(localMap_, v).maxUnits;
        int n = 0;
        for (auto& p : s.picked) if (p && ++n > most) p = 0;
        if (n == 0) for (std::size_t i = 0; i < s.picked.size() && int(i) < most; ++i) s.picked[i] = 1;
    };
    clampPicks();
    sendPick();
    ctx_.engine.input().setTextInput(true);
    bool ready = false;
    // FUN_0044adb0: our picks fit the map (at least one, at most its limit).
    const auto picksFit = [&] {
        const int n = pickedCount();
        return n >= 1 && n <= netgame::mapEntry(localMap_, v).maxUnits;
    };
    // FUN_00447ac0: the room fades in (and FUN_00447be0 out on leaving).
    fadeAlpha_ = 0xff;
    for (;;) {
        if (s.startedPending) {                       // FUN_00448650: the relay started it
            s.startedPending = false;
            ctx_.engine.input().setTextInput(false);
            co_await fadeRoom();                           // FUN_00447be0
            co_return 11;
        }
        if (s.state() == St::Failed || s.state() == St::Lobby || s.state() == St::Offline) {
            ctx_.engine.input().setTextInput(false);
            message(8);                                   // "the connection was cut"
            while (messageOpen()) {
                drawRoom(-1);
                backdrop();
                co_await yieldFrame();
            }
            co_return 6;
        }
        // The host's map may have changed (theirs or ours).
        netgame::Options o;
        // (FUN_00443780 case 5 / 7: the host's map and teams.) A ready
        // player whose picks no longer fit stops being ready (FUN_00442950).
        if (!s.isHost() && netgame::decodeOptions(s.room().options, o)) {
            teams_ = o.side;
            if (o.map != localMap_) {
                localMap_ = o.map;
                showMap();
                resetInfo();
                if (ready && !picksFit()) {
                    ready = false;
                    s.setReady(false);
                    status_ = "The map changed: pick your units again.";
                    statusTime_ = 120;
                }
            }
        }
        typing(chatLine_, 60);
        if (ctx_.engine.input().text().enter && !chatLine_.empty()) {   // FUN_00448490 -> FUN_004431a0
            // To our team only (+0x4e 1): marked for the others' filter (FUN_004432f0).
            s.chat(chatTeam_ ? std::string(1, '\x01') + chatLine_ : chatLine_);
            chatLine_.clear();
        }
        // FUN_004470c0's keys have no place here; Ctrl+A picks the whole
        // roster as far as the map allows (FUN_0044af30 -> FUN_0044ae70).
        if (ctx_.engine.input().text().selectAll) {
            const int most = netgame::mapEntry(localMap_, v).maxUnits;
            int n = 0;
            for (auto& p : s.picked) p = n < most ? (++n, 1) : 0;
            sendPick();
        }
        const bool click = ctx_.mouse().pressed & kMouseLeft;
        const int button = hit(kRoomButtons, 6);
        // FUN_00448fe0: 0 start (the host: everyone ready, each with units),
        // 1 leave, 2 ready (the others), 3 -, 4 the ready-made units, 5 a save's party.
        if (click && button != -1) {
            ctx_.audio->sePlay(5);
            switch (button) {
                case 0:
                    if (s.isHost()) {
                        // FUN_00448fe0 case 0: FUN_0041af20 all ready (the host alone
                        // counts: a room may start with one player), FUN_0044adb0 ours fit.
                        bool all = picksFit();
                        for (const auto& m : s.room().members)
                            all = all && (m.slot == s.room().host || m.ready) && !netgame::decodePick(m.data).empty();
                        if (all) s.startGame();
                        else { status_ = "Everyone must be ready and bring a unit."; statusTime_ = 120; }
                    }
                    break;
                case 1:
                    s.leaveRoom();
                    ctx_.engine.input().setTextInput(false);
                    co_await fadeRoom();                       // FUN_00447be0
                    co_return 6;
                case 2:                                        // FUN_00441de0 / FUN_004427d0
                    if (!s.isHost()) {
                        if (!ready && !picksFit()) { status_ = "Pick between 1 unit and the map's limit."; statusTime_ = 120; break; }
                        ready = !ready;
                        s.setReady(ready);
                        if (SDL_getenv("CH_NET_TRACE")) SDL_Log("[net] ready %d", int(ready));
                    }
                    break;
                case 4:                                        // FUN_0044a960 + FUN_0044aaf0
                    s.roster.clear();
                    for (const auto& r : netgame::defaultRoster(v)) s.roster.push_back(r);
                    s.picked.assign(s.roster.size(), 0);
                    s.rosterFromSave = false;
                    clampPicks();
                    sendPick();
                    break;
                case 5: {                                      // FUN_004d9a80 / FUN_004d9f30: the save screen over us
                    GameWork& w = ctx_.work;
                    w.set16(0x7e11a8, 6);
                    w.set16(0x7e11ac, 0);
                    w.set16(0x7e11ae, 1);
                    ctx_.engine.input().setTextInput(false);
                    ctx_.tasks.link(std::make_unique<SaveLoadTask>(ctx_), 0, 2);
                    while (w.s16(0x7e11ae) != 0) {             // FUN_004d9ac0
                        s.poll(SDL_GetTicks());
                        co_await yieldFrame();
                    }
                    if (w.s16(0x7e11ac) == 1) {                // FUN_004d9af0 -> FUN_0044a590 / FUN_0044a6e0
                        s.roster.clear();
                        for (const auto& r : netgame::saveRoster(w, v)) s.roster.push_back(r);
                        s.picked.assign(s.roster.size(), 0);
                        s.rosterFromSave = true;
                        clampPicks();
                        sendPick();
                    }
                    ctx_.engine.input().setTextInput(true);
                    break;
                }
                default: break;
            }
        }
        // FUN_0044af30: the roster, four to a row; FUN_0044aee0 flips a pick.
        const int rows = (int(s.roster.size()) + 3) / 4;
        if (click) {
            const int us = hit(kUnitScroll, 4);
            if (us == 0) unitScroll_ = 0;
            else if (us == 1) unitScroll_ = std::max(unitScroll_ - 1, 0);
            else if (us == 2) unitScroll_ = std::min(unitScroll_ + 1, std::max(rows - 5, 0));
            else if (us == 3) unitScroll_ = std::max(rows - 5, 0);
            const auto& m = ctx_.mouse();
            for (int c = 0; c < 20; ++c) {
                const int ux = 0xdd + (c & 3) * 0x48, uy = 0x49 + (c >> 2) * 0x48;
                const int at = unitScroll_ * 4 + c;
                if (m.x > ux + 0x15 && m.x < ux + 0x15 + 48 && m.y > uy && m.y < uy + 0x48 && at < int(s.roster.size())) {
                    auto& p = s.picked[std::size_t(at)];
                    if (p || pickedCount() < netgame::mapEntry(localMap_, v).maxUnits) {
                        p ^= 1;
                        ctx_.audio->sePlay(5);
                        sendPick();
                    }
                }
            }
            // FUN_00448320: the chat goes to all (0) or our team (1).
            const int ct = hit(kChatButtons, 2);
            if (ct != -1) chatTeam_ = ct;
            // FUN_0044a070: the description's page (summary, objective,
            // conditions, strength).
            const int tab = hit(kSideTabs, 4);
            if (tab != -1) infoSide_ = tab;
            // Its arrows (DAT_00605e78 0..3: top, up, down, bottom). The
            // original moves a hidden cursor over all 0x14 lines (a 12-row
            // ListBoxCtrl: FUN_00428720 / 8810 / 8850 / 8760), so the page
            // only moves after 11 presses; here they scroll the page at once
            // and stop at the side's last line of text.
            const int arrow = hit(kInfoArrows, 4);
            if (arrow != -1) {
                int lines = 0;
                for (int i = 0; i < 0x14; ++i) if (!netgame::mapLine(localMap_, infoSide_, i, v).empty()) lines = i + 1;
                const int most = std::max(lines - kInfoRows, 0);
                int& top = infoTop_[std::size_t(infoSide_)];
                top = arrow == 0 ? 0 : arrow == 1 ? std::max(top - 1, 0) : arrow == 2 ? std::min(top + 1, most) : most;
                ctx_.audio->sePlay(5);
            }
            // FUN_0044bb20: the host sets each player's team (1..4, ?
            // random) unless the map is co-operative (FUN_004429a0 sends it).
            if (s.isHost() && !netgame::mapEntry(localMap_, v).coop) {
                const int cell = hit(kTeamCells, 20);
                if (cell != -1 && teams_[std::size_t(cell / 5)] != cell % 5) {
                    teams_[std::size_t(cell / 5)] = std::uint8_t(cell % 5);
                    ctx_.audio->sePlay(5);
                    sendOptions();
                }
            }
            // The address button (DAT_00605d18 [3]) puts the invite code on the clipboard.
            if (button == 3 && !s.room().code.empty()) {
                SDL_SetClipboardText(s.room().code.c_str());
                status_ = "Invite code " + s.room().code + " copied.";
                statusTime_ = 90;
            }
            // FUN_004498c0: the host's map lists (versus, co-operative) and
            // their arrows move a list's cursor; the map is the cursor's
            // (FUN_004494b0), a new one resets the description (FUN_00449fd0).
            if (s.isHost()) {
                for (int l = 0; l < 2; ++l) {
                    ListBoxCtrl& list = mapList_[std::size_t(l)];
                    const int r = hit(kMapRows[l], std::min(list.count(), kVisibleMaps));
                    if (r != -1) {
                        list.setRow(r);
                        mapType_ = l;
                        ctx_.audio->sePlay(5);
                    }
                }
                const int a = hit(kMapScroll, 8);
                if (a != -1) {
                    ListBoxCtrl& list = mapList_[std::size_t(a / 4)];
                    switch (a & 3) {
                        case 0: list.first(); break;
                        case 1: list.up(); break;
                        case 2: list.down(); break;
                        default: list.last(); break;
                    }
                    mapType_ = a / 4;
                    ctx_.audio->sePlay(5);
                }
                const std::vector<int> maps = netgame::mapList(mapType_ == 1, ctx_.work, v);
                const int at = mapList_[std::size_t(mapType_)].index();
                if (at < int(maps.size()) && maps[std::size_t(at)] != localMap_) {
                    localMap_ = maps[std::size_t(at)];
                    resetInfo();
                    clampPicks();
                    sendOptions();
                    sendPick();
                }
            }
        }
        if (fadeAlpha_ > 0) {
            ctx_.draw.fill({.x = 0, .y = 0, .w = 0x403, .h = 0x300, .priority = 2, .argb = std::uint32_t(fadeAlpha_) << 24});
            fadeAlpha_ = std::max(fadeAlpha_ - 0xff / 0x3c - 1, 0);
        }
        drawRoom((ctx_.mouse().held & kMouseLeft) ? button : -1);
        backdrop();
        co_await yieldFrame();
    }
}

// FUN_00447be0: the room to black in 60 frames.
Co<> NetTask::fadeRoom() {
    for (int i = 0; i <= 0x3c; ++i) {
        ctx_.draw.fill({.x = 0, .y = 0, .w = 0x403, .h = 0x300, .priority = 2, .argb = std::uint32_t(i * 0xff / 0x3c) << 24});
        drawRoom(-1);
        backdrop();
        co_await yieldFrame();
    }
}

// FUN_004481b0
void NetTask::drawRoom(int pressed) {
    net::Session& s = ctx_.net;
    const GameVersion v = ctx_.version;
    sub(5, kRoomBack, 0, 0, 0x5a);   // FUN_0044b8b0
    if (pressed != -1 && pressed != 2)                                                   // FUN_004492d0
        sub(5, kRoomPressed[pressed],
            kRoomButtons[pressed].x, kRoomButtons[pressed].y, 0x55);
    // FUN_00449e00: the two map lists (the host's choice lit).
    for (int l = 0; l < 2; ++l) {
        const std::vector<int> maps = netgame::mapList(l == 1, ctx_.work, v);
        const ListBoxCtrl& list = mapList_[std::size_t(l)];
        for (int r = 0; r < list.disp() && list.top() + r < int(maps.size()); ++r) {
            const int at = maps[std::size_t(list.top() + r)];
            const auto& row = kMapRows[l][r];
            if (at == localMap_) ctx_.draw.fill({.x = row.x, .y = row.y, .w = row.w, .h = row.h, .priority = 0x56, .argb = 0x60ffffffu});
            text(row.x + 2, row.y + 1, netgame::mapTitle(at, v), 0x54, s.isHost() ? 0xffffffffu : 0xffa0a0a0u, 13);
        }
    }
    // FUN_0044a310 / FUN_0044a3e0 / FUN_0044a530: the side tab, the map's
    // objectives for that side, the map's title.
    // (FUN_0044a210: the tab or arrow under the pointer while held, the same rects.)
    sub(5, kSideTabOn[infoSide_],
        kSideTabs[infoSide_].x, kSideTabs[infoSide_].y, 0x56);
    text(0x324, 0x31, netgame::mapTitle(localMap_, v), 0x54, 0xffffffff, 16);
    // FUN_0044a3e0: that side's 12 rows from its first shown line, 0x11 apart.
    const int top = infoTop_[std::size_t(infoSide_)];
    for (int i = top; i != top + kInfoRows && i < 0x14; ++i)
        text(0x242 + 2, 0x70 + (i - top) * 0x11, netgame::mapLine(localMap_, infoSide_, i, v), 0x54, 0xffffffff, 13);
    // FUN_0044b230 / FUN_0044b300: the roster: face (bank 0x5b by character),
    // job, level (+0xe) and stamina in minutes (+0x20 / 0xe10); greyed unpicked.
    for (int c = 0; c < 20; ++c) {
        const int at = unitScroll_ * 4 + c;
        if (at >= int(s.roster.size())) break;
        const auto& r = s.roster[std::size_t(at)];
        const int x = 0xdd + (c & 3) * 0x48, y = 0x49 + (c >> 2) * 0x48;
        const bool on = at < int(s.picked.size()) && s.picked[std::size_t(at)];
        const int ch = r[2] | r[3] << 8;
        ysub::drawSub(ctx_, {.bank = static_cast<std::uint8_t>(kFaces), .frame = static_cast<std::uint16_t>(ch), .sx = kFace.x,
                             .sy = kFace.y, .w = kFace.w, .h = kFace.h, .alpha = std::uint8_t(on ? 0x80 : 0x40), .rgb = std::uint8_t(on ? 0x80 : 0x40)},
                      0x4c, x + 0x15, y + 2);
        const int job = r[0xc] | r[0xd] << 8;
        if (job != 0 && job < 0x80) {
            const int k = std::max(int(logic::sdU8(0x6b2d8a + std::uint32_t(job) * 0x40, v)) - 1, 0);
            sub(5, {kJobs[std::min(k, 4)].x, kJobs[std::min(k, 4)].y, 18, 18}, x + 1, y + 1, 0x4d);
        }
        const int minutes = std::min<int>(int(std::uint32_t(r[0x20] | r[0x21] << 8 | r[0x22] << 16 | r[0x23] << 24) / 0xe10), 99);
        number(x + 2, y + 0x16, 0x4d, std::min<int>(r[0xe], 99), 2);
        number(x + 2, y + 0x26, 0x4d, minutes, 2);
    }
    // FUN_0044b170: the roster's arrows while held; FUN_00449d00 the map lists'.
    if (ctx_.mouse().held & kMouseLeft) {
        if (const int us = hit(kUnitScroll, 4); us != -1)
            ctx_.draw.fill({.x = kUnitScroll[us].x, .y = kUnitScroll[us].y, .w = kUnitScroll[us].w, .h = kUnitScroll[us].h, .priority = 0x54, .argb = 0x50ffffffu});
        if (s.isHost())
            if (const int a = hit(kMapScroll, 8); a != -1)
                sub(5, kMapScrollOn[a], kMapScroll[a].x, kMapScroll[a].y, 0x55);
    }
    // FUN_0044b5b0 / FUN_0044b550: the map's limit and how many are picked.
    number(0x98, 0xb0, 0x55, netgame::mapEntry(localMap_, v).maxUnits, 2);   // FUN_0044ad50: the map's limit
    number(0x98, 200, 0x55, pickedCount(), 2);
    // FUN_0044b730 / FUN_0044bc30: the players: ready mark, name, units, side.
    for (const auto& m : s.room().members) {
        if (m.slot < 0 || m.slot > 3) continue;
        const int y = 0x24d + m.slot * 0x19;
        if (m.ready || m.slot == s.room().host)
            sub(5, {kReadyMarks[m.slot].x, kReadyMarks[m.slot].y, 20, 21}, 0x239, 0x24a + m.slot * 0x19, 0x55);
        std::string name = m.name + (m.slot == s.room().host ? "  (host)" : "") + (m.slot == s.room().you ? "  *" : "");
        text(0x256, y, name, 0x55, 0xffffffff, 14);
        // FUN_0044b730: the units brought and their total level (FUN_0044acd0); the round trip.
        const auto units = netgame::decodePick(m.data);
        int levels = 0;
        for (const auto& u : units) levels += u[0xe];
        number(0x36e, y + 2, 0x55, int(units.size()), 2);
        number(0x3b8, y + 2, 0x55, levels, 4);
        text(0x2c0, y + 4, std::to_string(m.rttMs) + "ms", 0x55, 0xff909090u, 10);
    }
    // FUN_0044bc30: each slot's team mark in its row (dimmed on a co-operative map).
    const bool coop = netgame::mapEntry(localMap_, v).coop;
    for (int slot = 0; slot < 4; ++slot) {
        const int t = std::min<int>(coop ? 0 : teams_[std::size_t(slot)], 4);
        const auto& c = kTeamCells[slot * 5 + t];
        sub(5, {kSideMarks[t].x, kSideMarks[t].y, 20, 21}, c.x, c.y, 0x55, 0xff, coop ? 0x68 : 0x80);
    }
    // FUN_00449380: our ready mark (the others), the host's start button.
    if (!s.isHost()) {
        for (const auto& m : s.room().members)
            if (m.slot == s.room().you && m.ready) sub(5, {1, 769, 250, 29}, 6, 732, 0x55);
    }
    // FUN_004485b0: the chat line at (7, 0x2ba) and the log above it
    // (FUN_00425620 at (7, 0x1de), 0x14 lines).
    // FUN_004432f0: a line meant for one team shows only to that team's
    // players (FUN_0044c800: the sender's team is ours) and its sender.
    std::vector<std::pair<int, std::string>> lines;
    for (const auto& c : s.chatLog()) {
        std::string t = c.text;
        if (!t.empty() && t[0] == '\x01') {
            t.erase(0, 1);
            const int me = s.room().you;
            const bool same = coop || (c.slot >= 0 && c.slot < 4 && me >= 0 && me < 4 && teams_[std::size_t(c.slot)] == teams_[std::size_t(me)]);
            if (!same && c.slot != me) continue;
            t = "(team) " + t;
        }
        lines.emplace_back(c.slot, std::move(t));
    }
    if (SDL_getenv("CH_NET_TRACE"))
        for (std::size_t i = loggedChat_; i < lines.size(); ++i) SDL_Log("[net] room chat shown: %s", lines[i].second.c_str());
    loggedChat_ = std::max(loggedChat_, lines.size());
    // FUN_00443c10 makes the box of 10 lines; FUN_00448490 adds each line.
    ChatBox log(10);
    for (const auto& [slot, line] : lines) {
        std::string who = "?";
        for (const auto& m : s.room().members) if (m.slot == slot) who = m.name;
        log.add(who + ": " + line);
    }
    log.draw([&](int row, const std::string& line) {
        if (!line.empty()) text(7, 0x1de + row * 0x14, line, 0x55, 0xffffffff, 13);
    });
    // FUN_004483c0: the chosen chat button lit.
    sub(5, kChatOn[chatTeam_],
        kChatButtons[chatTeam_].x, kChatButtons[chatTeam_].y, 0x55);
    text(7, 0x2ba, "> " + chatLine_ + ((SDL_GetTicks() / 400) % 2 ? "_" : ""), 0x55, 0xffffffff, 14);
    // The invite code (the session name's place).
    text(0x34a, 0x2b6, "Room " + s.room().code, 0x55, 0xffffffff, 14);
}

} // namespace ch
