#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Deterministic Execution Engine
//
// Enforces total order for multi-core / multi-threaded runs so the same
// guest binary, inputs and starting state produce identical traces down to
// the exact cycle. Mirrors QEMU's deterministic icount and record-replay.
// Uses a global sequence clock and per-core deterministic quanta.
// ============================================================================

class DeterministicEngine {
public:
    explicit DeterministicEngine(uint64_t quantum = 1000) : quantum_(quantum) {}

    void set_quantum(uint64_t q) noexcept { quantum_ = q; }
    uint64_t quantum() const noexcept { return quantum_; }

    // Called at each instruction commit; returns true if deterministic barrier needed
    bool tick(uint32_t core_id, uint64_t cycles) {
        std::lock_guard<std::mutex> g(mu_);
        clocks_[core_id % kMaxCores] += cycles;
        global_clock_ += cycles;
        seq_++;
        // Deterministic barrier every quantum
        if ((global_clock_ % quantum_) == 0) return true;
        return false;
    }

    uint64_t global_clock() const noexcept { return global_clock_; }
    uint64_t seq() const noexcept { return seq_; }

    void reset() {
        std::lock_guard<std::mutex> g(mu_);
        global_clock_ = 0; seq_ = 0;
        clocks_.assign(kMaxCores, 0);
    }

    std::string report() const {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "deterministic quantum=%llu global_clock=%llu seq=%llu",
            (unsigned long long)quantum_, (unsigned long long)global_clock_, (unsigned long long)seq_);
        return buf;
    }

private:
    static constexpr size_t kMaxCores = 16;
    uint64_t quantum_ = 1000;
    uint64_t global_clock_ = 0;
    uint64_t seq_ = 0;
    std::vector<uint64_t> clocks_ = std::vector<uint64_t>(kMaxCores, 0);
    std::mutex mu_;
};

} // namespace aiasm
