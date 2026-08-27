#pragma once

#include "config.h"

#include <array>
#include <cstdint>
#include <string>

namespace aiasm {

// ============================================================================
// Architectural State — CSR File
//
// Supplements RegisterFile (GPRs/VRs/PC) with RISC-V-style CSRs and
// control/status registers used by MMU/trap logic. Covers floating-point
// CSRs (fcsr), machine/supervisor CSRs, and custom ASIC CSRs.
// ============================================================================

enum class PrivilegeLevel : uint8_t { User = 0, Supervisor = 1, Machine = 3 };

// CSR addresses (12-bit, RISC-V encoding + custom Novium range 0x800-0x8FF)
enum CSRAddr : uint16_t {
    CSR_FFLAGS     = 0x001, CSR_FRM      = 0x002, CSR_FCSR     = 0x003,
    CSR_CYCLE      = 0xC00, CSR_TIME     = 0xC01, CSR_INSTRET  = 0xC02,
    CSR_SSTATUS    = 0x100, CSR_SIE      = 0x104, CSR_STVEC    = 0x105,
    CSR_SEPC       = 0x141, CSR_SCAUSE   = 0x142, CSR_STVAL    = 0x143,
    CSR_SATP       = 0x180, // Supervisor address translation
    CSR_MSTATUS    = 0x300, CSR_MISA     = 0x301, CSR_MEDELEG  = 0x302,
    CSR_MIDELEG    = 0x303, CSR_MIE      = 0x304, CSR_MTVEC    = 0x305,
    CSR_MSCRATCH   = 0x340, CSR_MEPC     = 0x341, CSR_MCAUSE   = 0x342,
    CSR_MTVAL      = 0x343, CSR_MIP      = 0x344,
    // Novium custom (tensor / DMA)
    CSR_NOV_TILE   = 0x800, CSR_NOV_LANES= 0x801, CSR_NOV_STAT = 0x802,
};

struct CSRFile {
    // Sparse storage — 4096 CSR slots, only a subset populated
    std::array<uint64_t, 4096> regs{};

    PrivilegeLevel priv = PrivilegeLevel::Machine;

    uint64_t read(uint16_t addr) const noexcept {
        if (addr < regs.size()) return regs[addr];
        return 0;
    }
    void write(uint16_t addr, uint64_t val) noexcept {
        if (addr >= regs.size()) return;
        // Hard-wire read-only CSRs
        if (addr == CSR_MISA) return;
        // MSTATUS.FS/SD etc could be masked here
        regs[addr] = val;
    }

    // Convenience helpers
    uint64_t mstatus() const noexcept { return read(CSR_MSTATUS); }
    uint64_t mtvec()   const noexcept { return read(CSR_MTVEC); }
    uint64_t mepc()    const noexcept { return read(CSR_MEPC); }
    void set_mepc(uint64_t v) noexcept { write(CSR_MEPC, v); }
    uint64_t satp()    const noexcept { return read(CSR_SATP); }

    bool mstatus_mie() const noexcept { return (read(CSR_MSTATUS) & (1ULL<<3)) != 0; }
    bool mie_enabled(uint8_t irq) const noexcept { return (read(CSR_MIE) & (1ULL<<irq)) != 0; }

    void reset() {
        regs.fill(0);
        regs[CSR_MISA] = 0x40001100; // RV64IMAFD
        regs[CSR_MSTATUS] = 0;
        regs[CSR_MTVEC] = 0;
        regs[CSR_SATP] = 0;
        priv = PrivilegeLevel::Machine;
    }

    std::string dump() const;
};

} // namespace aiasm
