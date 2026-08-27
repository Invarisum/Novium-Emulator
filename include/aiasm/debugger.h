#pragma once

#include "config.h"
#include "isa.h"

#include <cstdint>
#include <set>
#include <string>

namespace aiasm {

class VirtualSRAM;  // forward decl

// ============================================================================
// Step Debugger
//
// Features:
//   - Breakpoint management (add/remove/query by address)
//   - Single-step execution
//   - Memory dump (hex + disasm)
//   - Register dump
// ============================================================================

class Debugger {
public:
    // --- Breakpoint management ---
    void add_breakpoint(uint32_t addr) { breakpoints_.insert(addr); }
    void remove_breakpoint(uint32_t addr) { breakpoints_.erase(addr); }
    void clear_breakpoints() { breakpoints_.clear(); }
    bool has_breakpoint(uint32_t addr) const { return breakpoints_.contains(addr); }
    const std::set<uint32_t>& breakpoints() const noexcept { return breakpoints_; }

    // --- Single-step mode ---
    void enable_single_step() noexcept { single_step_ = true; }
    void disable_single_step() noexcept { single_step_ = false; }
    bool single_step() const noexcept { return single_step_; }

    // --- Break reason ---
    enum class StopReason {
        Breakpoint,
        SingleStep,
        Halt,
        None,
    };

    // --- Dump helpers ---
    std::string dump_registers(const RegisterFile& regs) const;
    std::string dump_memory(const VirtualSRAM& mem, uint32_t addr, size_t count) const;
    std::string disassemble_range(const VirtualSRAM& mem, uint32_t start, size_t count) const;

private:
    std::set<uint32_t> breakpoints_;
    bool single_step_ = false;
};

} // namespace aiasm
