#pragma once

#include "config.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Parallel Execution Mapping — GPU / ASIC
//
// GPU: Warp/Wavefront scheduler, SIMT lanes, unified memory
// ASIC: Systolic array, DMA engines, fixed-function DSP
// ============================================================================

// --- GPU: Warp scheduler (32 threads / warp, like NVIDIA) ---
struct WarpConfig {
    uint32_t warp_size = 32;
    uint32_t max_warps_per_sm = 32;
    uint32_t num_sms = 4;
};

class WarpScheduler {
public:
    explicit WarpScheduler(WarpConfig cfg = {}) : cfg_(cfg) { reset(); }

    void configure(WarpConfig c) { cfg_ = c; reset(); }

    // Schedule next warp (round-robin GTO); returns warp id or -1 if stalled
    int schedule();

    // Warp state
    void mark_ready(int warp_id) noexcept { if (valid(warp_id)) state_[warp_id]=0; }
    void mark_stalled(int warp_id, uint32_t cycles) noexcept { if (valid(warp_id)) { state_[warp_id]=cycles; stalls_++; } }
    void tick(uint32_t cycles) noexcept;

    uint32_t active_warps() const noexcept;
    uint64_t scheduled() const noexcept { return scheduled_; }
    uint64_t stalls() const noexcept { return stalls_; }

    void reset() { state_.assign(cfg_.max_warps_per_sm * cfg_.num_sms, 0); scheduled_=0; stalls_=0; rr_=0; }

    std::string report() const {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "warp scheduler %ux%u active=%u scheduled=%llu stalls=%llu",
            cfg_.num_sms, cfg_.max_warps_per_sm, active_warps(),
            (unsigned long long)scheduled_, (unsigned long long)stalls_);
        return buf;
    }

private:
    bool valid(int id) const noexcept { return id>=0 && size_t(id)<state_.size(); }
    WarpConfig cfg_{};
    std::vector<uint32_t> state_; // 0=ready, >0=stall cycles remaining
    uint64_t scheduled_ = 0, stalls_ = 0;
    size_t rr_ = 0;
};

// --- ASIC: Systolic array (weight-stationary) ---
struct SystolicConfig {
    uint32_t rows = 8, cols = 8;
    uint32_t macs_per_cycle = 64;
};

class SystolicArray {
public:
    explicit SystolicArray(SystolicConfig cfg = {}) : cfg_(cfg) {}

    void configure(SystolicConfig c) noexcept { cfg_ = c; }

    // Enqueue a TILE matmul; returns cycles for this tile (pipelined)
    uint32_t enqueue_tile(uint32_t tile_n);

    uint64_t tiles() const noexcept { return tiles_; }
    uint64_t total_cycles() const noexcept { return total_cycles_; }

    void reset() { tiles_=0; total_cycles_=0; pipeline_depth_=0; }

    std::string report() const {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "systolic %ux%u tiles=%llu cycles=%llu",
            cfg_.rows, cfg_.cols, (unsigned long long)tiles_, (unsigned long long)total_cycles_);
        return buf;
    }

private:
    SystolicConfig cfg_{};
    uint64_t tiles_ = 0, total_cycles_ = 0;
    uint32_t pipeline_depth_ = 0;
};

// Unified parallel mapping (owned by Emulator when target is GPU/ASIC)
struct ParallelMapping {
    WarpScheduler warp;
    SystolicArray systolic;
    // DMA engines count as parallel fixed-function
    uint32_t dma_engines = 2;
};

} // namespace aiasm
