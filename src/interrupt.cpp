#include "aiasm/interrupt.h"

namespace aiasm {

bool InterruptController::interrupt_pending() const noexcept {
    if (!csr_) return (mip_ != 0);
    uint64_t mie = csr_->read(CSR_MIE);
    uint64_t mstatus = csr_->read(CSR_MSTATUS);
    bool mie_global = (mstatus & (1ULL<<3)) != 0;
    if (!mie_global) return false;
    return (mip_ & mie) != 0;
}

std::optional<Trap> InterruptController::pending_interrupt() const noexcept {
    if (!interrupt_pending()) return std::nullopt;
    uint64_t mie = csr_ ? csr_->read(CSR_MIE) : 0xFFFFFFFF;
    uint32_t pending = mip_ & uint32_t(mie);
    // Priority: external > timer > soft
    if (pending & (1u<<11)) return Trap{TrapCause::MachineExternal, 0, 0, true};
    if (pending & (1u<<7))  return Trap{TrapCause::MachineTimer, 0, 0, true};
    if (pending & (1u<<3))  return Trap{TrapCause::MachineSoft, 0, 0, true};
    if (pending & (1u<<9))  return Trap{TrapCause::SupervisorExternal, 0, 0, true};
    if (pending & (1u<<5))  return Trap{TrapCause::SupervisorTimer, 0, 0, true};
    return std::nullopt;
}

bool InterruptController::take_trap(const Trap& t) {
    if (!csr_) return false;
    uint64_t cause = static_cast<uint16_t>(t.cause);
    bool is_int = t.is_interrupt;
    uint64_t trap_cause = cause | (is_int ? (1ULL<<63) : 0);
    // Save PC → MEPC, cause → MCAUSE, tval → MTVAL
    csr_->write(CSR_MEPC, t.pc);
    csr_->write(CSR_MCAUSE, trap_cause);
    csr_->write(CSR_MTVAL, t.tval);
    // MSTATUS: save MIE → MPIE, set MPP to current priv, clear MIE
    uint64_t mstatus = csr_->read(CSR_MSTATUS);
    bool mie = (mstatus & (1ULL<<3)) != 0;
    mstatus &= ~((1ULL<<7) | (1ULL<<3) | (0x3ULL<<11));
    if (mie) mstatus |= (1ULL<<7); // MPIE = old MIE
    mstatus |= (uint64_t(csr_->priv) << 11); // MPP
    csr_->write(CSR_MSTATUS, mstatus);
    csr_->priv = PrivilegeLevel::Machine;
    ++trap_count_;
    return true;
}

Trap InterruptController::ecall_trap(uint64_t pc) const noexcept {
    Trap t;
    if (!csr_) { t.cause = TrapCause::EnvCallM; return t; }
    if (csr_->priv == PrivilegeLevel::User) t.cause = TrapCause::EnvCallU;
    else if (csr_->priv == PrivilegeLevel::Supervisor) t.cause = TrapCause::EnvCallS;
    else t.cause = TrapCause::EnvCallM;
    t.pc = pc; t.tval = 0; t.is_interrupt = false;
    return t;
}
Trap InterruptController::ebreak_trap(uint64_t pc) const noexcept { return Trap{TrapCause::Breakpoint, 0, pc, false}; }
Trap InterruptController::page_fault_trap(uint64_t pc, uint64_t va, bool is_store) const noexcept {
    Trap t; t.pc = pc; t.tval = va; t.is_interrupt = false;
    t.cause = is_store ? TrapCause::StorePageFault : TrapCause::LoadPageFault;
    return t;
}
Trap InterruptController::illegal_inst_trap(uint64_t pc, uint32_t inst) const noexcept { return Trap{TrapCause::IllegalInst, inst, pc, false}; }

uint64_t InterruptController::mret() noexcept {
    if (!csr_) return 0;
    uint64_t mstatus = csr_->read(CSR_MSTATUS);
    uint64_t mpp = (mstatus >> 11) & 0x3;
    bool mpie = (mstatus & (1ULL<<7)) != 0;
    // Restore MIE = MPIE
    mstatus &= ~(1ULL<<3);
    if (mpie) mstatus |= (1ULL<<3);
    mstatus &= ~(1ULL<<7);
    mstatus &= ~(0x3ULL<<11);
    csr_->write(CSR_MSTATUS, mstatus);
    // Restore privilege
    csr_->priv = static_cast<PrivilegeLevel>(mpp & 0x3);
    return csr_->read(CSR_MEPC);
}
uint64_t InterruptController::sret() noexcept {
    if (!csr_) return 0;
    uint64_t mstatus = csr_->read(CSR_MSTATUS);
    bool spie = (mstatus & (1ULL<<5)) != 0;
    uint64_t spp = (mstatus >> 8) & 1;
    mstatus &= ~(1ULL<<1);
    if (spie) mstatus |= (1ULL<<1);
    mstatus &= ~(1ULL<<5);
    mstatus &= ~(1ULL<<8);
    csr_->write(CSR_MSTATUS, mstatus);
    csr_->priv = spp ? PrivilegeLevel::Supervisor : PrivilegeLevel::User;
    return csr_->read(CSR_SEPC);
}

} // namespace aiasm
