#include "aiasm/emulator.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace aiasm {

// ============================================================================
// Profiler implementation
// ============================================================================

void Profiler::reset() {
    instr_count_ = 0;
    cycle_count_ = 0;
    dma_bytes_ = 0;
    dma_cycle_count_ = 0;
    opcode_hist_.fill(0);
}

void Profiler::record(Opcode op, uint32_t cycles, uint64_t dma_bytes) {
    instr_count_++;
    cycle_count_ += cycles;
    opcode_hist_[static_cast<uint8_t>(op)]++;

    if (dma_bytes > 0) {
        dma_bytes_ += dma_bytes;
        dma_cycle_count_ += cycles;
    }
}

double Profiler::ipc() const noexcept {
    return cycle_count_ > 0
         ? static_cast<double>(instr_count_) / static_cast<double>(cycle_count_)
         : 0.0;
}

double Profiler::dma_throughput() const noexcept {
    return dma_cycle_count_ > 0
         ? static_cast<double>(dma_bytes_) / static_cast<double>(dma_cycle_count_)
         : 0.0;
}

Profiler::State Profiler::save() const {
    State s;
    s.instr_count    = instr_count_;
    s.cycle_count    = cycle_count_;
    s.dma_bytes      = dma_bytes_;
    s.dma_cycles     = dma_cycle_count_;
    s.opcode_hist    = opcode_hist_;
    return s;
}

void Profiler::restore(const State& s) {
    instr_count_    = s.instr_count;
    cycle_count_    = s.cycle_count;
    dma_bytes_      = s.dma_bytes;
    dma_cycle_count_ = s.dma_cycles;
    opcode_hist_    = s.opcode_hist;
}

std::string Profiler::report() const {
    static const char* names[NUM_OPCODES] = {
        "NOP","HALT","JMP","BEQ","BNE","ADD","SUB","MUL",
        "LOAD","STORE","MOV","MOVI","LOAD_VR","STORE_VR",
        "","",
        "VEC_ADD","MATMUL_TILE","DMA_LOAD","DMA_STORE",
        "","","","","","","","","",""
    };

    char buf[256];
    std::string s;
    s.reserve(1024);

    std::snprintf(buf, sizeof(buf),
        "=== Performance Report ===\n"
        "Instructions:    %llu\n"
        "Total cycles:    %llu\n"
        "IPC:             %.4f\n"
        "DMA bytes:       %llu\n"
        "DMA cycles:      %llu\n"
        "DMA throughput:  %.2f bytes/cycle\n"
        "\nOpcode histogram:\n",
        static_cast<unsigned long long>(instr_count_),
        static_cast<unsigned long long>(cycle_count_),
        ipc(),
        static_cast<unsigned long long>(dma_bytes_),
        static_cast<unsigned long long>(dma_cycle_count_),
        dma_throughput());
    s += buf;

    for (size_t i = 0; i < NUM_OPCODES; i++) {
        if (opcode_hist_[i] > 0) {
            std::snprintf(buf, sizeof(buf), "  [%2zu] %-12s %llu\n",
                          i, names[i] ? names[i] : "???",
                          static_cast<unsigned long long>(opcode_hist_[i]));
            s += buf;
        }
    }
    return s;
}

// ============================================================================
// Debugger implementation — dump helpers
// ============================================================================

std::string Debugger::dump_registers(const RegisterFile& regs) const {
    std::string s;
    s.reserve(512);

    for (size_t i = 0; i < NUM_GPR; i++) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "r%d=0x%08X ",
                      static_cast<int>(i), regs.gpr[i]);
        s += buf;
    }
    s += "\n";

    for (size_t i = 0; i < NUM_VR; i++) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "v%d: ", static_cast<int>(i));
        s += buf;
        for (size_t j = 0; j < VEC_WIDTH; j++) {
            char fbuf[16];
            std::snprintf(fbuf, sizeof(fbuf), "%6.1f ", regs.vr[i][j]);
            s += fbuf;
        }
        s += "\n";
    }

    char buf[128];
    std::snprintf(buf, sizeof(buf), "PC=0x%08X  Z=%d N=%d V=%d\n",
                  regs.pc, regs.status.zero, regs.status.negative, regs.status.overflow);
    s += buf;
    return s;
}

std::string Debugger::dump_memory(const VirtualSRAM& mem, uint32_t addr, size_t count) const {
    std::string s;
    s.reserve(count * 20);

    for (size_t i = 0; i < count; i += 16) {
        char line[160];
        int len = std::snprintf(line, sizeof(line), "%08X: ", addr + static_cast<uint32_t>(i));
        for (size_t j = 0; j < 16 && i + j < count; j++) {
            len += std::snprintf(line + len, sizeof(line) - len, "%02X ",
                                 mem.ptr(addr + static_cast<uint32_t>(i + j))[0]);
        }
        s += line;
        s += "\n";
    }
    return s;
}

std::string Debugger::disassemble_range(const VirtualSRAM& mem, uint32_t start, size_t count) const {
    std::string s;
    s.reserve(count * 48);

    for (size_t i = 0; i < count; i++) {
        uint32_t addr = start + static_cast<uint32_t>(i * 4);
        uint32_t raw = mem.read32(addr);
        Instruction instr{.raw = raw};
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%08X: %08X  %s\n",
                      addr, raw, disassemble(instr).c_str());
        s += buf;
    }
    return s;
}

// ============================================================================
// Emulator implementation
// ============================================================================

Emulator::Emulator(size_t scratchpad_bytes, size_t main_ram_bytes)
    : scratchpad_(scratchpad_bytes)
    , main_ram_(main_ram_bytes)
    , bus_(scratchpad_)
    , interconnect_(InterconnectConfig{FabricProtocol::AXI4, 64, 2, 8, 16})
    , axi_(InterconnectConfig{FabricProtocol::AXI4, 64, 2, 8, 16})
    , pcie_(InterconnectConfig{FabricProtocol::PCIe, 64, 4, 4, 8})
    , cxi_(InterconnectConfig{FabricProtocol::CXI, 64, 3, 16, 16})
    , tick_accum_(0)
{
    init_dispatch_table();
    csr_.reset();
    mmu_.attach_ram(&scratchpad_);
    mmu_.set_csr(&csr_);
    intc_.attach_csr(&csr_);
    // Interconnect is synchronous — no extra wiring needed, DMA ops will call axi_.issue()

    // --- Register MMIO devices on the bus ---
    bus_.add_device(MMIO_BASE, 0x1000,
        [this](uint32_t addr) { return timer_.read_reg(addr - MMIO_BASE); },
        [this](uint32_t addr, uint32_t val) { timer_.write_reg(addr - MMIO_BASE, val); });

    bus_.add_device(MMIO_BASE + UART_BASE, 0x1000,
        [this](uint32_t addr) { return uart_.read_reg(addr - MMIO_BASE - UART_BASE); },
        [this](uint32_t addr, uint32_t val) { uart_.write_reg(addr - MMIO_BASE - UART_BASE, val); });
}

void Emulator::init_dispatch_table() {
    dispatch_table_.fill(nullptr);

    dispatch_table_[static_cast<uint8_t>(Opcode::NOP)]         = &Emulator::op_nop;
    dispatch_table_[static_cast<uint8_t>(Opcode::HALT)]        = &Emulator::op_halt;
    dispatch_table_[static_cast<uint8_t>(Opcode::JMP)]         = &Emulator::op_jmp;
    dispatch_table_[static_cast<uint8_t>(Opcode::BEQ)]         = &Emulator::op_beq;
    dispatch_table_[static_cast<uint8_t>(Opcode::BNE)]         = &Emulator::op_bne;
    dispatch_table_[static_cast<uint8_t>(Opcode::ADD)]         = &Emulator::op_add;
    dispatch_table_[static_cast<uint8_t>(Opcode::SUB)]         = &Emulator::op_sub;
    dispatch_table_[static_cast<uint8_t>(Opcode::MUL)]         = &Emulator::op_mul;
    dispatch_table_[static_cast<uint8_t>(Opcode::LOAD)]        = &Emulator::op_load;
    dispatch_table_[static_cast<uint8_t>(Opcode::STORE)]       = &Emulator::op_store;
    dispatch_table_[static_cast<uint8_t>(Opcode::MOV)]         = &Emulator::op_mov;
    dispatch_table_[static_cast<uint8_t>(Opcode::MOVI)]        = &Emulator::op_movi;
    dispatch_table_[static_cast<uint8_t>(Opcode::LOAD_VR)]     = &Emulator::op_load_vr;
    dispatch_table_[static_cast<uint8_t>(Opcode::STORE_VR)]    = &Emulator::op_store_vr;
    dispatch_table_[static_cast<uint8_t>(Opcode::VEC_ADD)]     = &Emulator::op_vec_add;
    dispatch_table_[static_cast<uint8_t>(Opcode::MATMUL_TILE)] = &Emulator::op_matmul_tile;
    dispatch_table_[static_cast<uint8_t>(Opcode::DMA_LOAD)]    = &Emulator::op_dma_load;
    dispatch_table_[static_cast<uint8_t>(Opcode::DMA_STORE)]   = &Emulator::op_dma_store;
}

void Emulator::set_target(TargetProfile p) {
    target_ = std::move(p);
    block_cache_.clear();
}
void Emulator::set_target_kind(TargetKind k) {
    switch (k) {
        case TargetKind::CPU:  target_ = make_cpu_profile(); break;
        case TargetKind::GPU:  target_ = make_gpu_profile(); break;
        case TargetKind::ASIC: target_ = make_asic_profile(); break;
    }
    block_cache_.clear();
}

void Emulator::reset() {
    regs_.reset();
    csr_.reset();
    mmu_.tlb_flush();
    mmu_.set_enabled(false);
    axi_.reset(); pcie_.reset(); cxi_.reset(); interconnect_.reset();
    intc_.reset(); intc_.attach_csr(&csr_); mmu_.set_csr(&csr_);
    scratchpad_.clear();
    main_ram_.clear();
    profiler_.reset();
    debugger_.clear_breakpoints();
    debugger_.disable_single_step();
    verbose_ = false;
    tick_accum_ = 0;
    last_pc_ = 0;
    last_gpr_.fill(0);
    block_cache_.clear();
}

// --- Program / data loading ---

void Emulator::load_program(const uint32_t* instructions, size_t count) {
    load_program(reinterpret_cast<const uint8_t*>(instructions), count * sizeof(uint32_t));
}

void Emulator::load_program(const uint8_t* bytes, size_t count) {
    if (count > TEXT_SIZE)
        throw EmulatorError("Program size (" + std::to_string(count)
            + " bytes) exceeds .text region (" + std::to_string(TEXT_SIZE) + " bytes)");
    scratchpad_.load(TEXT_BASE, bytes, count);
    block_cache_.clear();
}

void Emulator::load_data(uint32_t addr, const uint8_t* data, size_t count) {
    scratchpad_.load(addr, data, count);
}

void Emulator::load_main_ram(uint32_t addr, const uint8_t* data, size_t count) {
    main_ram_.write(addr, data, count);
}

void Emulator::load_tensor(uint32_t addr, const float* data, size_t count) {
    scratchpad_.load(addr, reinterpret_cast<const uint8_t*>(data), count * sizeof(float));
}

// --- Execution ---

void Emulator::run() {
    while (!regs_.halt && !debugger_.has_breakpoint(regs_.pc)) {
        step();
    }
    uart_.flush();
}

void Emulator::run_fast() {
    // Block-cache accelerated path — same semantics, but decodes each
    // basic block once and then replays from cache. Hot kernels still
    // lower to host SIMD via host:: helpers.
    while (!regs_.halt && !debugger_.has_breakpoint(regs_.pc)) {
        if (verbose_) { step(); continue; }
        const DecodedBlock& blk = block_cache_.translate(scratchpad_, regs_.pc);
        for (const auto& ins : blk.ops) {
            if (regs_.halt || debugger_.has_breakpoint(regs_.pc)) break;
            // fetch already done by translate; emulate PC advance
            regs_.pc += 4;
            uint32_t cyc = execute(ins);
            tick_peripherals();
            if (ins.opcode() == Opcode::JMP || ins.opcode() == Opcode::BEQ ||
                ins.opcode() == Opcode::BNE || ins.opcode() == Opcode::HALT) break;
            (void)cyc;
        }
    }
    uart_.flush();
}

void Emulator::step() {
    if (regs_.halt)
        return;

    // --- Trap: check pending interrupts before fetch (like QEMU) ---
    if (auto irq = intc_.pending_interrupt()) {
        intc_.take_trap(*irq);
        uint64_t mtvec = csr_.read(CSR_MTVEC);
        uint32_t base = uint32_t(mtvec & ~0x3ULL);
        uint32_t cause = uint32_t(static_cast<uint16_t>(irq->cause) & 0xFF);
        bool vectored = (mtvec & 1) != 0;
        regs_.pc = vectored ? base + cause * 4 : base;
    }

    // Save before-state for verbose diff
    uint32_t pc_before = regs_.pc;
    if (verbose_) {
        last_pc_ = pc_before;
        for (size_t i = 0; i < NUM_GPR; i++)
            last_gpr_[i] = regs_.read_gpr(static_cast<uint8_t>(i));
    }

    Instruction instr = fetch();

    uint32_t cycles = execute(instr);

    tick_peripherals();

    if (verbose_)
        log_step(pc_before, instr, cycles);
}

// --- Pipeline stages ---

Instruction Emulator::fetch() {
    uint32_t va = regs_.pc;
    if (mmu_.enabled()) {
        PageFaultCause cause = PageFaultCause::None;
        auto pa = mmu_.translate(va, false, true, &cause);
        if (!pa) {
            auto trap = intc_.page_fault_trap(va, va, false);
            trap.cause = TrapCause::InstPageFault;
            intc_.take_trap(trap);
            uint64_t mtvec = csr_.read(CSR_MTVEC);
            regs_.pc = uint32_t(mtvec & ~0x3ULL);
            return Instruction{enc::nop()};
        }
        uint32_t raw;
        std::memcpy(&raw, scratchpad_.ptr(*pa), 4);
        regs_.pc += 4;
        return Instruction{.raw = raw};
    }
    uint32_t raw = bus_.read32(va);
    regs_.pc += 4;
    return Instruction{.raw = raw};
}

uint32_t Emulator::execute(const Instruction& instr) {
    auto handler = dispatch_table_[static_cast<uint8_t>(instr.opcode())];
    if (!handler) {
        // Trap: illegal instruction — architectural trap to mtvec
        auto trap = intc_.illegal_inst_trap(regs_.pc - 4, instr.raw);
        if (csr_.read(CSR_MTVEC) != 0) {
            intc_.take_trap(trap);
            uint64_t mtvec = csr_.read(CSR_MTVEC);
            regs_.pc = uint32_t(mtvec & ~0x3ULL);
            return 2; // trap entry cost
        }
        throw EmulatorError("Unknown opcode: 0x"
            + std::to_string(static_cast<int>(instr.opcode())));
    }
    uint32_t cycles = (this->*handler)(instr);

    // Compute DMA bytes for profiler
    uint64_t dma_bytes = 0;
    if (instr.opcode() == Opcode::DMA_LOAD || instr.opcode() == Opcode::DMA_STORE)
        dma_bytes = instr.func();

    profiler_.record(instr.opcode(), cycles, dma_bytes);
    tick_accum_ += cycles;
    return cycles;
}

// ============================================================================
// Instruction handlers
// ============================================================================

uint32_t Emulator::op_nop(const Instruction&) {
    return cycle_weight(Opcode::NOP);
}

uint32_t Emulator::op_halt(const Instruction&) {
    regs_.halt = true;
    return 0;  // HALT has zero cost
}

uint32_t Emulator::op_jmp(const Instruction& instr) {
    regs_.pc = instr.target();
    return cycle_weight(Opcode::JMP);
}

uint32_t Emulator::op_beq(const Instruction& instr) {
    uint32_t rs = regs_.read_gpr(instr.rd());    // first source (bits 25-21)
    uint32_t rt = regs_.read_gpr(instr.ra());    // second source (bits 20-16)
    uint32_t cycles = cycle_weight(Opcode::BEQ);
    if (rs == rt) {
        regs_.pc += instr.offset();   // offset is relative to PC after fetch
        cycles += 1;                   // branch-taken penalty
    }
    return cycles;
}

uint32_t Emulator::op_bne(const Instruction& instr) {
    uint32_t rs = regs_.read_gpr(instr.rd());    // first source (bits 25-21)
    uint32_t rt = regs_.read_gpr(instr.ra());    // second source (bits 20-16)
    uint32_t cycles = cycle_weight(Opcode::BNE);
    if (rs != rt) {
        regs_.pc += instr.offset();
        cycles += 1;
    }
    return cycles;
}

uint32_t Emulator::op_add(const Instruction& instr) {
    uint32_t a = regs_.read_gpr(instr.ra());
    uint32_t b = regs_.read_gpr(instr.rb());
    uint32_t result = a + b;
    regs_.write_gpr(instr.rd(), result);

    // Overflow: both operands same sign, result different sign
    bool a_neg = (static_cast<int32_t>(a) < 0);
    bool b_neg = (static_cast<int32_t>(b) < 0);
    bool r_neg = (static_cast<int32_t>(result) < 0);
    regs_.status.overflow = (a_neg == b_neg) && (a_neg != r_neg);
    regs_.update_flags_u32(result);

    return cycle_weight(Opcode::ADD);
}

uint32_t Emulator::op_sub(const Instruction& instr) {
    uint32_t a = regs_.read_gpr(instr.ra());
    uint32_t b = regs_.read_gpr(instr.rb());
    uint32_t result = a - b;
    regs_.write_gpr(instr.rd(), result);

    bool a_neg = (static_cast<int32_t>(a) < 0);
    bool b_neg = (static_cast<int32_t>(b) < 0);
    bool r_neg = (static_cast<int32_t>(result) < 0);
    regs_.status.overflow = (a_neg != b_neg) && (a_neg != r_neg);
    regs_.update_flags_u32(result);

    return cycle_weight(Opcode::SUB);
}

uint32_t Emulator::op_mul(const Instruction& instr) {
    uint32_t a = regs_.read_gpr(instr.ra());
    uint32_t b = regs_.read_gpr(instr.rb());
    uint32_t result = a * b;
    regs_.write_gpr(instr.rd(), result);
    regs_.status.overflow = false;
    regs_.update_flags_u32(result);
    return cycle_weight(Opcode::MUL);
}

uint32_t Emulator::op_load(const Instruction& instr) {
    uint32_t base = regs_.read_gpr(instr.ra());
    uint32_t addr = base + static_cast<uint32_t>(instr.simm16());
    if (mmu_.enabled()) {
        PageFaultCause c; auto pa = mmu_.translate(addr, false, false, &c);
        if (!pa) { auto t=intc_.page_fault_trap(regs_.pc-4, addr, false); intc_.take_trap(t); regs_.pc=uint32_t(csr_.read(CSR_MTVEC)&~0x3ULL); return 2; }
        addr = *pa;
    }
    // AXI read latency
    Transaction t{0, addr, 4, false, profiler_.cycles(), 0, FabricProtocol::AXI4};
    axi_.issue(t, profiler_.cycles());
    uint32_t value = bus_.read32(addr);
    regs_.write_gpr(instr.rd(), value);
    return cycle_weight(Opcode::LOAD);
}

uint32_t Emulator::op_store(const Instruction& instr) {
    // STORE rs, ra, imm → mem[r[ra] + imm] = r[rs]
    // In encoding: rd = rs (source), ra = base, imm = offset
    uint32_t base  = regs_.read_gpr(instr.ra());
    uint32_t addr  = base + static_cast<uint32_t>(instr.simm16());
    if (mmu_.enabled()) {
        PageFaultCause c; auto pa = mmu_.translate(addr, true, false, &c);
        if (!pa) { auto t=intc_.page_fault_trap(regs_.pc-4, addr, true); intc_.take_trap(t); regs_.pc=uint32_t(csr_.read(CSR_MTVEC)&~0x3ULL); return 2; }
        addr = *pa;
    }
    uint32_t value = regs_.read_gpr(instr.rd());
    Transaction t{0, addr, 4, true, profiler_.cycles(), 0, FabricProtocol::AXI4};
    axi_.issue(t, profiler_.cycles());
    bus_.write32(addr, value);
    return cycle_weight(Opcode::STORE);
}

uint32_t Emulator::op_mov(const Instruction& instr) {
    uint32_t value = regs_.read_gpr(instr.ra());
    regs_.write_gpr(instr.rd(), value);
    return cycle_weight(Opcode::MOV);
}

uint32_t Emulator::op_movi(const Instruction& instr) {
    regs_.write_gpr(instr.rd(), instr.imm16());
    return cycle_weight(Opcode::MOVI);
}

uint32_t Emulator::op_load_vr(const Instruction& instr) {
    uint8_t  vr_idx = instr.rd();
    uint32_t base   = regs_.read_gpr(instr.ra());
    uint32_t addr   = base + static_cast<uint32_t>(instr.simm16());

    float* dst = regs_.write_vr(vr_idx);
    scratchpad_.read_bytes(addr, reinterpret_cast<uint8_t*>(dst), VR_BYTE_SIZE);
    return cycle_weight(Opcode::LOAD_VR);
}

uint32_t Emulator::op_store_vr(const Instruction& instr) {
    uint8_t  vr_idx = instr.rd();
    uint32_t base   = regs_.read_gpr(instr.ra());
    uint32_t addr   = base + static_cast<uint32_t>(instr.simm16());

    const float* src = regs_.read_vr(vr_idx);
    scratchpad_.write_bytes(addr, reinterpret_cast<const uint8_t*>(src), VR_BYTE_SIZE);
    return cycle_weight(Opcode::STORE_VR);
}

uint32_t Emulator::op_vec_add(const Instruction& instr) {
    uint8_t vd = instr.rd();
    uint8_t va = instr.ra();
    uint8_t vb = instr.rb();

    float*       d = regs_.write_vr(vd);
    const float* a = regs_.read_vr(va);
    const float* b = regs_.read_vr(vb);

    if (accel_) host::vec_add_host(d, a, b);
    else for (size_t i = 0; i < VEC_WIDTH; i++) d[i] = a[i] + b[i];

    return profile_cycle_weight(target_, Opcode::VEC_ADD);
}

uint32_t Emulator::op_matmul_tile(const Instruction& instr) {
    uint8_t n  = static_cast<uint8_t>(instr.func() & 0xF);
    if (n == 0 || n > TILE_SIZE)
        throw EmulatorError("MATMUL_TILE: invalid tile size N=" + std::to_string(n));

    uint8_t vd = instr.rd();
    uint8_t va = instr.ra();
    uint8_t vb = instr.rb();

    float*       d = regs_.write_vr(vd);
    const float* a = regs_.read_vr(va);
    const float* b = regs_.read_vr(vb);

    if (accel_) host::matmul_tile_host(d, a, b, n);
    else {
        for (uint8_t i = 0; i < n; i++) {
            for (uint8_t j = 0; j < n; j++) {
                float sum = 0.0f;
                for (uint8_t k = 0; k < n; k++)
                    sum += a[i * n + k] * b[k * n + j];
                d[i * n + j] = sum;
            }
        }
    }
    return profile_cycle_weight(target_, Opcode::MATMUL_TILE, n);
}

uint32_t Emulator::op_dma_load(const Instruction& instr) {
    uint32_t dest  = regs_.read_gpr(instr.rd());
    uint32_t src   = regs_.read_gpr(instr.ra());
    uint32_t bytes = instr.func();

    // MMU translate for src/dest if enabled
    if (mmu_.enabled()) {
        PageFaultCause c;
        auto pa_dest = mmu_.translate(dest, true, false, &c);
        auto pa_src = mmu_.translate(src, false, false, &c);
        if (!pa_dest || !pa_src) {
            auto trap = intc_.page_fault_trap(regs_.pc-4, !pa_dest?dest:src, !pa_dest);
            intc_.take_trap(trap);
            regs_.pc = uint32_t(csr_.read(CSR_MTVEC) & ~0x3ULL);
            return 2;
        }
        dest = *pa_dest; src = *pa_src;
    }

    if (dest + bytes > scratchpad_.size())
        throw EmulatorError("DMA_LOAD: scratchpad address out of bounds");
    if (src + bytes > main_ram_.size())
        throw EmulatorError("DMA_LOAD: main RAM address out of bounds");

    // Synchronous interconnect: AXI burst
    Transaction t{0, dest, bytes, false, profiler_.cycles(), 0, FabricProtocol::AXI4};
    if (!axi_.issue(t, profiler_.cycles())) profiler_.record(Opcode::DMA_LOAD, 1, 0); // backpressure
    host::dma_copy_host(scratchpad_.ptr(dest), main_ram_.ptr(src), bytes);
    return profile_cycle_weight(target_, Opcode::DMA_LOAD, bytes);
}

uint32_t Emulator::op_dma_store(const Instruction& instr) {
    uint32_t dest  = regs_.read_gpr(instr.rd());
    uint32_t src   = regs_.read_gpr(instr.ra());
    uint32_t bytes = instr.func();

    if (mmu_.enabled()) {
        PageFaultCause c;
        auto pa_dest = mmu_.translate(dest, true, false, &c);
        auto pa_src = mmu_.translate(src, false, false, &c);
        if (!pa_dest || !pa_src) {
            auto trap = intc_.page_fault_trap(regs_.pc-4, !pa_dest?dest:src, true);
            intc_.take_trap(trap);
            regs_.pc = uint32_t(csr_.read(CSR_MTVEC) & ~0x3ULL);
            return 2;
        }
        dest = *pa_dest; src = *pa_src;
    }

    if (dest + bytes > main_ram_.size())
        throw EmulatorError("DMA_STORE: main RAM address out of bounds");
    if (src + bytes > scratchpad_.size())
        throw EmulatorError("DMA_STORE: scratchpad address out of bounds");

    Transaction t{0, src, bytes, true, profiler_.cycles(), 0, FabricProtocol::AXI4};
    axi_.issue(t, profiler_.cycles());
    host::dma_copy_host(main_ram_.ptr(dest), scratchpad_.ptr(src), bytes);
    return profile_cycle_weight(target_, Opcode::DMA_STORE, bytes);
}

// --- Peripheral ticking ---

void Emulator::tick_peripherals() {
    while (tick_accum_ >= CYCLES_PER_TICK) {
        timer_.tick(CYCLES_PER_TICK);
        uart_.tick(CYCLES_PER_TICK);
        // Propagate timer IRQ to interrupt controller
        if (timer_.interrupt_pending()) intc_.raise_timer();
        else intc_.clear_timer();
        // Advance synchronous fabrics
        axi_.tick(CYCLES_PER_TICK);
        pcie_.tick(CYCLES_PER_TICK);
        cxi_.tick(CYCLES_PER_TICK);
        interconnect_.tick(CYCLES_PER_TICK);
        tick_accum_ -= CYCLES_PER_TICK;
    }
}

// --- Disassembly ---

std::string Emulator::disassemble_at(uint32_t addr) const {
    uint32_t raw = scratchpad_.read32(addr);
    Instruction instr{.raw = raw};
    return disassemble(instr);
}

// --- State serialization ---

Emulator::State Emulator::save_state() const {
    State s;
    s.status      = regs_.status;
    s.gpr         = regs_.gpr;
    s.vr          = regs_.vr;
    s.pc          = regs_.pc;
    s.tick_accum  = tick_accum_;
    s.profiler    = profiler_.save();
    return s;
}

void Emulator::restore_state(const State& s) {
    regs_.status     = s.status;
    regs_.gpr        = s.gpr;
    regs_.vr         = s.vr;
    regs_.pc         = s.pc;
    tick_accum_      = s.tick_accum;
    profiler_.restore(s.profiler);
    // Ensure r0 = 0
    regs_.gpr[0] = 0;
}

// --- Logging ---

void Emulator::log_step(uint32_t pc, const Instruction& instr, uint32_t cycles) {
    std::string dis = disassemble(instr);
    char line[256];
    std::snprintf(line, sizeof(line),
        "[0x%08X] 0x%08X  %-32s %u cy",
        pc, instr.raw, dis.c_str(), cycles);
    std::printf("%s", line);

    // Print changed GPRs
    for (size_t i = 0; i < NUM_GPR; i++) {
        uint32_t after = regs_.read_gpr(static_cast<uint8_t>(i));
        if (after != last_gpr_[i])
            std::printf("  r%d=0x%08X", static_cast<int>(i), after);
    }

    // Print PC change for control-flow instructions
    if (regs_.pc != pc + 4)
        std::printf("  ->PC=0x%08X", regs_.pc);

    // Print DMA info
    if (instr.opcode() == Opcode::DMA_LOAD || instr.opcode() == Opcode::DMA_STORE)
        std::printf("  DMA=%uB", instr.func());

    std::printf("  [Z=%d N=%d V=%d]\n",
                regs_.status.zero, regs_.status.negative, regs_.status.overflow);
}

} // namespace aiasm

// Bring C++ symbols into global scope for the C-ABI wrappers
using aiasm::Emulator;
using aiasm::VEC_WIDTH;

// ============================================================================
// C-ABI interface (global scope, C linkage)
// ============================================================================

extern "C" {

AismEmulator* aism_create(uint32_t scratchpad_bytes, uint32_t main_ram_bytes) {
    try {
        size_t s = scratchpad_bytes == 0 ? aiasm::SCRATCHPAD_SIZE : scratchpad_bytes;
        size_t m = main_ram_bytes == 0 ? aiasm::MAIN_RAM_SIZE : main_ram_bytes;
        return new Emulator(s, m);
    } catch (...) {
        return nullptr;
    }
}

void aism_destroy(AismEmulator* emu) {
    delete emu;
}

void aism_reset(AismEmulator* emu) {
    emu->reset();
}

int aism_load_program(AismEmulator* emu, const uint32_t* instrs, uint32_t count) {
    try {
        emu->load_program(instrs, count);
        return 0;
    } catch (...) {
        return -1;
    }
}

int aism_load_main_ram(AismEmulator* emu, uint32_t offset, const uint8_t* data, uint32_t count) {
    try {
        emu->load_main_ram(offset, data, count);
        return 0;
    } catch (...) {
        return -1;
    }
}

int aism_load_tensor(AismEmulator* emu, uint32_t addr, const float* data, uint32_t count) {
    try {
        emu->load_tensor(addr, data, count);
        return 0;
    } catch (...) {
        return -1;
    }
}

int aism_write_scratchpad(AismEmulator* emu, uint32_t addr, const uint8_t* data, uint32_t count) {
    try {
        emu->memory().write_bytes(addr, data, count);
        return 0;
    } catch (...) {
        return -1;
    }
}

int aism_write32(AismEmulator* emu, uint32_t addr, uint32_t value) {
    try {
        emu->bus().write32(addr, value);
        return 0;
    } catch (...) {
        return -1;
    }
}

uint32_t aism_read32(AismEmulator* emu, uint32_t addr) {
    try {
        return emu->bus().read32(addr);
    } catch (...) {
        return 0;
    }
}

void aism_run(AismEmulator* emu) {
    try {
        emu->run();
    } catch (...) {}
}

void aism_step(AismEmulator* emu) {
    try {
        emu->step();
    } catch (...) {}
}

void aism_set_verbose(AismEmulator* emu, int verbose) {
    emu->set_verbose(verbose != 0);
}

int aism_halted(AismEmulator* emu) {
    return emu->halted() ? 1 : 0;
}

void aism_add_breakpoint(AismEmulator* emu, uint32_t addr) {
    emu->add_breakpoint(addr);
}

void aism_remove_breakpoint(AismEmulator* emu, uint32_t addr) {
    emu->remove_breakpoint(addr);
}

void aism_clear_breakpoints(AismEmulator* emu) {
    emu->clear_breakpoints();
}

int aism_has_breakpoint(AismEmulator* emu, uint32_t addr) {
    return emu->debugger().has_breakpoint(addr) ? 1 : 0;
}

void aism_enable_single_step(AismEmulator* emu) {
    emu->enable_single_step();
}

void aism_disable_single_step(AismEmulator* emu) {
    emu->disable_single_step();
}

uint32_t aism_pc(AismEmulator* emu) {
    return emu->pc();
}

void aism_read_gpr(AismEmulator* emu, uint8_t idx, uint32_t* out) {
    *out = emu->regs().read_gpr(idx);
}

void aism_read_vr(AismEmulator* emu, uint8_t idx, float* out, uint32_t count) {
    const float* vr = emu->regs().read_vr(idx);
    uint32_t n = count < VEC_WIDTH ? count : VEC_WIDTH;
    for (uint32_t i = 0; i < n; i++)
        out[i] = vr[i];
}

uint64_t aism_instruction_count(AismEmulator* emu) {
    return emu->profiler().instructions();
}

uint64_t aism_cycle_count(AismEmulator* emu) {
    return emu->profiler().cycles();
}

uint64_t aism_dma_bytes(AismEmulator* emu) {
    return emu->profiler().dma_bytes();
}

double aism_dma_throughput(AismEmulator* emu) {
    return emu->profiler().dma_throughput();
}

int aism_disassemble_at(AismEmulator* emu, uint32_t addr, char* buf, uint32_t bufsize) {
    try {
        std::string disasm = emu->disassemble_at(addr);
        std::snprintf(buf, bufsize, "%s", disasm.c_str());
        return 0;
    } catch (...) {
        std::snprintf(buf, bufsize, "???");
        return -1;
    }
}

void aism_set_target_cpu(AismEmulator* emu) { emu->set_target_kind(aiasm::TargetKind::CPU); }
void aism_set_target_gpu(AismEmulator* emu) { emu->set_target_kind(aiasm::TargetKind::GPU); }
void aism_set_target_asic(AismEmulator* emu) { emu->set_target_kind(aiasm::TargetKind::ASIC); }
void aism_set_acceleration(AismEmulator* emu, int enable) { emu->set_acceleration(enable != 0); }
const char* aism_host_info(void) {
    static std::string s = aiasm::host::host_feature_string();
    return s.c_str();
}
void aism_run_fast(AismEmulator* emu) { try { emu->run_fast(); } catch (...) {} }
uint64_t aism_block_cache_hits(AismEmulator* emu) { return emu->block_cache().hits(); }
uint64_t aism_block_cache_misses(AismEmulator* emu) { return emu->block_cache().misses(); }
double aism_block_cache_hit_rate(AismEmulator* emu) { return emu->block_cache().hit_rate(); }

} // extern "C"
