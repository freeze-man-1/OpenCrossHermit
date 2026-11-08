// ============================================================================
// saveload_task.hpp — SLProgTask, scene 0xd (src\yoshimaru\SLProg)
//
// The load and save screens over SaveData\*.sav (logic::saveGame /
// loadGame for the format).
//   FUN_004d9d50  Start: by DAT_007e11a8 (0/1/5/6 load, 2/3 save), then the
//                 next scene by the result in DAT_007e11aa
//   FUN_004d7830  init (bank 0x12, fonts, lists)
//   FUN_004d7e80  load screen, FUN_004d98b0 its draw
//   FUN_004d8620  save screen, FUN_004d9990 its draw
//   FUN_004d6cb0  list the valid saves, newest first (FUN_004d73c0 reads one)
//   FUN_004d7a80  message boxes, FUN_004d7720 write
// ============================================================================
#pragma once
#include "../game_context.hpp"
#include "../ysub/ysub.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ch {

class SaveLoadTask final : public Task {
public:
    explicit SaveLoadTask(GameContext& ctx) : ctx_(ctx) {}
    // FUN_004d9c00: the bank, the two GDI fonts (FUN_004d1bf0, DeleteObject;
    // the port's text renderer holds none), the text lists.
    ~SaveLoadTask() override;
    Co<> start(std::uint32_t) override;

private:
    static constexpr std::uint16_t kBank = 0x12;   // data\SLProg\SLProg.bin

    struct Entry { std::string file, name, date, time; std::int64_t stamp = 0; };

    Co<> loadScreen();                           // FUN_004d7e80
    Co<> saveScreen();                           // FUN_004d8620
    Co<> fade(bool in, bool save);
    void scan(int visible);                      // FUN_004d6cb0
    void messageBox(int kind);                   // FUN_004d7a80
    void drawLoad();                             // FUN_004d98b0
    void drawSave();                             // FUN_004d9990
    void drawRows(int y, int visible);           // the list: name, game date, file time columns
    [[nodiscard]] ysub::Rect rect(std::uint32_t va) const;
    [[nodiscard]] bool button(int x, int y, int w, int h) const;
    int  scrollButtons(int y0, int pitch, int h, std::uint32_t flashVa);
    [[nodiscard]] std::string defaultName() const;

    // Web build only: the download and upload buttons (web/shell.html does the
    // browser's part). webInput returns true when an upload changed the selection.
    struct WebButtons { int download, upload, y; };
    bool webInput(const WebButtons& b, int visible);
    void webDraw(const WebButtons& b);
    void webUpload(const std::string& name, const std::string& path, std::string& newFile);

    GameContext&       ctx_;
    ysub::TextList     list_, nameLine_, message_;
    ysub::Flashes      flashes_;
    std::vector<Entry> entries_;
    int                selected_ = -1;     // +0x2c726
    int                top_ = 0;           // +0x2c728
    int                shownMessage_ = -1; // +0x2c724
    std::string        name_;              // the save screen's file name (without .SAV)
    struct Notice { std::string text; bool ok; };
    std::vector<Notice> notices_;          // upload results, shown for noticeFrames_
    int                noticeFrames_ = 0;
    bool               webLive_ = false;   // the buttons work (no dialog is up)
};

} // namespace ch
