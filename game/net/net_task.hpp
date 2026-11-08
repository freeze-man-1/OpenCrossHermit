// ============================================================================
// net_task.hpp — NetTask, the network game's screens (scenes 0xE / 0xF,
// src\yoshida\nettask.cpp), on the relay (net/net_session.hpp)
//
// FUN_004439d0 (from the title's "Multi", start 0) and FUN_00443ac0 (back
// from a battle or a save, start 1) make the task; FUN_00444330 runs its
// screens by +0x1a50:
//   0 fade in -> 5         FUN_00444820
//   5 the player's name     FUN_00444a90 (IME box at 0x1bd, 0x171, 15 chars)
//   6 the menu              FUN_00444e60: 0 / 1 the session list, 2 back
//   7 / 8 the session list  FUN_00445150 / FUN_00446780 ("Now Search...",
//                           join, create, back; 7 rows)
//   10 the room             FUN_004478e0 / FUN_00447d00: the map (the host),
//                           the units, the objectives by side, the players,
//                           chat, ready / start, a save's party
//   11 the start            FUN_00448650 -> FUN_00448a30, then task 0x10
//   1 / 2 fade out -> 3 the title
// What DirectPlay did is the relay's: a session is a room, the session list
// the relay's room list (and its invite code joins a room not listed), and
// the start handshake (FUN_00442090 / FUN_00442360) the relay's Start.
// FUN_00443c10 makes it (FUN_00444780, FUN_00443bb0), FUN_00444060 tears it
// down (the destructor here). DirectPlay's IP entry (FUN_00446a80 /
// FUN_00446c70, its keypad FUN_004470c0 / FUN_00447660 / FUN_00447720 with
// FUN_00446e20 / FUN_00446e90 / FUN_00446fa0 checking the address) is the
// invite code; joining the session picked (FUN_00445c10) is joinRoom; the
// session list's rows (FUN_00445eb0 / FUN_00445f00 / FUN_00445f60, the host's
// name and "%s Ver %d.%d", FUN_004461f0 / FUN_00446290) are the room list's.
// The map lists (FUN_004494b0 / FUN_00449510 / FUN_004495a0 / FUN_00449650 /
// FUN_004496d0, FUN_0044bed0 / FUN_0044bf40 / FUN_0044c000 / FUN_0044c0a0 /
// FUN_0044c150, FUN_0044a640) and the map's text sprites (FUN_0044c490 /
// FUN_0044c560 / FUN_0044c680, FUN_00449fd0) are drawn as text from
// net_game; FUN_0044b610 draws the players' names the same way. The
// roster's places (FUN_0044abe0 counts them, FUN_0044ab40 sizes its scroll,
// FUN_0044aa30 fills them from the work, FUN_0044ae10 clears the picks,
// FUN_0044ae70 picks all) are Session::roster / picked. FUN_0044b9d0 /
// FUN_0044ba50: a co-operative map puts everyone on one side (net_game).
// Drawn from data\Tactics\NetData.bin (bank 0x5a: frame 7 the backdrop, 1
// the name box, 2 the menu, 3 the list, 5 the room, 0 the pointer) and
// NetUnitFace.bin (bank 0x5b, by character).
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "../ysub/list_box.hpp"
#include "net_game.hpp"

#include <string>
#include <vector>

namespace ch {

class NetTask final : public Task {
public:
    explicit NetTask(GameContext& ctx) : ctx_(ctx) {}
    ~NetTask() override;                         // FUN_004442f0
    Co<> start(std::uint32_t param) override;    // FUN_00444330

private:
    static constexpr std::uint16_t kBank = 0x5a, kFaces = 0x5b;
    static constexpr std::uint8_t kMesBank = 2;   // data\Tactics\MesBox.bin (FUN_00441470)
    struct Rect { int x, y, w, h; };

    Co<> fade(bool in);                          // FUN_00444820 / FUN_00444960
    Co<int> nameScreen();                        // FUN_00444a90: 6 on, 2 back
    Co<int> menuScreen();                        // FUN_00444e60: 7 the list, 8 the code, 2 back
    Co<int> listScreen(bool byCode);             // FUN_00445150 / FUN_00446780: 10 a room, 6 back
    Co<int> roomScreen();                        // FUN_00447d00: 11 start, 6 left, 2 the title
    Co<> fadeRoom();                             // FUN_00447be0

    // Drawing.
    void backdrop();                             // FUN_0044ca80 / FUN_0044c9a0
    void number(int x, int y, int prio, int value, int digits);   // FUN_0044cc80 / FUN_0044cce0
    void sub(int frame, const Rect& src, int x, int y, int prio, std::uint8_t alpha = 0xff, std::uint8_t rgb = 0x80);
    void text(int x, int y, const std::string& s, int prio, std::uint32_t argb = 0xffffffff, int px = 14);
    [[nodiscard]] int hit(const Rect* rects, int n) const;   // FUN_0044c710
    void drawRoom(int hover);                    // FUN_004481b0
    void drawStatus();                           // short notes (not the original's)
    void message(int set);                       // FUN_00441630: open message `set`
    bool messageOpen();                          // draw it and take its click; false once closed
    int  mesSet_ = -1;

    // The room's state on our side.
    void sendPick();                             // FUN_00442f40: our units go to the others
    void sendOptions();                          // the host's map and sides
    [[nodiscard]] netgame::Options options() const;
    [[nodiscard]] int pickedCount() const;       // FUN_0044ac50
    void typing(std::string& line, std::size_t most);   // the IME box (FUN_00425a10 ..)

    GameContext& ctx_;
    std::string  status_;                        // the last message box (FUN_00441630)
    int          statusTime_ = 0;
    std::string  statusShown_;                   // CH_NET_TRACE: the note last logged
    // The room.
    int unitScroll_ = 0, infoSide_ = 0;          // infoSide_: the description's page (+0x1a8a)
    ListBoxCtrl rooms_;                          // the session list (+0x1a54 ..; 7 rows)
    std::array<ListBoxCtrl, 2> mapList_;         // +0x8a60: the versus and co-operative maps, 7 rows
    int mapType_ = 0;                            // +0x1a8b: the list the host's map is from
    std::array<int, 4> infoTop_{};               // each side's description's first shown line (+0x8a80's lists)
    std::string chatLine_;
    int localMap_ = 0;                           // the host's choice (+0x1a88)
    std::array<std::uint8_t, 4> teams_{0, 1, 2, 3};   // each slot's team (+0x38: 0..3, 4 random)
    int chatTeam_ = 0;                           // +0x4e: chat to all (0) or our team (1)
    std::size_t loggedChat_ = 0;                 // CH_NET_TRACE: the lines already logged
    int fadeAlpha_ = 0;
};

} // namespace ch
