#include "aiasm/pipeline.h"

namespace aiasm {

uint32_t Pipeline::try_issue(const Instruction& instr, uint64_t cur_cycle) {
    if (!cfg_.out_of_order) return 0;
    for (auto& rs : rs_) if (!rs.occupied) {
        rs.occupied = true; rs.instr = instr; rs.issue_cycle = cur_cycle;
        return 0;
    }
    stalls_++;
    return 1;
}

uint32_t Pipeline::commit(uint64_t cur_cycle) {
    uint32_t committed = 0;
    for (auto& rs : rs_) if (rs.occupied && rs.issue_cycle + 1 <= cur_cycle) {
        rs.occupied = false; committed++; if (committed >= cfg_.issue_width) break;
    }
    (void)cur_cycle;
    return committed;
}

bool Pipeline::predict_taken(uint32_t pc) const noexcept {
    if (!cfg_.branch_prediction) return false;
    return (bpred_[pc % bpred_.size()] & 0x2) != 0;
}
void Pipeline::update_predictor(uint32_t pc, bool taken, bool mispredicted) noexcept {
    uint8_t& ctr = bpred_[pc % bpred_.size()];
    if (taken) ctr = ctr < 3 ? ctr + 1 : 3;
    else       ctr = ctr > 0 ? ctr - 1 : 0;
    if (mispredicted) mispredicts_++;
}

} // namespace aiasm
