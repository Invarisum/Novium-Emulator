#include "aiasm/pmc.h"

namespace aiasm {

void PMC::on_commit(uint32_t cycles, bool is_branch_mispredict, bool is_cache_miss) {
    inc(PMCEvent::Cycles, cycles);
    inc(PMCEvent::Instructions, 1);
    if (is_branch_mispredict) inc(PMCEvent::BranchMispredicts, 1);
    if (is_cache_miss) inc(PMCEvent::CacheMisses, 1);
}

} // namespace aiasm
