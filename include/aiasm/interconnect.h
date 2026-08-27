#pragma once

#include "config.h"

#include <array>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Synchronous Interconnect / Bus Simulation
//
// Models AXI4, PCIe (TLP), and CXI (Cray) fabrics that connect core →
// memory → accelerators. Each transaction is staged through channels with
// configurable latency / bandwidth; profiler counts wait cycles.
//
// This sits *above* the simple MMIO Bus (bus.h) — Bus handles address
// decode, Interconnect handles timing & protocol.
// ============================================================================

enum class FabricProtocol : uint8_t { AXI4 = 0, PCIe = 1, CXI = 2 };

struct InterconnectConfig {
    FabricProtocol protocol = FabricProtocol::AXI4;
    uint32_t data_width = 64;      // bytes per beat
    uint32_t latency_cycles = 2;   // per-transaction base latency
    uint32_t max_outstanding = 8;  // max in-flight transactions
    uint32_t burst_len = 16;       // beats per burst
};

struct Transaction {
    uint32_t id = 0;
    uint32_t addr = 0;
    uint32_t bytes = 0;
    bool is_write = false;
    uint64_t issue_cycle = 0;
    uint64_t complete_cycle = 0;
    FabricProtocol proto = FabricProtocol::AXI4;
};

class Interconnect {
public:
    explicit Interconnect(InterconnectConfig cfg = {}) : cfg_(cfg) {}

    void configure(InterconnectConfig cfg) { cfg_ = cfg; }
    const InterconnectConfig& config() const noexcept { return cfg_; }

    // Issue a transaction; returns false if fabric is full (backpressure)
    bool issue(const Transaction& t, uint64_t cur_cycle);

    // Advance fabric by `cycles`, complete pending transactions
    void tick(uint64_t cycles);

    // Query
    size_t outstanding() const noexcept { return pending_.size(); }
    uint64_t completed() const noexcept { return completed_; }
    uint64_t stalled() const noexcept { return stalled_; }
    double utilization() const noexcept {
        uint64_t tot = completed_ + stalled_;
        return tot ? double(completed_) / double(tot) : 0.0;
    }
    size_t pending_bytes() const noexcept {
        size_t s = 0;
        for (auto& t : pending_) s += t.bytes;
        return s;
    }

    void reset() { pending_.clear(); completed_ = 0; stalled_ = 0; cur_cycle_ = 0; }

    std::string proto_name() const {
        switch (cfg_.protocol) {
            case FabricProtocol::AXI4: return "AXI4";
            case FabricProtocol::PCIe: return "PCIe";
            case FabricProtocol::CXI:  return "CXI";
        }
        return "unknown";
    }

    // Latency model for profiler (cycles to add to DMA / LOAD)
    uint32_t latency_for(uint32_t bytes) const noexcept {
        uint32_t bursts = (bytes + cfg_.data_width * cfg_.burst_len - 1) / (cfg_.data_width * cfg_.burst_len);
        return cfg_.latency_cycles + bursts;
    }

private:
    InterconnectConfig cfg_{};
    std::deque<Transaction> pending_;
    uint64_t cur_cycle_ = 0;
    uint64_t completed_ = 0;
    uint64_t stalled_ = 0;
};

} // namespace aiasm
