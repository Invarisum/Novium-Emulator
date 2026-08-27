#pragma once

#include "isa.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>

namespace aiasm {

// ============================================================================
// Pipeline & Execution Unit Simulation
//
// Models fetch→decode→execute→writeback, reservation stations, OoO queues,
// register renaming and branch prediction. In cycle-approximate mode the
// pipeline is single-cycle; in cycle-accurate it injects stalls.
// ============================================================================

enum class PipelineStage : uint8_t { Fetch, Decode, Execute, Writeback, Commit };

struct ReservationStation {
    bool occupied = false;
    Instruction instr{};
    uint8_t dst = 0;
    bool src1_ready = false, src2_ready = false;
    uint32_t src1_val = 0, src2_val = 0;
    uint64_t issue_cycle = 0;
};

struct ReorderBufferEntry {
    bool valid = false;
    Instruction instr{};
    uint8_t dst = 0;
    uint32_t result = 0;
    bool completed = false;
    uint64_t ready_cycle = 0;
};

class Pipeline {
public:
    static constexpr size_t kRSSize = 8;
    static constexpr size_t kROBSize = 16;
    static constexpr size_t kRenameRegs = 32;

    struct Config {
        bool out_of_order = false;
        bool register_renaming = false;
        bool branch_prediction = true;
        size_t fetch_width = 1;
        size_t issue_width = 1;
    };

    Pipeline() : cfg_() {}
    explicit Pipeline(Config cfg) : cfg_(cfg) {}

    void configure(Config c) noexcept { cfg_ = c; }
    const Config& config() const noexcept { return cfg_; }

    void reset() { rs_.fill({}); rob_.fill({}); rob_head_ = 0; rob_tail_ = 0; stalls_ = 0; mispredicts_ = 0; }

    // Try to issue; returns stall cycles (0 = no stall)
    uint32_t try_issue(const Instruction& instr, uint64_t cur_cycle);

    // Commit up to issue_width entries
    uint32_t commit(uint64_t cur_cycle);

    // Branch predictor (simple 2-bit)
    bool predict_taken(uint32_t pc) const noexcept;
    void update_predictor(uint32_t pc, bool taken, bool mispredicted) noexcept;

    uint64_t stalls() const noexcept { return stalls_; }
    uint64_t mispredicts() const noexcept { return mispredicts_; }
    double stall_rate(uint64_t total_cycles) const noexcept {
        return total_cycles ? double(stalls_) / double(total_cycles) : 0.0;
    }

    std::string report(uint64_t total_cycles) const {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "pipeline %s stalls=%llu mispredicts=%llu stall_rate=%.1f%%",
            cfg_.out_of_order ? "OoO" : "in-order",
            (unsigned long long)stalls_, (unsigned long long)mispredicts_,
            stall_rate(total_cycles)*100);
        return buf;
    }

private:
    Config cfg_{};
    std::array<ReservationStation, kRSSize> rs_{};
    std::array<ReorderBufferEntry, kROBSize> rob_{};
    size_t rob_head_ = 0, rob_tail_ = 0;
    uint64_t stalls_ = 0, mispredicts_ = 0;
    // 256-entry direct-mapped 2-bit predictor
    std::array<uint8_t, 256> bpred_{};
};

} // namespace aiasm
