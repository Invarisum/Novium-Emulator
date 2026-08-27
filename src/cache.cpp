#include "aiasm/cache.h"

namespace aiasm {

void Cache::configure(CacheConfig cfg) {
    cfg_ = cfg;
    size_t sets = cfg_.size_bytes / (cfg_.line_bytes * cfg_.associativity);
    sets_.assign(sets, std::vector<Line>(cfg_.associativity));
    tick_ = 0; stats_ = {};
}
bool Cache::access(uint32_t addr, bool is_write, uint32_t* stall_cycles) {
    if (sets_.empty()) return true;
    uint32_t line = addr / cfg_.line_bytes;
    uint32_t tag = line;
    size_t set = line % sets_.size();
    tick_++;
    for (auto& l : sets_[set]) if (l.valid && l.tag == tag) {
        l.lru = tick_; stats_.hits++;
        if (is_write) l.dirty = true;
        if (stall_cycles) *stall_cycles = cfg_.hit_latency;
        return true;
    }
    // miss — evict LRU
    Line* victim = &sets_[set][0];
    for (auto& l : sets_[set]) if (l.lru < victim->lru) victim = &l;
    if (victim->dirty) stats_.writebacks++;
    victim->valid = true; victim->tag = tag; victim->dirty = is_write; victim->lru = tick_;
    stats_.misses++;
    if (stall_cycles) *stall_cycles = cfg_.miss_penalty;
    return false;
}
void Cache::flush() noexcept { for (auto& s: sets_) for (auto& l: s) { l.valid=false; l.dirty=false; } }

uint32_t MemoryController::access(uint32_t addr, bool is_write) {
    (void)is_write;
    uint32_t bank = (addr / 64) % cfg_.banks;
    uint32_t row = addr >> 12;
    bool hit = open_rows_[bank % open_rows_.size()] == row;
    if (hit) row_hits_++; else { row_misses_++; open_rows_[bank % open_rows_.size()] = row; }
    return hit ? cfg_.tCAS : cfg_.tCAS + cfg_.tRCD + cfg_.tRP;
}
uint32_t CacheHierarchy::access(uint32_t addr, bool is_write) {
    uint32_t stall = 0, tmp = 0;
    if (l1.access(addr, is_write, &tmp)) return tmp;
    stall += tmp;
    if (l2.access(addr, is_write, &tmp)) { stall += tmp; return stall; }
    stall += tmp;
    if (l3.access(addr, is_write, &tmp)) { stall += tmp; return stall; }
    stall += tmp;
    stall += mc.access(addr, is_write);
    return stall;
}
std::string CacheHierarchy::report() const {
    return l1.report("L1") + " | " + l2.report("L2") + " | " + l3.report("L3") + " | " + mc.report();
}

} // namespace aiasm
