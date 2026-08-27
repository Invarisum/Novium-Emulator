#include "aiasm/cluster.h"

namespace aiasm {

Cluster::Cluster() : Cluster(Config{}) {}
Cluster::Cluster(Config cfg) : cfg_(cfg), shared_ram_(cfg.shared_ram), deterministic_(cfg.quantum) {
    cores_.reserve(cfg.num_cores);
    for (size_t i = 0; i < cfg.num_cores; ++i) {
        auto e = std::make_unique<Emulator>(cfg.scratchpad_per_core, cfg.shared_ram);
        // Share main RAM via shared window at 0x80000000 (also keep private for simplicity)
        // For demo, we just ensure each core's MainRAM is preloaded from shared on run
        // Add shared bus window (optional TLM)
        e->bus().add_device(0x80000000, 0x01000000,
            [this](uint32_t addr){ uint32_t off = addr - 0x80000000; return shared_ram_.ptr(off)[0] | (shared_ram_.ptr(off)[1]<<8) | (shared_ram_.ptr(off)[2]<<16) | (shared_ram_.ptr(off)[3]<<24); },
            [this](uint32_t addr, uint32_t v){ uint32_t off = addr - 0x80000000; std::memcpy(shared_ram_.ptr(off), &v, 4); });
        cores_.push_back(std::move(e));
    }
}
Cluster::~Cluster() = default;

void Cluster::load_program_all(const uint32_t* prog, size_t count) {
    for (auto& c : cores_) c->load_program(prog, count);
}

void Cluster::run(uint64_t max_cycles) {
    uint64_t start = deterministic_.global_clock();
    while (!all_halted() && (deterministic_.global_clock() - start) < max_cycles) {
        for (size_t i = 0; i < cores_.size(); ++i) {
            if (cores_[i]->halted()) continue;
            // Deterministic quantum
            uint64_t before = cores_[i]->profiler().cycles();
            for (uint64_t q = 0; q < cfg_.quantum && !cores_[i]->halted(); ++q) cores_[i]->step();
            uint64_t after = cores_[i]->profiler().cycles();
            deterministic_.tick(uint32_t(i), after - before);
            // Coherence: if this core did a store, snoop others (simplified: every quantum)
            // (real snoop would be on each STORE; we do periodic for demo)
        }
        // Barrier — deterministic ordering
        if (all_halted()) break;
    }
}
void Cluster::run_fast(uint64_t max_cycles) {
    uint64_t start = deterministic_.global_clock();
    while (!all_halted() && (deterministic_.global_clock() - start) < max_cycles) {
        for (auto& c : cores_) if (!c->halted()) c->run_fast();
        deterministic_.tick(0, cfg_.quantum);
        if (all_halted()) break;
    }
}
void Cluster::step_all() {
    for (size_t i = 0; i < cores_.size(); ++i) if (!cores_[i]->halted()) cores_[i]->step();
}

void Cluster::snoop_invalidate(uint32_t addr, size_t writer) {
    for (size_t i = 0; i < cores_.size(); ++i) if (i != writer) {
        // Invalidate L1 line in other cores (if we had per-core L1 exposure, we'd call)
        // For now, just flush TLB line as proxy for coherence
        cores_[i]->mmu().tlb_flush_va(addr);
    }
}
void Cluster::tlb_shootdown(uint32_t va, size_t initiator) {
    for (size_t i = 0; i < cores_.size(); ++i) if (i != initiator) cores_[i]->mmu().tlb_flush_va(va);
}
void Cluster::send_ipi(size_t target) {
    if (target < cores_.size()) cores_[target]->interrupt_controller().raise_soft();
}
bool Cluster::all_halted() const {
    for (auto& c : cores_) if (!c->halted()) return false;
    return true;
}
uint64_t Cluster::total_cycles() const {
    uint64_t s = 0; for (auto& c : cores_) s += c->profiler().cycles(); return s;
}
uint64_t Cluster::total_instructions() const {
    uint64_t s = 0; for (auto& c : cores_) s += c->profiler().instructions(); return s;
}
std::string Cluster::report() const {
    char buf[512];
    std::snprintf(buf, sizeof(buf), "Cluster %zu cores shared_ram=%zuKB det_clock=%llu total_cyc=%llu total_instr=%llu",
        cores_.size(), shared_ram_.size()/1024,
        (unsigned long long)deterministic_.global_clock(),
        (unsigned long long)total_cycles(), (unsigned long long)total_instructions());
    return buf;
}

} // namespace aiasm
