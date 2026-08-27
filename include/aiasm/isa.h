#pragma once

#include "config.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace aiasm {

// ============================================================================
// Opcode enumeration
// ============================================================================

enum class Opcode : uint8_t {
    // Control & Flow
    NOP         = 0x00,
    HALT        = 0x01,
    JMP         = 0x02,
    BEQ         = 0x03,
    BNE         = 0x04,

    // Scalar ops
    ADD         = 0x05,
    SUB         = 0x06,
    MUL         = 0x07,
    LOAD        = 0x08,
    STORE       = 0x09,
    MOV         = 0x0A,     // register-to-register move
    MOVI        = 0x0B,     // load immediate (pseudo: MOV rd, #imm)
    LOAD_VR     = 0x0C,     // load vector register from scratchpad
    STORE_VR    = 0x0D,     // store vector register to scratchpad

    // Tensor / SIMD
    VEC_ADD     = 0x10,
    MATMUL_TILE = 0x11,
    DMA_LOAD    = 0x12,
    DMA_STORE   = 0x13,
};

// Bitmask type for quick opcode lookup (dynamic bitmasking requirement)
using OpcodeMask = uint32_t;
inline constexpr OpcodeMask opcode_bit(Opcode op) {
    return OpcodeMask{1} << static_cast<uint8_t>(op);
}

// ============================================================================
// Instruction word (32-bit fixed-length)
//
// R-type (register-register ops):
//   [31:26] opcode | [25:21] rd | [20:16] ra | [15:11] rb | [10:0] func
//
// I-type (immediate ops):
//   [31:26] opcode | [25:21] rd | [20:16] ra | [15:0]  imm16
//
// J-type (jump):
//   [31:26] opcode | [25:0]  target26
//
// B-type (branch):
//   [31:26] opcode | [25:21] ra | [20:16] rb | [15:0] offset16
// ============================================================================

struct Instruction {
    uint32_t raw = 0;

    // --- Portable field accessors (manual bit extraction) ---
    Opcode    opcode()  const noexcept { return static_cast<Opcode>((raw >> OPCODE_SHIFT) & OPCODE_MASK); }
    uint8_t   rd()      const noexcept { return (raw >> RD_SHIFT) & RD_MASK; }
    uint8_t   ra()      const noexcept { return (raw >> RA_SHIFT) & RA_MASK; }
    uint8_t   rb()      const noexcept { return (raw >> RB_SHIFT) & RB_MASK; }
    uint16_t  func()    const noexcept { return raw & FUNC_MASK; }
    uint16_t  imm16()   const noexcept { return raw & IMM_MASK; }
    int32_t   simm16()  const noexcept { return static_cast<int16_t>(raw & IMM_MASK); }
    uint32_t  target()  const noexcept { return raw & TARGET_MASK; }
    int32_t   offset()  const noexcept { return static_cast<int16_t>(raw & IMM_MASK); }

    // --- Union for alternative decoding views (used by debugger / disassembler) ---
    union Format {
        uint32_t word;
        struct { uint32_t opcode : 6; uint32_t rd : 5; uint32_t ra : 5; uint32_t rb : 5; uint32_t func : 11; } rtype;
        struct { uint32_t opcode : 6; uint32_t rd : 5; uint32_t ra : 5; uint32_t imm  : 16; } itype;
        struct { uint32_t opcode : 6; uint32_t target : 26; } jtype;
        struct { uint32_t opcode : 6; uint32_t rs : 5; uint32_t rt : 5; uint32_t offset : 16; } btype;
    };

    Format format() const noexcept { return Format{.word = raw}; }
};

// ============================================================================
// Instruction encoder helpers
// ============================================================================

namespace enc {

    inline uint32_t rtype(Opcode op, uint8_t rd_, uint8_t ra_, uint8_t rb_, uint16_t func_ = 0) noexcept {
        return (static_cast<uint32_t>(op) << OPCODE_SHIFT)
             | ((static_cast<uint32_t>(rd_) & RD_MASK) << RD_SHIFT)
             | ((static_cast<uint32_t>(ra_) & RA_MASK) << RA_SHIFT)
             | ((static_cast<uint32_t>(rb_) & RB_MASK) << RB_SHIFT)
             | (static_cast<uint32_t>(func_) & FUNC_MASK);
    }

    inline uint32_t itype(Opcode op, uint8_t rd_, uint8_t ra_, uint16_t imm) noexcept {
        return (static_cast<uint32_t>(op) << OPCODE_SHIFT)
             | ((static_cast<uint32_t>(rd_) & RD_MASK) << RD_SHIFT)
             | ((static_cast<uint32_t>(ra_) & RA_MASK) << RA_SHIFT)
             | (static_cast<uint32_t>(imm) & IMM_MASK);
    }

    inline uint32_t jtype(Opcode op, uint32_t addr) noexcept {
        return (static_cast<uint32_t>(op) << OPCODE_SHIFT)
             | (addr & TARGET_MASK);
    }

    inline uint32_t btype(Opcode op, uint8_t rs_, uint8_t rt_, int16_t off) noexcept {
        return (static_cast<uint32_t>(op) << OPCODE_SHIFT)
             | ((static_cast<uint32_t>(rs_) & RA_MASK) << RD_SHIFT)     // bits 25-21
             | ((static_cast<uint32_t>(rt_) & RA_MASK) << RA_SHIFT)     // bits 20-16
             | (static_cast<uint32_t>(off) & IMM_MASK);                  // bits 15-0
    }

    // Convenience instruction constructors
    // NOLINTNEXTLINE(google-explicit-conversions)
    inline uint32_t make(Opcode op) { return static_cast<uint32_t>(op) << OPCODE_SHIFT; }

    inline uint32_t nop()              { return make(Opcode::NOP); }
    inline uint32_t halt()             { return make(Opcode::HALT); }
    inline uint32_t jmp(uint32_t addr) { return jtype(Opcode::JMP, addr); }
    inline uint32_t beq(uint8_t ra_, uint8_t rb_, int16_t off) { return btype(Opcode::BEQ, ra_, rb_, off); }
    inline uint32_t bne(uint8_t ra_, uint8_t rb_, int16_t off) { return btype(Opcode::BNE, ra_, rb_, off); }
    inline uint32_t add(uint8_t rd_, uint8_t ra_, uint8_t rb_)  { return rtype(Opcode::ADD, rd_, ra_, rb_); }
    inline uint32_t sub(uint8_t rd_, uint8_t ra_, uint8_t rb_)  { return rtype(Opcode::SUB, rd_, ra_, rb_); }
    inline uint32_t mul(uint8_t rd_, uint8_t ra_, uint8_t rb_)  { return rtype(Opcode::MUL, rd_, ra_, rb_); }
    inline uint32_t mov(uint8_t rd_, uint8_t ra_)               { return rtype(Opcode::MOV, rd_, ra_, 0); }
    inline uint32_t movi(uint8_t rd_, uint16_t imm)             { return itype(Opcode::MOVI, rd_, 0, imm); }
    // LOAD: rd = mem[r[ra] + imm]
    inline uint32_t load(uint8_t rd_, uint8_t ra_, uint16_t imm) { return itype(Opcode::LOAD, rd_, ra_, imm); }
    // STORE: mem[r[ra] + imm] = r[rs]  (rs encoded in rd field)
    inline uint32_t store(uint8_t rs_, uint8_t ra_, uint16_t imm) { return itype(Opcode::STORE, rs_, ra_, imm); }
    // LOAD_VR: vr = mem[r[ra] + imm]
    inline uint32_t load_vr(uint8_t vr_, uint8_t ra_, uint16_t imm) { return itype(Opcode::LOAD_VR, vr_, ra_, imm); }
    // STORE_VR: mem[r[ra] + imm] = vr
    inline uint32_t store_vr(uint8_t vr_, uint8_t ra_, uint16_t imm) { return itype(Opcode::STORE_VR, vr_, ra_, imm); }
    // VEC_ADD: vd = va + vb (element-wise)
    inline uint32_t vec_add(uint8_t vd_, uint8_t va_, uint8_t vb_) { return rtype(Opcode::VEC_ADD, vd_, va_, vb_); }
    // MATMUL_TILE: vd = va * vb (N x N matrix multiply, N in func)
    inline uint32_t matmul_tile(uint8_t vd_, uint8_t va_, uint8_t vb_, uint8_t n) { return rtype(Opcode::MATMUL_TILE, vd_, va_, vb_, n); }
    // DMA_LOAD: scratchpad[r[rd]] = main_ram[r[ra] .. r[ra]+func-1]
    inline uint32_t dma_load(uint8_t rd_, uint8_t ra_, uint16_t bytes) { return rtype(Opcode::DMA_LOAD, rd_, ra_, 0, bytes); }
    // DMA_STORE: main_ram[r[rd]] = scratchpad[r[ra] .. r[ra]+func-1]
    inline uint32_t dma_store(uint8_t rd_, uint8_t ra_, uint16_t bytes) { return rtype(Opcode::DMA_STORE, rd_, ra_, 0, bytes); }

} // namespace enc

// ============================================================================
// Cycle weights
// ============================================================================

inline uint32_t cycle_weight(Opcode op, uint16_t func = 0) noexcept {
    switch (op) {
        case Opcode::NOP:         return 1;
        case Opcode::HALT:        return 0;
        case Opcode::JMP:         return 2;
        case Opcode::BEQ:
        case Opcode::BNE:         return 2;     // 2 base; +1 if taken (handled in execute)
        case Opcode::ADD:
        case Opcode::SUB:         return 1;
        case Opcode::MUL:         return 3;
        case Opcode::LOAD:
        case Opcode::STORE:       return 3;
        case Opcode::MOV:
        case Opcode::MOVI:        return 1;
        case Opcode::LOAD_VR:
        case Opcode::STORE_VR:    return 5;     // 64 bytes transferred
        case Opcode::VEC_ADD:     return 4;     // 16 elements, 4-wide SIMD
        case Opcode::MATMUL_TILE: return 16;    // 4x4x4=64 MACs, 4/cycle
        case Opcode::DMA_LOAD:
        case Opcode::DMA_STORE: {
            uint32_t bytes = func;
            uint32_t lines = (bytes + DMA_LINE_SIZE - 1) / DMA_LINE_SIZE;
            return static_cast<uint32_t>(lines) + 2;  // +2 overhead
        }
    }
    return 1;
}

// ============================================================================
// Disassembler
// ============================================================================

inline std::string disassemble(const Instruction& instr) {
    const Opcode op = instr.opcode();
    char buf[128];

    switch (op) {
        case Opcode::NOP:         return "NOP";
        case Opcode::HALT:        return "HALT";
        case Opcode::JMP:         std::snprintf(buf, sizeof(buf), "JMP  0x%08X", instr.target()); return buf;
        case Opcode::BEQ:         std::snprintf(buf, sizeof(buf), "BEQ  r%d, r%d, %d", instr.rd(), instr.ra(), instr.offset()); return buf;
        case Opcode::BNE:         std::snprintf(buf, sizeof(buf), "BNE  r%d, r%d, %d", instr.rd(), instr.ra(), instr.offset()); return buf;
        case Opcode::ADD:         std::snprintf(buf, sizeof(buf), "ADD  r%d, r%d, r%d", instr.rd(), instr.ra(), instr.rb()); return buf;
        case Opcode::SUB:         std::snprintf(buf, sizeof(buf), "SUB  r%d, r%d, r%d", instr.rd(), instr.ra(), instr.rb()); return buf;
        case Opcode::MUL:         std::snprintf(buf, sizeof(buf), "MUL  r%d, r%d, r%d", instr.rd(), instr.ra(), instr.rb()); return buf;
        case Opcode::LOAD:        std::snprintf(buf, sizeof(buf), "LOAD r%d, r%d, %d", instr.rd(), instr.ra(), instr.simm16()); return buf;
        case Opcode::STORE:       std::snprintf(buf, sizeof(buf), "STORE r%d, r%d, %d", instr.rd(), instr.ra(), instr.simm16()); return buf;
        case Opcode::MOV:         std::snprintf(buf, sizeof(buf), "MOV  r%d, r%d", instr.rd(), instr.ra()); return buf;
        case Opcode::MOVI:        std::snprintf(buf, sizeof(buf), "MOVI r%d, #%d", instr.rd(), instr.imm16()); return buf;
        case Opcode::LOAD_VR:     std::snprintf(buf, sizeof(buf), "LOAD_VR  v%d, r%d, %d", instr.rd(), instr.ra(), instr.simm16()); return buf;
        case Opcode::STORE_VR:    std::snprintf(buf, sizeof(buf), "STORE_VR v%d, r%d, %d", instr.rd(), instr.ra(), instr.simm16()); return buf;
        case Opcode::VEC_ADD:     std::snprintf(buf, sizeof(buf), "VEC_ADD  v%d, v%d, v%d", instr.rd(), instr.ra(), instr.rb()); return buf;
        case Opcode::MATMUL_TILE: std::snprintf(buf, sizeof(buf), "MATMUL_TILE v%d, v%d, v%d, N=%d", instr.rd(), instr.ra(), instr.rb(), instr.func()); return buf;
        case Opcode::DMA_LOAD:    std::snprintf(buf, sizeof(buf), "DMA_LOAD  r%d, r%d, %d bytes", instr.rd(), instr.ra(), instr.func()); return buf;
        case Opcode::DMA_STORE:   std::snprintf(buf, sizeof(buf), "DMA_STORE r%d, r%d, %d bytes", instr.rd(), instr.ra(), instr.func()); return buf;
    }
    return "???";
}

} // namespace aiasm
