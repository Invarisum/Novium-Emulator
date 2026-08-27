#pragma once

#include "emulator.h"
#include "cache.h"
#include "deterministic.h"
#include "interconnect.h"

#include <memory>
#include <mutex>
#include <vector>

namespace aiasm {

// ============================================================================
// Cluster — Multi-core SMP with Deterministic Execution & Coherence
//
// Owns N Emulator cores with private scratchpads/L1s, shared MainRAM (via
// shared bus window), shared L2/L3, and a global DeterministicEngine that
// enforces quantum-based lockstep so the same binary reproduces down to the
// cycle. Coherence is MESI via snoop invalidates on writes.
// ============================================================================

class Cluster {
public:
    struct Config {
        size_t num_cores = 2;
        size_t scratchpad_per_core = SCRATCHPAD_SIZE;
        size_t shared_ram = MAIN_RAM_SIZE;
        uint64_t quantum = 1000; // cycles per deterministic barrier
    };

    Cluster();
    explicit Cluster(Config cfg);
    ~Cluster();

    Emulator& core(size_t id) { return *cores_[id]; }
    const Emulator& core(size_t id) const { return *cores_[id]; }
    size_t num_cores() const noexcept { return cores_.size(); }

    MainRAM& shared_ram() noexcept { return shared_ram_; }
    DeterministicEngine& deterministic() noexcept { return deterministic_; }
    Cache& shared_l2() noexcept { return shared_l2_; }
    Interconnect& cluster_interconnect() noexcept { return cluster_bus_; }

    // Load same program to all cores (SPMD)
    void load_program_all(const uint32_t* prog, size_t count);
    void load_program_all(const std::vector<uint32_t>& prog) { load_program_all(prog.data(), prog.size()); }

    // Deterministic run: each core runs `quantum` cycles, then barrier
    void run(uint64_t max_cycles = UINT64_MAX);
    void run_fast(uint64_t max_cycles = UINT64_MAX);
    void step_all(); // one step per core in round-robin

    // Cache coherence: snoop invalidate other cores' L1 on write
    void snoop_invalidate(uint32_t addr, size_t core_writer);

    // TLB shootdown across cores (SFENCE.VMA)
    void tlb_shootdown(uint32_t va, size_t initiator);

    // Inter-core interrupt (IPI) via CLINT
    void send_ipi(size_t target_core);

    bool all_halted() const;
    uint64_t total_cycles() const;
    uint64_t total_instructions() const;

    // C-ABI helpers
    std::string report() const;

private:
    Config cfg_;
    std::vector<std::unique_ptr<Emulator>> cores_;
    MainRAM shared_ram_;
    Cache shared_l2_{CacheConfig{512*1024, 64, 16, CoherenceProtocol::MESI, 4, 20}};
    Interconnect cluster_bus_{InterconnectConfig{FabricProtocol::AXI4, 64, 2, 16, 16}};
    DeterministicEngine deterministic_;
    std::mutex mu_;
};

} // namespace aiasm
