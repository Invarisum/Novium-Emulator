#pragma once

#include "config.h"

#include <array>
#include <cstdint>

namespace aiasm {

// ============================================================================
// Register File
//
//   16× 32-bit GPRs (r0 hardwired to 0)
//    8× Vector registers, each VEC_WIDTH floats
//    PC, HALT flag, STATUS flags (Zero, Negative, Overflow)
// ============================================================================

struct RegisterFile {
    std::array<uint32_t, NUM_GPR> gpr{};              // r0-r15
    std::array<std::array<float, VEC_WIDTH>, NUM_VR> vr{};  // v0-v7

    uint32_t pc        = 0;
    bool     halt      = false;

    struct StatusFlags {
        bool zero     : 1 {false};
        bool negative : 1 {false};
        bool overflow : 1 {false};
    } status{};

    // --- GPR accessors (r0 always reads / writes zero) ---
    uint32_t read_gpr(uint8_t idx) const noexcept {
        return (idx == 0) ? 0u : gpr[idx];
    }

    void write_gpr(uint8_t idx, uint32_t value) noexcept {
        if (idx != 0) gpr[idx] = value;
        gpr[0] = 0;  // hardwired
    }

    // --- VR accessors ---
    const float* read_vr(uint8_t idx) const noexcept { return vr[idx].data(); }
    float*       write_vr(uint8_t idx) noexcept     { return vr[idx].data(); }

    // --- Status flag helpers ---
    void update_flags_u32(uint32_t result) noexcept {
        status.zero      = (result == 0);
        status.negative  = ((result >> 31) & 1) == 1;
        // Overflow for unsigned is undefined; for signed add/sub the executor
        // should set overflow explicitly.
        status.overflow  = false;
    }

    void update_flags_s32(int32_t result) noexcept {
        status.zero      = (result == 0);
        status.negative  = (result < 0);
        status.overflow  = false;
    }

    void reset() {
        gpr.fill(0);
        vr.fill({});
        pc    = 0;
        halt  = false;
        status = {};
    }
};

} // namespace aiasm
