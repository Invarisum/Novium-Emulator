#pragma once

#include "config.h"
#include "isa.h"
#include "memory.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Memory-Mapped Bus
//
// Routes load/store requests to the appropriate device:
//   0x00000000 - scratchpad SRAM (main memory)
//   MMIO_BASE  - peripheral register space
//
// Devices register themselves by base address and a read/write callback.
// ============================================================================

class Bus {
public:
    using ReadHandler  = std::function<uint32_t(uint32_t addr)>;
    using WriteHandler = std::function<void(uint32_t addr, uint32_t value)>;

    struct Device {
        uint32_t base;
        uint32_t size;
        ReadHandler  read;
        WriteHandler write;
    };

    explicit Bus(VirtualSRAM& ram) : ram_(ram) {}

    // Register an MMIO device
    void add_device(uint32_t base, uint32_t size, ReadHandler rd, WriteHandler wr) {
        devices_.push_back({base, size, std::move(rd), std::move(wr)});
    }

    // --- 32-bit memory access (CPU side) ---
    uint32_t read32(uint32_t addr) {
        if (addr >= MMIO_BASE) {
            for (const auto& dev : devices_) {
                if (addr >= dev.base && addr < dev.base + dev.size) {
                    if (dev.read) return dev.read(addr);
                    return 0;
                }
            }
        }
        return ram_.read32(addr);
    }

    void write32(uint32_t addr, uint32_t value) {
        if (addr >= MMIO_BASE) {
            for (const auto& dev : devices_) {
                if (addr >= dev.base && addr < dev.base + dev.size) {
                    if (dev.write) dev.write(addr, value);
                    return;
                }
            }
        }
        ram_.write32(addr, value);
    }

    // --- Byte-level access for DMA and VR loads ---
    const uint8_t* ptr(uint32_t addr) const { return ram_.ptr(addr); }
    uint8_t* ptr(uint32_t addr)             { return ram_.ptr(addr); }

    bool is_mmio(uint32_t addr) const noexcept { return addr >= MMIO_BASE; }

private:
    VirtualSRAM& ram_;
    std::vector<Bus::Device> devices_;
};

} // namespace aiasm
