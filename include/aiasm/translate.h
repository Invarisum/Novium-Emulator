#pragma once

#include "config.h"
#include "isa.h"
#include "host.h"
#include "target.h"
#include "profiler.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace aiasm {

// ============================================================================
// Translation layer — guest ISA → host execution
//
// Fast path: decoded block cache (JIT-style). Instead of re-decoding every
// fetch, we translate linear basic blocks once and replay them. On x86_64
// the hot kernels (VEC_ADD/MATMUL/DMA) are further lowered to host SIMD
// via host::vec_add_host etc. On ARM the same guest maps to NEON/SVE.
//
// Accuracy: translation is semantics-preserving. Cycle counts come from
// TargetProfile so timing stays target-accurate even when host runs faster.
// ============================================================================

struct DecodedBlock {
    uint32_t start_pc = 0;
    uint32_t end_pc   = 0; // exclusive
    std::vector<Instruction> ops;
    bool ends_with_branch = false;
    bool ends_with_halt   = false;
};

class BlockCache {
public:
    void clear() { map_.clear(); hits_ = 0; misses_ = 0; }

    const DecodedBlock* find(uint32_t pc) const {
        auto it = map_.find(pc);
        return it == map_.end() ? nullptr : &it->second;
    }

    // Decode a linear block starting at pc until branch/halt or max_len.
    // Reads directly from backing memory (caller ensures bounds).
    template <typename Mem>
    const DecodedBlock& translate(const Mem& mem, uint32_t pc, size_t max_len = 32) {
        auto it = map_.find(pc);
        if (it != map_.end()) { ++hits_; return it->second; }
        ++misses_;

        DecodedBlock blk;
        blk.start_pc = pc;
        uint32_t cur = pc;
        for (size_t i = 0; i < max_len; ++i) {
            uint32_t raw = mem.read32(cur);
            Instruction ins{raw};
            blk.ops.push_back(ins);
            cur += 4;
            Opcode op = ins.opcode();
            if (op == Opcode::HALT || op == Opcode::JMP ||
                op == Opcode::BEQ || op == Opcode::BNE) {
                blk.ends_with_branch = (op != Opcode::HALT);
                blk.ends_with_halt = (op == Opcode::HALT);
                break;
            }
            // Stop if next PC would cross cache line / page
            if ((cur & 0x3F) == 0) break;
        }
        blk.end_pc = cur;
        auto [ins_it, _] = map_.emplace(pc, std::move(blk));
        return ins_it->second;
    }

    size_t size() const noexcept { return map_.size(); }
    uint64_t hits() const noexcept { return hits_; }
    uint64_t misses() const noexcept { return misses_; }
    double hit_rate() const noexcept {
        uint64_t tot = hits_ + misses_;
        return tot ? double(hits_) / double(tot) : 0.0;
    }

private:
    std::unordered_map<uint32_t, DecodedBlock> map_;
    uint64_t hits_ = 0, misses_ = 0;
};

// Host execution helpers — called by emulator hot paths
namespace translate {

inline uint32_t exec_vec_add(float* dst, const float* a, const float* b,
                             const TargetProfile& prof, Profiler* p = nullptr) {
    (void)prof;
    host::vec_add_host(dst, a, b);
    uint32_t cyc = profile_cycle_weight(prof, Opcode::VEC_ADD);
    if (p) p->record(Opcode::VEC_ADD, cyc, 0);
    return cyc;
}

inline uint32_t exec_matmul_tile(float* d, const float* a, const float* b,
                                 uint8_t n, const TargetProfile& prof, Profiler* p = nullptr) {
    host::matmul_tile_host(d, a, b, n);
    uint32_t cyc = profile_cycle_weight(prof, Opcode::MATMUL_TILE, n);
    if (p) p->record(Opcode::MATMUL_TILE, cyc, 0);
    return cyc;
}

} // namespace translate

} // namespace aiasm
