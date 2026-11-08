// tSysChatBox (game/ysub/chat_box.hpp) and tSysListBoxCtrl
// (game/ysub/list_box.hpp) against the original's arithmetic.
#include "../game/ysub/chat_box.hpp"
#include "../game/ysub/list_box.hpp"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

static std::vector<std::string> rows(const ch::ChatBox& b) {
    std::vector<std::string> out;
    b.draw([&](int, const std::string& s) { out.push_back(s); });
    return out;
}

int main() {
    // The lobby's box: 10 lines, oldest first; empty rows still drawn.
    ch::ChatBox lobby(3);
    lobby.add("a");
    assert((rows(lobby) == std::vector<std::string>{"a", "", ""}));
    for (const char* s : {"b", "c", "d", "e"}) lobby.add(s);
    assert((rows(lobby) == std::vector<std::string>{"c", "d", "e"}));
    for (int i = 0; i < 20; ++i) lobby.add(std::to_string(i));   // written stays within count .. 2 count
    assert((rows(lobby) == std::vector<std::string>{"17", "18", "19"}));

    // The battle's box: newest first, nothing before the first line.
    ch::ChatBox battle(4, true);
    assert(rows(battle).empty());
    for (const char* s : {"a", "b", "c", "d", "e"}) battle.add(s);
    assert((rows(battle) == std::vector<std::string>{"e", "d", "c", "b"}));

    // A list of 20 items, 12 rows: the cursor moves before the page does.
    ch::ListBoxCtrl l(0x14, 0xc);
    for (int i = 0; i < 11; ++i) l.down();
    assert(l.top() == 0 && l.index() == 11);
    l.down();
    assert(l.top() == 1 && l.index() == 12);
    l.last();
    assert(l.top() == 8 && l.index() == 19);
    l.down();
    assert(l.top() == 8 && l.index() == 19);
    l.up();
    assert(l.top() == 8 && l.index() == 18);
    l.first();
    l.up();
    assert(l.top() == 0 && l.index() == 0);
    l.select(15);
    assert(l.top() == 4 && l.index() == 15);

    // Fewer items than rows: the cursor stops at the last item.
    ch::ListBoxCtrl s(3, 7);
    for (int i = 0; i < 5; ++i) s.down();
    assert(s.top() == 0 && s.index() == 2);
    s.setRow(5);
    assert(s.index() == 2);
    s.setRow(1);
    assert(s.index() == 1);
    ch::ListBoxCtrl e(0, 7);
    e.down();
    e.last();
    assert(e.index() == 0);
    std::puts("sys boxes ok");
    return 0;
}
