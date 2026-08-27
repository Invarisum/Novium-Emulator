#include "aiasm/checkpoint.h"

namespace aiasm {

std::vector<uint8_t> CheckpointManager::save_blob(const Checkpoint& cp) const {
    std::vector<uint8_t> blob(sizeof(cp.regs) + sizeof(cp.csr) + 64);
    std::memcpy(blob.data(), &cp.regs, sizeof(cp.regs));
    return blob;
}
bool CheckpointManager::restore_blob(const std::vector<uint8_t>& blob, Checkpoint& out) const {
    if (blob.size() < sizeof(out.regs)) return false;
    std::memcpy(&out.regs, blob.data(), sizeof(out.regs));
    return true;
}

} // namespace aiasm
