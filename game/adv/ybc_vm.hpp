// ============================================================================
// ybc_vm.hpp — YBC script VM (src\yoshimaru\ybc32.cpp / ybctask.cpp)
//
// File (.ybc), u32 count + u32 offsets; block lookup clamps to the last block
// (FUN_004cd700), so an empty block aliases the next one:
//   block 0  code: records of u16 opcode, u16 byte length, operands
//   block 1  0x30-byte name records (scripts, picture banks)   (FUN_004c2190)
//   block 2  text table: u32 count, u32 entries (offset in low 20 bits)
//
// Operand words: top 4 bits kind (0 slot local, 1 global, 2 immediate,
// 3 = -1), low 28 bits value (FUN_004c1eb0 read / FUN_004c1f40 write).
//
// FUN_004ce8f0 runs the slot state machine once per frame: state 0 executes
// one instruction and takes the handler's wait kind as the new state;
// 1 waits on an opcode-specific condition, 2 resumes next frame, 3 waits to
// be released from outside, 4 waits on a global hold flag.
// ============================================================================
#pragma once

#include "../state/game_work.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ch {

class YbcScript {
public:
    YbcScript() = default;
    explicit YbcScript(std::vector<std::uint8_t> bytes);

    [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }
    [[nodiscard]] std::uint32_t block(std::uint32_t k) const;          // FUN_004cd700
    [[nodiscard]] std::uint32_t codeSize() const;
    [[nodiscard]] std::string   name(std::uint16_t index) const;       // block 1 record (Shift-JIS)
    [[nodiscard]] std::string   text(std::uint32_t index) const;       // block 2 entry (Shift-JIS)
    [[nodiscard]] std::uint32_t textCount() const;

    [[nodiscard]] std::uint8_t  u8(std::uint32_t off) const;
    [[nodiscard]] std::uint16_t u16(std::uint32_t off) const;
    [[nodiscard]] std::uint32_t u32(std::uint32_t off) const;

private:
    std::vector<std::uint8_t> bytes_;
};

class YbcVm;

// Everything the VM cannot do by itself: presentation, audio, scene changes.
// exec() starts an opcode's effect; the VM then polls waitDone() while the
// slot is in wait state 1 or 3. Unported opcodes should log and finish at once.
class IYbcHost {
public:
    virtual ~IYbcHost() = default;
    virtual std::vector<std::uint8_t> loadFile(std::string_view legacyPath) = 0;
    // Opcodes the VM does not implement itself. Read operands through
    // vm.value(rec, off) / vm.script(); `rec` is the record's offset in block 0.
    virtual void exec(YbcVm& vm, std::uint16_t op, std::uint32_t rec) = 0;
    [[nodiscard]] virtual bool waitDone(std::uint16_t op) = 0;
    // Debug fast-forward: finish the wait at once, with the effect it would
    // have had when it ended. False stops the fast-forward there (a choice).
    [[nodiscard]] virtual bool skipWait(std::uint16_t) { return true; }
    // The script ended (FUN_004ce800, "YBCSysBgmStop": despite its name it
    // stops the voice player, DirectX +0x8f00; the music, +0x8c88, plays on).
    virtual void scriptEnded() = 0;
};

// Game state the scripts read and write (opcodes 0x5a / 0x5b / 0xa9 / 0xab) lives
// in GameWork (state/game_work.hpp).

class YbcVm {
public:
    static constexpr std::size_t kLocals  = 0x32;    // per slot
    static constexpr std::size_t kGlobals = 0x400;   // DAT_007aab1c (size not known; bounded)

    YbcVm(IYbcHost& host, GameWork& work) : host_(host), work_(work) {}

    // FUN_004ce260 / FUN_004ce360: load into slot 0. `entry` is stored in
    // the task (+0x92dc) unless it is 0x7f (FUN_004ce6e0 passes 0x7f).
    void load(std::string_view legacyPath, std::int32_t entry);
    // FUN_004ce460 -> FUN_004ce4b0: a script already in memory (the battle
    // scripts' chunks), into slot 0 when -1 is given.
    void loadCode(std::vector<std::uint8_t> bytes, std::int32_t entry);
    [[nodiscard]] bool running() const noexcept { return slot_.active; }
    // FUN_004ce8f0. Returns false once the script has ended (opcode 0x13).
    bool tick();

    void release() noexcept { if (slot_.state == 3) slot_.state = 0; }   // external release of state 3
    // A host wait that branches (0x0e click, 0x34 hotspot) sets the next
    // instruction; the slot resumes there once waitDone() returns true.
    void jump(std::uint32_t pc) noexcept { slot_.pc = pc; }
    void setHold(bool hold) noexcept { hold_ = hold; }                    // +0x92e0
    // Debug: run on without waiting (every opcode still executes) until the
    // script ends or the host stops it at a choice.
    void setFastForward(bool on) noexcept { fastForward_ = on; }
    [[nodiscard]] bool fastForward() const noexcept { return fastForward_; }

    [[nodiscard]] std::int32_t global(std::uint32_t i) const { return i < kGlobals ? globals_[i] : 0; }
    // Operand helpers for host-implemented opcodes.
    [[nodiscard]] std::int32_t  value(std::uint32_t rec, std::uint32_t off) const { return read(operandAt(rec, off)); }
    [[nodiscard]] std::uint16_t field16(std::uint32_t rec, std::uint32_t off) const {
        return slot_.file.u16(slot_.file.block(0) + rec + off);
    }
    void store(std::uint32_t rec, std::uint32_t off, std::int32_t v) { write(operandAt(rec, off), v); }
    // A handler that picks its own wait (FUN_004c5bc0 / FUN_004c66e0 return
    // 0 or 1 by their operand): the state the slot takes after this opcode.
    void setWait(int kind) noexcept { waitOverride_ = kind; }
    [[nodiscard]] std::int32_t entry() const noexcept { return entry_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }   // the script last loaded by name
    [[nodiscard]] const YbcScript& script() const noexcept { return slot_.file; }
    [[nodiscard]] std::uint64_t executed() const noexcept { return executed_; }

    // Wait kind each opcode returns (third argument at its dispatch site).
    static int waitKind(std::uint16_t op);

private:
    struct Slot {
        bool          active = false;             // +4
        YbcScript     file;                       // +0xc
        std::uint32_t pc = 0;                     // +0x1c, offset into block 0
        std::uint16_t state = 0;                  // +0x20
        std::uint16_t op = 0, len = 0;            // +0x22 / +0x24
        std::array<std::int32_t, kLocals> locals{};   // +0x3c
    };

    [[nodiscard]] static std::int32_t arith(std::int32_t a, std::int32_t b, std::uint8_t kind);
    [[nodiscard]] std::int32_t nextRand();
    [[nodiscard]] std::int32_t read(std::uint32_t operand) const;   // FUN_004c1eb0
    void write(std::uint32_t operand, std::int32_t value);         // FUN_004c1f40
    [[nodiscard]] std::uint32_t operandAt(std::uint32_t rec, std::uint32_t off) const {
        return slot_.file.u32(slot_.file.block(0) + rec + off);
    }
    int execute(std::uint32_t rec);
    void endScript();

    IYbcHost&     host_;
    GameWork&     work_;
    Slot          slot_;
    std::array<std::int32_t, kGlobals> globals_{};
    std::string   path_;
    std::int32_t  entry_ = -1;                    // +0x92dc (DAT_007e0f04): next scene, -1 none
    bool          hold_ = false;
    int           waitOverride_ = -1;
    bool          fastForward_ = false;
    std::uint32_t rand_ = 1;                     // opcode 7: MSVC rand() from its default seed
    std::uint64_t executed_ = 0;
};

} // namespace ch
