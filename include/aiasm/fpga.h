#pragma once

#include <cstdint>
#include <string>

namespace aiasm {

// ============================================================================
// FPGA Co-Emulation Support (Hardware In The Loop)
//
// Offloads heavy logic (e.g., systolic matmul, DMA) to a host FPGA via
// PCIe. When no FPGA is present, the call falls back to host SIMD
// (host.h) so the same binary runs everywhere. Mirrors typical ASIC
// pre-silicon verification flows.
// ============================================================================

enum class FPGABackend : uint8_t { None = 0, PCIe = 1, USB = 2, Simulated = 3 };

struct FPGAConfig {
    FPGABackend backend = FPGABackend::None;
    std::string bitstream; // path to .bit / .xclbin
    uint32_t device_id = 0;
    bool fallback_to_sim = true;
};

class FPGACoEmulator {
public:
    explicit FPGACoEmulator(FPGAConfig cfg = {}) : cfg_(cfg) {}

    void configure(FPGAConfig c) { cfg_ = c; }
    const FPGAConfig& config() const noexcept { return cfg_; }

    bool available() const noexcept;
    bool connect();
    void disconnect();

    // Offload a matmul tile; returns true if handled by FPGA, false if caller should use sim
    bool offload_matmul(const float* a, const float* b, float* d, uint32_t n);

    // Offload DMA
    bool offload_dma(const void* src, void* dst, uint32_t bytes, bool is_write);

    uint64_t offloaded_ops() const noexcept { return offloaded_; }
    uint64_t fallback_ops() const noexcept { return fallback_; }

    std::string report() const {
        char buf[256];
        const char* be = cfg_.backend==FPGABackend::PCIe?"PCIe":cfg_.backend==FPGABackend::Simulated?"sim":cfg_.backend==FPGABackend::None?"none":"usb";
        std::snprintf(buf, sizeof(buf), "FPGA backend=%s available=%s offloaded=%llu fallback=%llu bitstream=%s",
            be, available()?"yes":"no",
            (unsigned long long)offloaded_, (unsigned long long)fallback_,
            cfg_.bitstream.empty()?"<none>":cfg_.bitstream.c_str());
        return buf;
    }

private:
    FPGAConfig cfg_{};
    bool connected_ = false;
    uint64_t offloaded_ = 0, fallback_ = 0;
};

} // namespace aiasm
