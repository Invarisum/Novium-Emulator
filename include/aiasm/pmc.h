#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace aiasm {

// ============================================================================
// Performance Monitor Counters (PMCs)
//
// Virtual PMCs like Linux `perf`: IPC, cache hit rates, bandwidth
// saturation, pipeline stalls, TLB misses. Each counter can be read via
// CSR (e.g., CSR_CYCLE/INSTRET) or via API.
// ============================================================================

enum class PMCEvent : uint8_t {
    Cycles = 0, Instructions = 1, BranchMispredicts = 2, CacheMisses = 3,
    TLBMisses = 4, DMABandwidth = 5, WarpStalls = 6, SystolicTiles = 7,
    InterconnectStalls = 8, Count
};

struct PMCCounters {
    std::array<uint64_t, size_t(PMCEvent::Count)> ctr{};

    uint64_t& operator[](PMCEvent e) noexcept { return ctr[size_t(e)]; }
    uint64_t operator[](PMCEvent e) const noexcept { return ctr[size_t(e)]; }

    double ipc() const noexcept {
        uint64_t c = ctr[size_t(PMCEvent::Cycles)];
        uint64_t i = ctr[size_t(PMCEvent::Instructions)];
        return c ? double(i)/double(c) : 0;
    }
    double cache_miss_rate(uint64_t hits, uint64_t misses) const noexcept {
        uint64_t tot = hits + misses;
        return tot ? double(misses)/double(tot) : 0;
    }
};

class PMC {
public:
    void reset() { ctr_.ctr.fill(0); }
    void inc(PMCEvent e, uint64_t v = 1) noexcept { ctr_[e] += v; }
    void set(PMCEvent e, uint64_t v) noexcept { ctr_[e] = v; }
    uint64_t get(PMCEvent e) const noexcept { return ctr_[e]; }
    const PMCCounters& counters() const noexcept { return ctr_; }

    // Convenience updaters called from Emulator
    void on_commit(uint32_t cycles, bool is_branch_mispredict, bool is_cache_miss);
    void on_tlb_miss() noexcept { inc(PMCEvent::TLBMisses); }
    void on_dma(uint32_t bytes) noexcept { inc(PMCEvent::DMABandwidth, bytes); }

    std::string report(uint64_t l1_hits, uint64_t l1_misses, uint64_t tlb_hits, uint64_t tlb_misses) const {
        char buf[512];
        uint64_t tot = l1_hits + l1_misses;
        double rate = tot ? double(l1_misses)/double(tot)*100 : 0;
        std::snprintf(buf, sizeof(buf),
            "PMCs IPC=%.3f cyc=%llu instr=%llu br_miss=%llu cache_miss_rate=%.1f%% tlb_miss=%llu dma_B=%llu warp_stall=%llu tiles=%llu",
            ctr_.ipc(),
            (unsigned long long)ctr_[PMCEvent::Cycles], (unsigned long long)ctr_[PMCEvent::Instructions],
            (unsigned long long)ctr_[PMCEvent::BranchMispredicts],
            rate,
            (unsigned long long)ctr_[PMCEvent::TLBMisses],
            (unsigned long long)ctr_[PMCEvent::DMABandwidth],
            (unsigned long long)ctr_[PMCEvent::WarpStalls],
            (unsigned long long)ctr_[PMCEvent::SystolicTiles]);
        (void)tlb_hits; (void)tlb_misses;
        return buf;
    }

private:
    PMCCounters ctr_{};
};

} // namespace aiasm
