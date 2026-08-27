#pragma once

#include "config.h"
#include "csr.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Basic Interrupt & Trap Controller
//
// Implements RISC-V-style traps: synchronous exceptions (ecall, page fault,
// illegal instruction) and asynchronous interrupts (timer, external, soft).
// Handles privilege transitions (U→S→M) and vectored/direct trap delivery
// via mtvec/stvec. Mirrors CLINT + PLIC.
// ============================================================================

enum class TrapCause : uint16_t {
    // Exceptions (sync)
    InstAddrMisaligned = 0, InstAccessFault = 1, IllegalInst = 2, Breakpoint = 3,
    LoadAddrMisaligned = 4, LoadAccessFault = 5, StoreAddrMisaligned = 6, StoreAccessFault = 7,
    EnvCallU = 8, EnvCallS = 9, EnvCallM = 11,
    InstPageFault = 12, LoadPageFault = 13, StorePageFault = 15,
    // Interrupts (async) — cause | 0x8000
    SupervisorSoft = 0x8001, MachineSoft = 0x8003,
    SupervisorTimer = 0x8005, MachineTimer = 0x8007,
    SupervisorExternal = 0x8009, MachineExternal = 0x800B,
};

struct Trap {
    TrapCause cause = TrapCause::IllegalInst;
    uint64_t tval = 0;   // faulting address / instruction
    uint64_t pc = 0;
    bool is_interrupt = false;
};

using TrapHandler = std::function<void(const Trap&)>;

class InterruptController {
public:
    explicit InterruptController(CSRFile* csr = nullptr) : csr_(csr) {}

    void attach_csr(CSRFile* csr) noexcept { csr_ = csr; }

    // --- Interrupt lines (from devices) ---
    void raise_timer() noexcept { mip_ |= (1u << 7); }      // MTIP
    void raise_external(uint32_t id = 0) noexcept { mip_ |= (1u << 11); external_id_ = id; }
    void raise_soft() noexcept { mip_ |= (1u << 3); }
    void clear_timer() noexcept { mip_ &= ~(1u << 7); }
    void clear_external() noexcept { mip_ &= ~(1u << 11); }
    void clear_soft() noexcept { mip_ &= ~(1u << 3); }

    // --- Trap entry ---
    // Returns true if trap was taken (caller should redirect PC to trap vector)
    bool take_trap(const Trap& t);

    // Check if any enabled interrupt is pending
    bool interrupt_pending() const noexcept;
    std::optional<Trap> pending_interrupt() const noexcept;

    // Ecall / ebreak helpers
    Trap ecall_trap(uint64_t pc) const noexcept;
    Trap ebreak_trap(uint64_t pc) const noexcept;
    Trap page_fault_trap(uint64_t pc, uint64_t va, bool is_store) const noexcept;
    Trap illegal_inst_trap(uint64_t pc, uint32_t inst) const noexcept;

    // MRET/SRET — return from trap
    uint64_t mret() noexcept;
    uint64_t sret() noexcept;

    uint32_t mip() const noexcept { return mip_; }
    void set_mip(uint32_t v) noexcept { mip_ = v; }

    void reset() { mip_ = 0; external_id_ = 0; trap_count_ = 0; }

    uint64_t trap_count() const noexcept { return trap_count_; }

private:
    CSRFile* csr_ = nullptr;
    uint32_t mip_ = 0; // machine interrupt pending (mirrors CSR MIP)
    uint32_t external_id_ = 0;
    uint64_t trap_count_ = 0;
};

} // namespace aiasm
