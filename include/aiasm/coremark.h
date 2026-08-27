#pragma once

#include "emulator.h"
#include "isa.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace aiasm::coremark {

// ============================================================================
// CoreMark-like suite for Novium — list / matrix / state / CRC
// Mapped to guest ISA so the same suite runs on CPU / GPU / ASIC targets
// and reports CoreMarks/MHz in a target-accurate way.
// ============================================================================

struct Result {
    uint32_t iterations = 0;
    uint64_t total_cycles = 0;
    uint64_t total_instrs = 0;
    double wall_us = 0;
    double coremark_per_mhz = 0; // iterations / (cycles / 1e6)  — scaled
    double iterations_per_sec = 0;
    bool crc_ok = false;
    bool list_ok = false;
    bool matrix_ok = false;
    std::string target_name;
    std::string host_info;
};

// Build a single CoreMark iteration as a guest program.
// Workload per iteration:
//   list:   scalar loop sum 1..N (ADD/BNE)
//   matrix: DMA + LOAD_VR + MATMUL_TILE + STORE_VR + DMA_STORE (uses matmul)
//   state:  CRC16 over data (ADD/XOR via host, modeled as MUL/ADD)
//   crc:    final CRC check
inline std::vector<uint32_t> build_iteration(uint32_t scratch_a = 0x1000, uint32_t scratch_b = 0x1040) {
    return {
        // list: sum 1..5
        enc::movi(1,0), enc::movi(2,5), enc::movi(4,1), enc::movi(3,0),
        enc::add(1,1,4), enc::add(3,3,1), enc::bne(1,2,uint16_t(-12)),
        // matrix: dma + matmul
        enc::movi(5, uint16_t(scratch_a)), enc::movi(6, uint16_t(scratch_b)),
        enc::movi(7,0), enc::movi(8,64),
        enc::dma_load(5,7,64), enc::dma_load(6,8,64),
        enc::load_vr(0,5,0), enc::load_vr(1,6,0),
        enc::vec_add(2,0,1),
        enc::matmul_tile(3,0,1,4),
        enc::store_vr(3,5,0),
        // crc/state modeled as scalar ALU
        enc::movi(10, 0x5A5A), enc::add(11,10,3), enc::mul(12,11,3),
        enc::halt()
    };
}

// Run N iterations on `emu` (emu must have target already set and main RAM
// preloaded with matrices). Returns Result. `use_fast` selects run_fast().
Result run(Emulator& emu, uint32_t iterations, bool use_fast, const float* mat_a, const float* mat_b);

inline std::string report(const Result& r) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "CoreMark [%s on %s]\n"
        "  iterations      : %u\n"
        "  total_cycles    : %llu\n"
        "  total_instrs    : %llu\n"
        "  wall_time       : %.1f us\n"
        "  iter/sec        : %.1f\n"
        "  CoreMark/MHz    : %.3f\n"
        "  checks: list=%s matrix=%s crc=%s  %s\n",
        r.target_name.c_str(), r.host_info.c_str(),
        r.iterations,
        (unsigned long long)r.total_cycles,
        (unsigned long long)r.total_instrs,
        r.wall_us,
        r.iterations_per_sec,
        r.coremark_per_mhz,
        r.list_ok ? "PASS" : "FAIL",
        r.matrix_ok ? "PASS" : "FAIL",
        r.crc_ok ? "PASS" : "FAIL",
        (r.list_ok && r.matrix_ok && r.crc_ok) ? "OVERALL PASS" : "OVERALL FAIL");
    return buf;
}

} // namespace aiasm::coremark
