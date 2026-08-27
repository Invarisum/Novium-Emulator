#include "aiasm/fpga.h"
#include "aiasm/host.h"
#include <cstring>

namespace aiasm {

bool FPGACoEmulator::available() const noexcept {
    if (cfg_.backend==FPGABackend::None) return false;
    if (cfg_.backend==FPGABackend::Simulated) return true;
    // Real PCIe probe would check /dev/xdma or lspci; for now, only simulated is available
    return false;
}
bool FPGACoEmulator::connect() {
    if (!available()) return false;
    connected_=true; return true;
}
void FPGACoEmulator::disconnect() { connected_=false; }

bool FPGACoEmulator::offload_matmul(const float* a, const float* b, float* d, uint32_t n) {
    if (!connected_ || !available()) { fallback_++; return false; }
    // Simulated FPGA: still use host SIMD but count as offloaded
    host::matmul_tile_host(d, a, b, uint8_t(n));
    offloaded_++; return true;
}
bool FPGACoEmulator::offload_dma(const void* src, void* dst, uint32_t bytes, bool is_write) {
    (void)is_write;
    if (!connected_ || !available()) { fallback_++; return false; }
    std::memcpy(dst, src, bytes);
    offloaded_++; return true;
}

} // namespace aiasm
