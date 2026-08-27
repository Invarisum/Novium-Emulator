#pragma once

#include "config.h"
#include "isa.h"

#include <array>
#include <cstdint>
#include <string>

namespace aiasm {

// ============================================================================
// Performance Profiler
//
// Tracks:
//   - Instruction count
//   - Total (estimated) clock cycles (per-opcode weights)
//   - Histogram of executed opcodes
//   - Total DMA byte throughput
// ============================================================================

class Profiler {
public:
    // --- State snapshot for serialization ---
    struct State {
        uint64_t instr_count;
        uint64_t cycle_count;
        uint64_t dma_bytes;
        uint64_t dma_cycles;
        std::array<uint64_t, NUM_OPCODES> opcode_hist;
    };

    void reset();

    // Called by the emulator after each executed instruction
    void record(Opcode op, uint32_t cycles, uint64_t dma_bytes = 0);

    // --- Accessors ---
    uint64_t instructions()     const noexcept { return instr_count_; }
    uint64_t cycles()           const noexcept { return cycle_count_; }
    uint64_t dma_bytes()        const noexcept { return dma_bytes_; }
    uint64_t dma_cycles()       const noexcept { return dma_cycle_count_; }

    double   ipc()              const noexcept;   // instructions per cycle
    double   dma_throughput()   const noexcept;   // bytes per DMA cycle

    const std::array<uint64_t, NUM_OPCODES>& opcode_histogram() const noexcept { return opcode_hist_; }

    // --- Serialization ---
    State save() const;
    void  restore(const State& s);

    std::string report() const;

private:
    uint64_t instr_count_      = 0;
    uint64_t cycle_count_      = 0;
    uint64_t dma_bytes_        = 0;
    uint64_t dma_cycle_count_  = 0;
    std::array<uint64_t, NUM_OPCODES> opcode_hist_{};
};

} // namespace aiasm
