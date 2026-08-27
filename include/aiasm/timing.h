#pragma once

#include <cstdint>
#include <string>

namespace aiasm {

// ============================================================================
// Execution & Timing Accuracy Controls
//
// Cycle-Accurate vs Cycle-Approximate vs Functional (fast) modes.
// In functional mode, canonically 1 cycle per instruction (like QEMU -accel tcg).
// In cycle-approximate, uses isa.h:cycle_weight() (fast, ~10% error).
// In cycle-accurate, models pipeline stalls, branch mispredicts, cache misses,
// and memory latency via Pipeline + Cache + Interconnect.
// ============================================================================

enum class TimingMode : uint8_t { Functional = 0, CycleApproximate = 1, CycleAccurate = 2 };

inline const char* timing_mode_name(TimingMode m) noexcept {
    switch (m) {
        case TimingMode::Functional:      return "functional";
        case TimingMode::CycleApproximate:return "cycle-approximate";
        case TimingMode::CycleAccurate:   return "cycle-accurate";
    }
    return "unknown";
}

struct TimingConfig {
    TimingMode mode = TimingMode::CycleApproximate;
    bool model_branch_mispredict = true;
    bool model_cache_miss = true;
    bool model_memory_latency = true;
    uint32_t branch_mispredict_penalty = 3; // cycles flushed on taken branch mispredict
    uint32_t cache_miss_penalty = 10;       // L1 miss
    uint32_t memory_latency = 100;          // DDR5/HBM
};

class TimingController {
public:
    explicit TimingController(TimingConfig cfg = {}) : cfg_(cfg) {}

    void set_mode(TimingMode m) noexcept { cfg_.mode = m; }
    TimingMode mode() const noexcept { return cfg_.mode; }
    const TimingConfig& config() const noexcept { return cfg_; }
    void configure(TimingConfig c) noexcept { cfg_ = c; }

    // Adjust base cycles for microarchitectural effects
    uint32_t adjust(uint32_t base_cycles, bool branch_taken, bool branch_mispredict,
                    bool cache_miss, bool is_memory_op) const noexcept {
        if (cfg_.mode == TimingMode::Functional) return 1;
        if (cfg_.mode == TimingMode::CycleApproximate) return base_cycles;
        uint32_t c = base_cycles;
        if (cfg_.model_branch_mispredict && branch_mispredict) c += cfg_.branch_mispredict_penalty;
        if (cfg_.model_cache_miss && cache_miss) c += cfg_.cache_miss_penalty;
        if (cfg_.model_memory_latency && is_memory_op) c += (cfg_.memory_latency / 10);
        (void)branch_taken;
        return c;
    }

    std::string describe() const {
        return std::string("timing=") + timing_mode_name(cfg_.mode)
            + (cfg_.model_branch_mispredict ? " +branch" : "")
            + (cfg_.model_cache_miss ? " +cache" : "");
    }

private:
    TimingConfig cfg_{};
};

} // namespace aiasm
