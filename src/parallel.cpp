#include "aiasm/parallel.h"

namespace aiasm {

int WarpScheduler::schedule() {
    for (size_t i=0;i<state_.size();++i) {
        size_t id = (rr_ + i) % state_.size();
        if (state_[id]==0) { rr_ = (id+1)%state_.size(); scheduled_++; return int(id); }
    }
    return -1;
}
void WarpScheduler::tick(uint32_t cycles) noexcept {
    for (auto& s: state_) if (s>0) { if (s>cycles) s-=cycles; else s=0; }
}
uint32_t WarpScheduler::active_warps() const noexcept {
    uint32_t n=0; for (auto s: state_) if (s==0) n++; return n;
}
uint32_t SystolicArray::enqueue_tile(uint32_t tile_n) {
    (void)tile_n;
    tiles_++;
    uint32_t cycles = (cfg_.rows * cfg_.cols + cfg_.macs_per_cycle -1)/cfg_.macs_per_cycle;
    // pipeline: first tile pays fill, subsequent overlap
    if (pipeline_depth_ < cfg_.rows) { pipeline_depth_++; total_cycles_ += cycles; }
    else { total_cycles_ += 1; }
    return cycles;
}

} // namespace aiasm
