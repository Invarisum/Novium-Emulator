#include "aiasm/interconnect.h"

namespace aiasm {

bool Interconnect::issue(const Transaction& t, uint64_t cur_cycle) {
    if (pending_.size() >= cfg_.max_outstanding) { ++stalled_; return false; }
    Transaction nt = t;
    nt.issue_cycle = cur_cycle;
    // simple latency model
    uint32_t bursts = (t.bytes + cfg_.data_width * cfg_.burst_len - 1) / (cfg_.data_width * cfg_.burst_len);
    nt.complete_cycle = cur_cycle + cfg_.latency_cycles + bursts;
    nt.proto = cfg_.protocol;
    pending_.push_back(nt);
    cur_cycle_ = cur_cycle;
    return true;
}

void Interconnect::tick(uint64_t cycles) {
    cur_cycle_ += cycles;
    while (!pending_.empty() && pending_.front().complete_cycle <= cur_cycle_) {
        pending_.pop_front();
        ++completed_;
    }
}

} // namespace aiasm
