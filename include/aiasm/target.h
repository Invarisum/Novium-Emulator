#pragma once

#include "config.h"
#include "isa.h"

#include <array>
#include <cstdint>
#include <string>

namespace aiasm {

// ============================================================================
// Simulation targets — CPU / GPU / custom ASIC profiles
// Each target defines its own cycle model, vector width, and parallelism.
// The same guest ISA runs on all three; only timing/throughput changes.
// ============================================================================

enum class TargetKind : uint8_t { CPU, GPU, ASIC };

struct TargetProfile {
    TargetKind kind = TargetKind::ASIC;
    std::string name;
    // Override cycle weights per opcode (0 = use default from isa.h)
    std::array<uint32_t, NUM_OPCODES> cycle_overrides{};
    // Parallelism hints for host translation
    uint32_t vector_lanes   = 4;   // SIMD lanes per cycle
    uint32_t tensor_cores   = 1;   // how many MATMUL tiles in parallel
    uint32_t dma_bandwidth  = 64;  // bytes per cycle (for DMA throughput model)
    bool     out_of_order   = false;
    bool     warp_scheduling = false; // GPU-style
};

inline TargetProfile make_cpu_profile() {
    TargetProfile p;
    p.kind = TargetKind::CPU;
    p.name = "cpu-x86-arm";
    p.vector_lanes = 8;   // AVX2/NEON 8-wide
    p.tensor_cores = 1;
    p.dma_bandwidth = 32;
    p.out_of_order = true;
    // CPU: scalar ops cheap, vector modest
    p.cycle_overrides.fill(0);
    p.cycle_overrides[static_cast<uint8_t>(Opcode::VEC_ADD)] = 2;
    p.cycle_overrides[static_cast<uint8_t>(Opcode::MATMUL_TILE)] = 24;
    return p;
}

inline TargetProfile make_gpu_profile() {
    TargetProfile p;
    p.kind = TargetKind::GPU;
    p.name = "gpu-warp32";
    p.vector_lanes = 32;  // warp
    p.tensor_cores = 4;   // 4 tiles in parallel
    p.dma_bandwidth = 256;
    p.warp_scheduling = true;
    p.cycle_overrides.fill(0);
    p.cycle_overrides[static_cast<uint8_t>(Opcode::VEC_ADD)] = 1;
    p.cycle_overrides[static_cast<uint8_t>(Opcode::MATMUL_TILE)] = 4; // tensor cores
    p.cycle_overrides[static_cast<uint8_t>(Opcode::DMA_LOAD)] = 1;
    p.cycle_overrides[static_cast<uint8_t>(Opcode::DMA_STORE)] = 1;
    return p;
}

inline TargetProfile make_asic_profile() {
    TargetProfile p;
    p.kind = TargetKind::ASIC;
    p.name = "asic-npu-4x4";
    p.vector_lanes = 16;
    p.tensor_cores = 1;
    p.dma_bandwidth = 64;
    p.cycle_overrides.fill(0);
    // Use defaults from isa.h (cycle_weight) — ASIC is the reference model
    return p;
}

inline TargetProfile make_custom_profile(std::string name,
                                         uint32_t lanes,
                                         uint32_t cores,
                                         uint32_t bw) {
    TargetProfile p = make_asic_profile();
    p.name = std::move(name);
    p.vector_lanes = lanes;
    p.tensor_cores = cores;
    p.dma_bandwidth = bw;
    return p;
}

inline uint32_t profile_cycle_weight(const TargetProfile& prof, Opcode op, uint16_t func = 0) noexcept {
    uint32_t ov = prof.cycle_overrides[static_cast<uint8_t>(op)];
    if (ov != 0) return ov;
    return cycle_weight(op, func);
}

} // namespace aiasm
