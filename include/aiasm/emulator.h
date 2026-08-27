#pragma once

#include "config.h"
#include "isa.h"
#include "memory.h"
#include "registers.h"
#include "profiler.h"
#include "bus.h"
#include "devices.h"
#include "debugger.h"
#include "host.h"
#include "target.h"
#include "translate.h"
#include "decoder.h"
#include "csr.h"
#include "mmu.h"
#include "interconnect.h"
#include "interrupt.h"

#include <array>
#include <cstdint>
#include <string>

namespace aiasm {

// ============================================================================
// Emulator / Golden Model — full architectural simulation
//
// Subsystems (as requested):
//   1. ISA Translation Engine (Decoder & Executor) — dec/decoder.h + translate.h
//      Translates ARM64/RISC-V/PTX/ASIC into host actions (interpreter + JIT
//      BlockCache, QEMU-TCG style).
//   2. Architectural State Register File — registers.h + csr.h
//      GPRs/VRs/PC + CSRs (mstatus/mtvec/satp etc.) + PrivilegeLevel.
//   3. MMU & Virtual Memory — mmu.h
//      Sv39 page tables, TLB (32-entry LRU), R/W/X/U + A/D permission.
//   4. Synchronous Interconnect / Bus — bus.h + interconnect.h
//      AXI4 / PCIe / CXI fabrics with latency/bandwidth & backpressure.
//   5. Interrupt & Trap Controller — interrupt.h
//      CLINT/PLIC, mtvec/stvec, ecall/page-fault/illegal-inst traps, MRET/SRET.
//
// Decoupled from CLI — a C-ABI wrapper is provided for shared-library export.
// ============================================================================

class Emulator {
public:
    explicit Emulator(size_t scratchpad_bytes = SCRATCHPAD_SIZE,
                      size_t main_ram_bytes   = MAIN_RAM_SIZE);

    // --- Configuration ---
    void    set_verbose(bool v) noexcept { verbose_ = v; }
    void    set_target(TargetProfile p);
    void    set_target_kind(TargetKind k);
    void    set_acceleration(bool v) noexcept { accel_ = v; }
    void    write_gpr(uint8_t idx, uint32_t val) noexcept { regs_.write_gpr(idx, val); }
    uint32_t read_gpr(uint8_t idx) const noexcept { return regs_.read_gpr(idx); }
    bool    acceleration() const noexcept { return accel_; }
    const TargetProfile& target() const noexcept { return target_; }
    BlockCache& block_cache() noexcept { return block_cache_; }
    const BlockCache& block_cache() const noexcept { return block_cache_; }
    // Fast path: run with block-cache translation (same semantics, fewer decodes)
    void run_fast();
    void reset();

    // --- Program / data loading ---
    void load_program(const uint32_t* instructions, size_t count);
    void load_program(const uint8_t*  bytes,  size_t count);
    void load_data(uint32_t addr, const uint8_t* data, size_t count);
    void load_main_ram(uint32_t addr, const uint8_t* data, size_t count);
    void load_tensor(uint32_t addr, const float* data, size_t count);

    // --- Execution ---
    void run();
    void step();

    // --- Debugger control ---
    Debugger& debugger() noexcept { return debugger_; }
    void add_breakpoint(uint32_t addr)     { debugger_.add_breakpoint(addr); }
    void remove_breakpoint(uint32_t addr)  { debugger_.remove_breakpoint(addr); }
    void clear_breakpoints()               { debugger_.clear_breakpoints(); }
    void enable_single_step()              { debugger_.enable_single_step(); }
    void disable_single_step()             { debugger_.disable_single_step(); }

    // --- Accessors ---
    uint32_t        pc()        const noexcept { return regs_.pc; }
    const RegisterFile& regs()  const noexcept { return regs_; }
    CSRFile& csr() noexcept { return csr_; }
    const CSRFile& csr() const noexcept { return csr_; }
    MMU& mmu() noexcept { return mmu_; }
    const MMU& mmu() const noexcept { return mmu_; }
    Interconnect& interconnect() noexcept { return interconnect_; } // default AXI4
    Interconnect& axi() noexcept { return axi_; }
    Interconnect& pcie() noexcept { return pcie_; }
    Interconnect& cxi() noexcept { return cxi_; }
    InterruptController& interrupt_controller() noexcept { return intc_; }
    TranslationEngine& translator() noexcept { return translator_; }
    VirtualSRAM&       memory()       noexcept { return scratchpad_; }
    const VirtualSRAM& memory() const noexcept { return scratchpad_; }
    MainRAM&           main_ram()     noexcept { return main_ram_; }
    const Profiler&    profiler()   const noexcept { return profiler_; }
    Bus&               bus()          noexcept { return bus_; }
    const Bus&         bus()    const noexcept { return bus_; }
    TimerDevice&       timer()        noexcept { return timer_; }
    UartDevice&        uart()         noexcept { return uart_; }
    bool               halted()       const noexcept { return regs_.halt; }

    // --- Debug / disassembly ---
    std::string disassemble_at(uint32_t addr) const;

    // --- State serialization (save / restore) ---
    struct State {
        RegisterFile::StatusFlags status;
        std::array<uint32_t, NUM_GPR> gpr;
        std::array<std::array<float, VEC_WIDTH>, NUM_VR> vr;
        uint32_t pc;
        uint64_t tick_accum;
        Profiler::State profiler;
    };
    State save_state() const;
    void  restore_state(const State& s);

private:
    // --- Pipeline ---
    Instruction fetch();
    uint32_t    execute(const Instruction& instr);

    // --- Jump table ---
    using Handler = uint32_t (Emulator::*)(const Instruction&);
    std::array<Handler, NUM_OPCODES> dispatch_table_{};
    void init_dispatch_table();

    // --- Instruction handlers (return cycles consumed) ---
    uint32_t op_nop(const Instruction&);
    uint32_t op_halt(const Instruction&);
    uint32_t op_jmp(const Instruction&);
    uint32_t op_beq(const Instruction&);
    uint32_t op_bne(const Instruction&);
    uint32_t op_add(const Instruction&);
    uint32_t op_sub(const Instruction&);
    uint32_t op_mul(const Instruction&);
    uint32_t op_load(const Instruction&);
    uint32_t op_store(const Instruction&);
    uint32_t op_mov(const Instruction&);
    uint32_t op_movi(const Instruction&);
    uint32_t op_load_vr(const Instruction&);
    uint32_t op_store_vr(const Instruction&);
    uint32_t op_vec_add(const Instruction&);
    uint32_t op_matmul_tile(const Instruction&);
    uint32_t op_dma_load(const Instruction&);
    uint32_t op_dma_store(const Instruction&);

    // --- Logging ---
    void log_step(uint32_t pc, const Instruction& instr, uint32_t cycles);

    // --- Peripheral tick ---
    void tick_peripherals();

    // --- Members ---
    VirtualSRAM  scratchpad_;
    MainRAM      main_ram_;
    RegisterFile regs_;
    CSRFile      csr_;
    MMU          mmu_;
    Bus          bus_;
    // Synchronous fabrics
    Interconnect interconnect_; // alias to axi_
    Interconnect axi_{InterconnectConfig{FabricProtocol::AXI4, 64, 2, 8, 16}};
    Interconnect pcie_{InterconnectConfig{FabricProtocol::PCIe, 64, 4, 4, 8}};
    Interconnect cxi_{InterconnectConfig{FabricProtocol::CXI, 64, 3, 16, 16}};
    TimerDevice  timer_;
    UartDevice   uart_;
    Profiler     profiler_;
    Debugger     debugger_;
    InterruptController intc_{&csr_};
    TranslationEngine translator_{GuestISA::Native};
    TargetProfile target_    = make_asic_profile();
    BlockCache   block_cache_;
    bool         accel_       = true; // host SIMD acceleration
    bool         verbose_     = false;
    uint64_t     tick_accum_  = 0;
    uint32_t     last_pc_     = 0;
    std::array<uint32_t, NUM_GPR> last_gpr_{};
};

} // namespace aiasm

// ============================================================================
// C-ABI interface (for shared-library export)
//
// These functions use C linkage and live at global scope so they can be
// resolved via dlopen / GetProcAddress from any host language.
// Pure-C consumers should include <aiasm/aism.h> directly.
// ============================================================================

#if defined(_WIN32) || defined(__CYGWIN__)
  #if defined(AISM_EXPORTS)
    #define AISM_API __declspec(dllexport)
  #elif defined(AISM_USING_DLL)
    #define AISM_API __declspec(dllimport)
  #else
    #define AISM_API  /* static library or standalone */
  #endif
#else
  #define AISM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef aiasm::Emulator AismEmulator;

AISM_API AismEmulator* aism_create(uint32_t scratchpad_bytes, uint32_t main_ram_bytes);
AISM_API void          aism_destroy(AismEmulator* emu);
AISM_API void          aism_reset(AismEmulator* emu);

AISM_API int  aism_load_program(AismEmulator* emu, const uint32_t* instrs, uint32_t count);
AISM_API int  aism_load_main_ram(AismEmulator* emu, uint32_t offset, const uint8_t* data, uint32_t count);
AISM_API int  aism_load_tensor(AismEmulator* emu, uint32_t addr, const float* data, uint32_t count);
AISM_API int  aism_write_scratchpad(AismEmulator* emu, uint32_t addr, const uint8_t* data, uint32_t count);
AISM_API int  aism_write32(AismEmulator* emu, uint32_t addr, uint32_t value);
AISM_API uint32_t aism_read32(AismEmulator* emu, uint32_t addr);

AISM_API void aism_run(AismEmulator* emu);
AISM_API void aism_step(AismEmulator* emu);
AISM_API void aism_set_verbose(AismEmulator* emu, int verbose);
AISM_API int  aism_halted(AismEmulator* emu);

AISM_API void aism_add_breakpoint(AismEmulator* emu, uint32_t addr);
AISM_API void aism_remove_breakpoint(AismEmulator* emu, uint32_t addr);
AISM_API void aism_clear_breakpoints(AismEmulator* emu);
AISM_API int  aism_has_breakpoint(AismEmulator* emu, uint32_t addr);
AISM_API void aism_enable_single_step(AismEmulator* emu);
AISM_API void aism_disable_single_step(AismEmulator* emu);

AISM_API uint32_t aism_pc(AismEmulator* emu);
AISM_API void    aism_read_gpr(AismEmulator* emu, uint8_t idx, uint32_t* out);
AISM_API void    aism_read_vr(AismEmulator* emu, uint8_t idx, float* out, uint32_t count);

AISM_API uint64_t aism_instruction_count(AismEmulator* emu);
AISM_API uint64_t aism_cycle_count(AismEmulator* emu);
AISM_API uint64_t aism_dma_bytes(AismEmulator* emu);
AISM_API double   aism_dma_throughput(AismEmulator* emu);

AISM_API int aism_disassemble_at(AismEmulator* emu, uint32_t addr, char* buf, uint32_t bufsize);

// --- Target / host translation ---
AISM_API void aism_set_target_cpu(AismEmulator* emu);
AISM_API void aism_set_target_gpu(AismEmulator* emu);
AISM_API void aism_set_target_asic(AismEmulator* emu);
AISM_API void aism_set_acceleration(AismEmulator* emu, int enable);
AISM_API const char* aism_host_info(void);
AISM_API void aism_run_fast(AismEmulator* emu);
AISM_API uint64_t aism_block_cache_hits(AismEmulator* emu);
AISM_API uint64_t aism_block_cache_misses(AismEmulator* emu);
AISM_API double   aism_block_cache_hit_rate(AismEmulator* emu);

#ifdef __cplusplus
} // extern "C"
#endif
