#pragma once

#include "config.h"
#include "isa.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// ISA Translation Engine — Decoder & Executor
//
// Translates guest instructions from multiple target ISAs (ARM64, RISC-V,
// CUDA PTX, custom ASIC microcode) into Novium host actions. Mirrors QEMU
// TCG: frontend decodes guest bytes → IR (our Instruction), backend either
// interprets (switch dispatch) or JITs to host code via translate.h BlockCache.
//
// Usage:
//   TranslationEngine eng(GuestISA::RISCV64);
//   auto prog = eng.translate_bytes(riscv_elf_bytes); // → vector<Instruction>
//   emu.load_program(prog.data(), prog.size());
// ============================================================================

enum class GuestISA : uint8_t {
    Native = 0,  // already Novium ISA
    ARM64  = 1,
    RISCV64= 2,
    PTX    = 3,  // CUDA PTX (parallel thread execution)
    ASIC   = 4,  // custom microcode
};

inline const char* guest_isa_name(GuestISA isa) noexcept {
    switch (isa) {
        case GuestISA::Native:  return "novium-native";
        case GuestISA::ARM64:   return "arm64";
        case GuestISA::RISCV64: return "riscv64";
        case GuestISA::PTX:     return "ptx";
        case GuestISA::ASIC:    return "asic-ucode";
    }
    return "unknown";
}

struct TranslationStats {
    uint64_t decoded = 0;
    uint64_t emitted = 0;
    uint64_t folded  = 0; // e.g. ARM64 LDP → single LOAD_VR
};

class TranslationEngine {
public:
    explicit TranslationEngine(GuestISA isa = GuestISA::Native) : isa_(isa) {}

    void set_isa(GuestISA isa) noexcept { isa_ = isa; }
    GuestISA isa() const noexcept { return isa_; }

    // Decode raw guest bytes → Novium Instructions (interpreter path)
    std::vector<uint32_t> translate_bytes(const uint8_t* data, size_t size, TranslationStats* stats = nullptr) const;
    std::vector<uint32_t> translate_bytes(const std::vector<uint8_t>& data, TranslationStats* stats = nullptr) const {
        return translate_bytes(data.data(), data.size(), stats);
    }

    // Single-instruction decoders (used by step debuggers)
    bool translate_one_arm64(uint32_t word, std::vector<uint32_t>& out) const;
    bool translate_one_riscv(uint32_t word, std::vector<uint32_t>& out) const;
    bool translate_one_ptx(const std::string& line, std::vector<uint32_t>& out) const;

private:
    GuestISA isa_ = GuestISA::Native;
};

} // namespace aiasm
