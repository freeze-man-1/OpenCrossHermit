// ============================================================================
// ybc_vm.cpp
// ============================================================================
#include "ybc_vm.hpp"
#include "../state/game_logic.hpp"

#include <cstdint>

#include <SDL.h>

#include <stdexcept>

namespace ch {

// ------------------------------------------------------------ YbcScript ---
YbcScript::YbcScript(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    if (bytes_.size() < 8 || u32(0) == 0) throw std::runtime_error("ybc: bad header");
}

std::uint8_t YbcScript::u8(std::uint32_t off) const {
    if (off >= bytes_.size()) throw std::out_of_range("ybc: read past end");
    return bytes_[off];
}
std::uint16_t YbcScript::u16(std::uint32_t off) const { return static_cast<std::uint16_t>(u8(off) | u8(off + 1) << 8); }
std::uint32_t YbcScript::u32(std::uint32_t off) const { return u16(off) | std::uint32_t(u16(off + 2)) << 16; }

std::uint32_t YbcScript::block(std::uint32_t k) const {
    const std::uint32_t count = u32(0);
    if (k > count - 1) k = count - 1;
    return u32(4 + 4 * k);
}

std::uint32_t YbcScript::codeSize() const {
    return (u32(0) > 1 ? block(1) : static_cast<std::uint32_t>(bytes_.size())) - block(0);
}

std::string YbcScript::name(std::uint16_t index) const {
    const std::uint32_t at = block(1) + index * 0x30u;
    std::string s;
    for (std::uint32_t i = 0; i < 0x30 && u8(at + i); ++i) s.push_back(static_cast<char>(u8(at + i)));
    return s;
}

std::uint32_t YbcScript::textCount() const { return u32(block(2)); }

// FUN_004c2120: base + (entry & 0xfffff); entries are NUL-terminated.
std::string YbcScript::text(std::uint32_t index) const {
    const std::uint32_t base = block(2);
    std::uint32_t at = base + (u32(base + 4 + 4 * index) & 0xFFFFF);
    std::string s;
    while (at < bytes_.size() && bytes_[at]) s.push_back(static_cast<char>(bytes_[at++]));
    return s;
}

// --------------------------------------------------------------- YbcVm ---
// Third argument at each dispatch site in FUN_004ce8f0 (opcode = case + 5);
// generated from the decompile. Opcodes not listed return 0.
int YbcVm::waitKind(std::uint16_t op) {
    static constexpr struct { std::uint16_t op; std::uint8_t kind; } kTable[] = {
        {0x0c, 1}, {0x0d, 1}, {0x0e, 1}, {0x0f, 2}, {0x10, 2}, {0x17, 1}, {0x18, 3}, {0x19, 1},
        {0x1b, 1}, {0x1c, 1}, {0x1e, 1}, {0x20, 1}, {0x22, 1}, {0x23, 3}, {0x24, 1}, {0x26, 1},
        {0x28, 1}, {0x29, 1}, {0x2a, 1}, {0x31, 1}, {0x32, 1}, {0x33, 1}, {0x34, 1}, {0x38, 1},
        {0x39, 1}, {0x3b, 1}, {0x3c, 1}, {0x3d, 1}, {0x3f, 1}, {0x40, 1}, {0x75, 4}, {0x76, 4},
        {0x77, 4}, {0x78, 4}, {0x79, 4}, {0x88, 4}, {0x8e, 4}, {0xa8, 1}, {0xac, 2},
    };
    for (const auto& e : kTable) if (e.op == op) return e.kind;
    return 0;
}

std::int32_t YbcVm::read(std::uint32_t operand) const {
    const std::uint32_t v = operand & 0x0FFFFFFF;
    switch (operand >> 28) {
        case 0:  return v < kLocals ? slot_.locals[v] : 0;
        case 1:  return v < kGlobals ? globals_[v] : 0;
        case 2:  return static_cast<std::int32_t>(v);
        case 3:  return -1;
        default: return 0;   // the original returns an uninitialised local here
    }
}

void YbcVm::write(std::uint32_t operand, std::int32_t value) {
    const std::uint32_t v = operand & 0x0FFFFFFF;
    switch (operand >> 28) {
        case 0: if (v < kLocals) slot_.locals[v] = value; break;
        case 1: if (v < kGlobals) globals_[v] = value; else SDL_Log("ybc: global %u out of range", v); break;
        default: break;    // immediates are read-only
    }
}

// FUN_004ce260 / FUN_004ce360 -> FUN_004ce560
void YbcVm::load(std::string_view legacyPath, std::int32_t entry) {
    slot_ = Slot{};
    slot_.file = YbcScript(host_.loadFile(legacyPath));
    slot_.active = true;
    if (entry != 0x7f) entry_ = entry;
    path_ = legacyPath;
    SDL_Log("ybc: script %.*s", int(legacyPath.size()), legacyPath.data());
}

void YbcVm::loadCode(std::vector<std::uint8_t> bytes, std::int32_t entry) {
    slot_ = Slot{};
    slot_.file = YbcScript(std::move(bytes));
    slot_.active = true;
    if (entry != 0x7f) entry_ = entry;
}

// FUN_004ce090(0) + FUN_004ce800; the host's state goes back to its start
// (FUN_004cd8d0: windows, layers, buffers and the loaded-bank marks; given
// 1, only the slot's flag), as FUN_004ce150 does when a slot is unloaded.
void YbcVm::endScript() {          // FUN_004ce090(0) + FUN_004ce800
    slot_.active = false;
    host_.scriptEnded();
}

namespace {
bool compare(std::int32_t a, std::int32_t b, std::uint8_t kind) {   // FUN_004c2060
    switch (kind) {
        case 1: return a == b;
        case 2: return a != b;
        case 3: return a > b;
        case 4: return a >= b;
        case 5: return a < b;
        case 6: return a <= b;
        default: return false;
    }
}
} // namespace

// FUN_004c1fc0: 1 +, 2 -, 3 *, 4 /, 5 %. 32-bit wrap-around as on x86;
// a zero divisor (a crash in the original) gives 0.
std::int32_t YbcVm::arith(std::int32_t a, std::int32_t b, std::uint8_t kind) {
    const auto ua = static_cast<std::uint32_t>(a), ub = static_cast<std::uint32_t>(b);
    switch (kind) {
        case 1: return static_cast<std::int32_t>(ua + ub);
        case 2: return static_cast<std::int32_t>(ua - ub);
        case 3: return static_cast<std::int32_t>(ua * ub);
        case 4: return b == 0 || (a == INT32_MIN && b == -1) ? 0 : a / b;
        case 5: return b == 0 || (a == INT32_MIN && b == -1) ? 0 : a % b;
        default: return 0;
    }
}

// The MSVC rand() sequence (0..0x7fff).
std::int32_t YbcVm::nextRand() {
    rand_ = rand_ * 214013u + 2531011u;
    return static_cast<std::int32_t>(rand_ >> 16 & 0x7fff);
}

int YbcVm::execute(std::uint32_t rec) {
    const YbcScript& f = slot_.file;
    const std::uint32_t at = f.block(0) + rec;
    switch (slot_.op) {
        case 0x05:   // dest = a (FUN_004c3f40)
            write(operandAt(rec, 4), read(operandAt(rec, 8)));
            break;
        case 0x06:   // dest = a <op> b (FUN_004c4000 / FUN_004c1fc0)
            write(operandAt(rec, 8), arith(read(operandAt(rec, 0xc)), read(operandAt(rec, 0x10)), f.u8(at + 4)));
            break;
        case 0x07: { // dest = rand() * a / 0x7fff (FUN_004c4130)
            const std::int64_t r = nextRand();
            write(operandAt(rec, 4), static_cast<std::int32_t>(r * read(operandAt(rec, 8)) / 0x7fff));
            break;
        }
        case 0x08:   // FUN_004c23f0: if (a <cmp> b) goto target
        case 0x09:   // FUN_004c2500: if !(a <cmp> b) goto target
            if (compare(read(operandAt(rec, 8)), read(operandAt(rec, 0xc)), f.u8(at + 4)) == (slot_.op == 0x08)) {
                slot_.len = 0;
                slot_.pc = f.u32(at + 0x10);
            }
            break;
        case 0x0b:   // FUN_004c2610: goto
            slot_.len = 0;
            slot_.pc = f.u32(at + 4);
            break;
        case 0x0f:   // FUN_004c21f0 -> FUN_004ce6e0: chain to script (clears the screen)
        case 0xac: { // FUN_004c22b0 -> FUN_004ce360: chain to script, keep the screen
            const std::string next = f.name(f.u16(at + 4));
            host_.exec(*this, slot_.op, rec);           // presentation side of the switch
            load(next, 0x7f);
            slot_.len = 0;
            return waitKind(slot_.op);                  // slot was reset; state set by caller
        }
        // No operation of their own: FUN_004c4b40 (0x12); FUN_004c2370 (0x10)
        // only reads its operand and resumes next frame. The rest only wait
        // (AdvTask::waitDone): FUN_004c5d00 (0x19, the windows),
        // FUN_004c6820 (0x24, the faces), FUN_004c5220 / FUN_004c5260 (0x31 /
        // 0x32, the text), FUN_004c54c0 (0x39, the fade), FUN_004c5960 (0x3c,
        // the backgrounds).
        case 0x10: case 0x12: case 0x19: case 0x24: case 0x31: case 0x32: case 0x39: case 0x3c:
            break;
        // Read their operands into the scratch and stop there: FUN_0042d2a0
        // (0x6d), FUN_0042e140 / FUN_0042e240 / FUN_0042e470 / FUN_0042e810
        // (0x80 / 0x81 / 0x83 / 0x86). FUN_0042d3c0 (0x6e) asserts at once (a
        // retired command).
        case 0x6d: case 0x6e: case 0x80: case 0x81: case 0x83: case 0x86:
            break;
        case 0x87:   // FUN_0042e910: dest = 0
            write(operandAt(rec, 4), 0);
            break;
        // Story values (game_logic.hpp): dest = value / value = src.
        //   0x5c..0x5f -> FUN_004e1da0(type, i): FUN_0042c460, FUN_0042c510, FUN_0042c5c0, FUN_0042c670
        //   0x60..0x63 -> FUN_004e1fa0(type, i, v): FUN_0042c720, FUN_0042c7e0, FUN_0042c8a0, FUN_0042c960
        //   0x64..0x67 -> FUN_004e21f0(map, type, i): FUN_0042ca20, FUN_0042cb10, FUN_0042cc00, FUN_0042ccf0
        //   0x68..0x6b -> FUN_004e2480(map, type, i, v): FUN_0042cde0, FUN_0042ced0, FUN_0042cfc0, FUN_0042d0b0
        //   0x9e..0xa1 -> FUN_004e2760(-, type, i): FUN_0042f460, FUN_0042f550, FUN_0042f640, FUN_0042f730
        //   0xa2..0xa5 -> FUN_004e29e0(-, type, i, v): FUN_0042f820, FUN_0042f910, FUN_0042fa00, FUN_0042faf0
        case 0x5c: case 0x5d: case 0x5e: case 0x5f:
            write(operandAt(rec, 4), logic::globalValue(work_, slot_.op - 0x5c, std::uint32_t(read(operandAt(rec, 8)))));
            break;
        case 0x60: case 0x61: case 0x62: case 0x63:
            logic::setGlobalValue(work_, slot_.op - 0x60, std::uint32_t(read(operandAt(rec, 8))), read(operandAt(rec, 4)));
            break;
        case 0x64: case 0x65: case 0x66: case 0x67:
            write(operandAt(rec, 4), logic::mapValue(work_, read(operandAt(rec, 8)), slot_.op - 0x64,
                                                     std::uint32_t(read(operandAt(rec, 0xc)))));
            break;
        case 0x68: case 0x69: case 0x6a: case 0x6b:
            logic::setMapValue(work_, read(operandAt(rec, 8)), slot_.op - 0x68, std::uint32_t(read(operandAt(rec, 0xc))),
                               read(operandAt(rec, 4)));
            break;
        case 0x9e: case 0x9f: case 0xa0: case 0xa1:
            write(operandAt(rec, 4), logic::battleValue(work_, slot_.op - 0x9e, std::uint32_t(read(operandAt(rec, 0xc)))));
            break;
        case 0xa2: case 0xa3: case 0xa4: case 0xa5:
            logic::setBattleValue(work_, slot_.op - 0xa2, std::uint32_t(read(operandAt(rec, 0xc))), read(operandAt(rec, 4)));
            break;
        // FUN_004d0a10 / FUN_004d0a50 / FUN_004d0a90 / FUN_004d0ad0: nothing.
        case 0x52: case 0x53: case 0x54: case 0x55:
            break;
        // These read their operands into the VM's scratch (+0x9250..58)
        // and stop there: FUN_004d11f0 (0x56), FUN_004d1260 (0x57),
        // FUN_004d0b10 (0x58), FUN_004d0bb0 (0x59), FUN_004d0ed0 (0x93),
        // FUN_004d0fb0 (0x94), FUN_004d1060 (0x95), FUN_004d1110 (0x96).
        case 0x56: case 0x57: case 0x58: case 0x59: case 0x93: case 0x94: case 0x95: case 0x96:
            break;
        case 0x9c: case 0x9d: {   // FUN_004d12d0 / FUN_004d1360 -> FUN_004e3170 / FUN_004e31c0
            const auto i = static_cast<std::int16_t>(read(operandAt(rec, 4)));
            if (slot_.op == 0x9c) logic::setSeen(work_, i);
            else logic::clearSeen(work_, i);
            break;
        }
        case 0xaa: {  // FUN_004d14a0: flag byte = src
            const std::int32_t v = read(operandAt(rec, 4)), i = read(operandAt(rec, 8));
            if (i >= 0 && i < 300) work_.set8(0x7a54ca + std::uint32_t(i), static_cast<std::uint8_t>(v));
            break;
        }
        case 0x5a: {  // FUN_004d0c80: dest = progress value
            std::int32_t v = 0;
            switch (read(operandAt(rec, 8))) {
                case 0: v = work_.month(); break;          // DAT_007a528e
                case 1: v = work_.week(); break;           // DAT_007a5290
                case 2: v = work_.day(); break;            // DAT_007a5292
                case 3: v = work_.s16(0x7e1180); break;
                case 4: v = work_.s16(0x7e1182); break;
            }
            write(operandAt(rec, 4), v);
            break;
        }
        case 0x5b: {  // FUN_004d0da0: progress value = src
            const auto v = static_cast<std::int16_t>(read(operandAt(rec, 4)));
            switch (read(operandAt(rec, 8))) {
                case 0: work_.setMonth(v); break;
                case 1: work_.setWeek(v); break;
                case 2: work_.setDay(v); break;
                case 3: work_.set16(0x7e1180, v); break;
                case 4: work_.set16(0x7e1182, v); break;
            }
            break;
        }
        case 0xa9: {  // FUN_004d13f0: dest = flag byte
            const std::int32_t i = read(operandAt(rec, 8));
            write(operandAt(rec, 4), i >= 0 && i < 300 ? work_.u8(0x7a54ca + std::uint32_t(i)) : 0);   // DAT_007a54ca
            break;
        }
        case 0xab: {  // FUN_004d1560: dest = index (1..11) of the highest unit score
            std::int32_t best = 0, index = 1;
            for (int i = 1; i < 0xc; ++i) {                // DAT_007cf468 + i * 0x124
                const std::int32_t score = work_.s32(GameWork::unitProgress(i) + 0x11c);
                if (best < score) { best = score; index = i; }
            }
            write(operandAt(rec, 4), index);
            break;
        }
        default:
            waitOverride_ = -1;
            host_.exec(*this, slot_.op, rec);
            if (waitOverride_ >= 0) {
                const int kind = waitOverride_;
                waitOverride_ = -1;
                return kind;
            }
            break;
    }
    return waitKind(slot_.op);
}

// FUN_004ce8f0 (the per-frame drawing it also does lives in the host).
bool YbcVm::tick() {
    if (!slot_.active) return false;
    switch (slot_.state) {
        case 0: {
            const YbcScript& f = slot_.file;
            const std::uint32_t at = f.block(0) + slot_.pc;
            slot_.op  = f.u16(at);
            slot_.len = f.u16(at + 2);
            if (slot_.op == 0x13) {
                endScript();
                return false;
            }
            ++executed_;
            const std::uint32_t rec = slot_.pc;
            if (SDL_getenv("CH_YBCTRACE")) SDL_Log("ybc: %04x op %02x", rec, slot_.op);
            const int kind = execute(rec);
            slot_.pc += slot_.len;
            slot_.state = static_cast<std::uint16_t>(kind);
            break;
        }
        case 1:
        case 3:
            if (fastForward_) {
                if (host_.skipWait(slot_.op)) slot_.state = 0;
                else fastForward_ = false;
            } else if (host_.waitDone(slot_.op)) {
                slot_.state = 0;
            }
            break;
        case 2:
            slot_.state = 0;
            break;
        case 4:
            if (!hold_ || fastForward_) slot_.state = 0;
            break;
        default:
            break;
    }
    return true;
}

} // namespace ch
