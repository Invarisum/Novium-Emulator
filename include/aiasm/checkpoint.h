#pragma once

#include "registers.h"
#include "csr.h"
#include "mmu.h"
#include "profiler.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Checkpointing & Reverse Execution
//
// Save/load the full execution + microarchitectural state instantaneously.
// History buffer keeps the last N committed states so `step_back()` can
// rewind to the root cause of a crash (like GDB reverse / rr). Each
// checkpoint is ~128 bytes + VRs, compact enough for thousands of steps.
// ============================================================================

struct Checkpoint {
    RegisterFile regs;
    CSRFile csr;
    Profiler::State profiler;
    uint64_t tick_accum = 0;
    uint64_t global_clock = 0;
    uint32_t seq = 0;
    std::vector<uint8_t> scratch_snapshot; // optional full memory (lazy)
    bool has_memory = false;
};

class CheckpointManager {
public:
    explicit CheckpointManager(size_t max_history = 1024) : max_history_(max_history) {}

    void set_max_history(size_t n) noexcept { max_history_ = n; }
    size_t size() const noexcept { return history_.size(); }
    size_t max_history() const noexcept { return max_history_; }

    // Capture current state (call after each committed instruction if tracing)
    void push(const RegisterFile& regs, const CSRFile& csr, const Profiler::State& prof,
              uint64_t tick, uint64_t clock, uint32_t seq) {
        Checkpoint cp;
        cp.regs = regs; cp.csr = csr; cp.profiler = prof;
        cp.tick_accum = tick; cp.global_clock = clock; cp.seq = seq;
        history_.push_back(std::move(cp));
        if (history_.size() > max_history_) history_.pop_front();
    }

    // Full memory checkpoint (heavy)
    void push_full(const Checkpoint& cp, const std::vector<uint8_t>& mem) {
        Checkpoint c = cp;
        c.scratch_snapshot = mem;
        c.has_memory = true;
        history_.push_back(std::move(c));
        if (history_.size() > max_history_) history_.pop_front();
    }

    bool can_reverse() const noexcept { return !history_.empty(); }

    // Pop last checkpoint and return it (caller restores)
    bool pop(Checkpoint& out) {
        if (history_.empty()) return false;
        out = history_.back();
        history_.pop_back();
        return true;
    }

    // Save/restore to opaque blob
    std::vector<uint8_t> save_blob(const Checkpoint& cp) const;
    bool restore_blob(const std::vector<uint8_t>& blob, Checkpoint& out) const;

    void clear() { history_.clear(); }

    std::string stats() const {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "checkpoint history %zu/%zu", history_.size(), max_history_);
        return buf;
    }

private:
    std::deque<Checkpoint> history_;
    size_t max_history_ = 1024;
};

} // namespace aiasm
