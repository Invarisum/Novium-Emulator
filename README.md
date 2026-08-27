# Novium Emulator v1.0 — `NOVIUM_VERSION_STRING` `v1.0.0` `include/aiasm/version.h:6`

Cycle-approximate emulator for a custom AI-acceleration ISA (16 GPRs, 8×16-lane vector registers, DMA, 4×4 systolic matmul) with full architectural simulation, host translation, and hardware-design co-verification.

> **v1.0** — First stable release. All subsystems validated, C-ABI stable, CoreMark + multi-core + trap ISA ready. See `VERSION` and `include/aiasm/version.h:6`.

## Architecture

```
Guest ISA (ARM64 / RISC-V / PTX / ASIC ucode)
        ↓  decoder.h:16 / translate.h:33 (QEMU-TCG style BlockCache)
   [Emulator]  emulator.h:45
   ├─ Register File  registers.h:18 + CSR  csr.h:14  (16 GPR r0=0, 8 VR, PC, mstatus/mtvec/satp, Priv U/S/M)
   ├─ MMU  mmu.h:14  (Sv39 3-level, 32-entry TLB, R/W/X/U, SFENCE.VMA)
   ├─ Bus  bus.h:25  + Interconnect  interconnect.h:14  (AXI4 / PCIe / CXI, latency/bandwidth, backpressure)
   ├─ Interrupt  interrupt.h:14  (CLINT/PLIC, mtvec/stvec, ECALL/page-fault/illegal, MRET/SRET)
   ├─ Pipeline  pipeline.h:14  (4-stage, RS 8, ROB 16, 2-bit predictor)
   ├─ Cache  cache.h:14  (L1 32K/8W, L2 256K, L3 2M MESI/MOESI + DDR5/HBM3 MC)
   ├─ Parallel  parallel.h:14  (Warp32 scheduler, 8×8 systolic array)
   ├─ Profiler  profiler.h:22  (cycles, IPC, DMA throughput, opcode hist)
   ├─ Debugger  debugger.h
   └─ Devices  devices.h:21  (Timer, UART)
        ↓  host.h:68  (x86_64 AVX2/AVX-512 ↔ aarch64 NEON/SVE, bit-identical FP32)
      Host CPU / GPU
```

Additional tooling is header-only and demonstrated via standalone objects in `main.cpp:365`:
`timing.h:14` (Functional/Approximate/Accurate), `deterministic.h:14` (quantum 1000), `checkpoint.h:14` (1024-entry reverse), `trace.h:14` (VCD/CSV, GDBStub), `tlm.h:14` (TLM-2.0 shim), `fpga.h:14` (PCIe/Sim offload), `pmc.h:14` (IPC, cache/TLB/DMA).

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
# optional: -DAISM_NATIVE=ON for -march=native, -DBUILD_SHARED_LIB=ON (default)
```

Compiler: C++20, GCC 11+ / Clang 14+ / MSVC 19.30+. On x86_64 `-mavx2 -mfma` auto-enabled (`CMakeLists.txt:18`), on ARM NEON/SVE when available. Output: `build/novium_emu.exe`, `build/libaism_core.a`, `build/libaism_emulator.dll`.

## Run

```bash
# Built-in integration test (scalar loop, DMA, VEC_ADD, MATMUL_TILE, verbose, breakpoints, MMIO)
.\build\novium_emu.exe

# Frontend — C++ / SystemVerilog / Chisel / guest asm
.\build\novium_emu.exe examples/coremark.cpp      # [[novium::kernel]] or // novium: vec_add
.\build\novium_emu.exe examples/accel.sv          # module npu_tile #(TILE=4,LANES=16,BW=64)
.\build\novium_emu.exe examples/npu_tile.scala    # class NPUTile extends Module
.\build\novium_emu.exe program.asm                # guest assembly (movi, vec_add, halt)

# Linux/macOS
./build/novium_emu
```

`main.cpp:32` dispatches via `frontend::Frontend::load(path)` `frontend.h:16` (extension → `SourceKind::Cpp/SystemVerilog/Chisel/GuestAsm`). Programmatic:

```cpp
#include "aiasm/frontend.h"
auto r = aiasm::frontend::Frontend::load("kernel.cpp");
Emulator emu; emu.set_target(r.target); emu.load_program(r.program.data(), r.program.size()); emu.run_fast();
```

Target profiles `target.h:16`:

```cpp
emu.set_target(make_cpu_profile());   // OoO, 8-lane
emu.set_target(make_gpu_profile());   // warp32, 4 tensor cores
emu.set_target(make_asic_profile());  // 4×4 reference (default)
emu.set_target(make_custom_profile("my-asic", 16, 2, 128));
```

Host translation `host.h:11` + `translate.h:33`: `VEC_ADD`→`_mm256_add_ps`/`vaddq_f32`, `MATMUL_TILE`→blocked `host::matmul_tile_host`, `DMA`→`memcpy` (rep movsb / LDP). Cycle counts via `profile_cycle_weight()` so wall time is host-fast, timing is target-accurate. `BlockCache` `translate.h:33` + `run_fast()` `emulator.h:49`.

## CoreMark

Synthetic CoreMark `coremark.h:14` `src/coremark.cpp:11` (list `ADD/BNE`, matrix `MATMUL_TILE`, state `MUL`, CRC):

```cpp
#include "aiasm/coremark.h"
Emulator emu; emu.set_target(make_cpu_profile());
auto r = coremark::run(emu, 100, true, mat_a, mat_b);
std::printf("%s", coremark::report(r).c_str());
// → CoreMark/MHz (cycles-accurate) + iter/sec (host wall)
```

`main.cpp:449` runs 100 iter on CPU/GPU/ASIC + C++ frontend `examples/coremark.cpp`.

## C-ABI (shared library)

Pure-C header `include/aiasm/aism.h` (opaque `AismEmulator*`, no C++):

```c
#include "aiasm/aism.h"
AismEmulator* emu = aism_create(0, 0); // 0 = 64 MB defaults
aism_load_program(emu, prog, n);
aism_set_verbose(emu, 1);
aism_run(emu);
uint64_t cyc = aism_cycle_count(emu);
aism_destroy(emu);
```

C++ may include `aiasm/emulator.h` directly (`aiasm::Emulator`) or the same `aism_*` symbols.

```bash
gcc -o my_app my_app.c build/libaism_core.a -Iinclude -std=c11 -lstdc++      # static
gcc -o my_app my_app.c -Lbuild -laism_emulator -Iinclude -std=c11 -DAISM_USING_DLL  # DLL
```

`tests/test_c_abi.c` verifies `MOVI+HALT`, profiler, `aism_reset`.

Additional C-ABI: `aism_set_target_cpu/gpu/asic`, `aism_set_acceleration`, `aism_host_info()`, `aism_run_fast`, `aism_block_cache_*`, `aism_disassemble_at`.

## Verification & Tooling

`main.cpp:305` demonstrates:

* **Pipeline/Cache/Parallel** standalone: `Pipeline` OoO stalls, `CacheHierarchy` L1/L2/L3 + `MemoryController` DDR5, `WarpScheduler`/`SystolicArray`.
* **Trace** `trace.h:14`: `InstructionTrace` + `VCDWriter` → `build/trace.vcd` (GTKWave) + CSV, `GDBStub` `:1234`.
* **TLM** `tlm.h:14`: `TLMSocket` `b_transport` shim → `Bus` (`tlm.cpp:1`).
* **FPGA** `fpga.h:14`: `FPGACoEmulator` `Simulated`/`PCIe` (`host::` fallback), `offloaded_ops()` counters.
* **PMC** `pmc.h:14`: `IPC`, cache/TLB miss, DMA bandwidth, `mcause` via CSR.

Example (standalone, no Emulator bloat):

```cpp
#include "aiasm/timing.h"
TimingController tc; tc.set_mode(TimingMode::CycleAccurate);
#include "aiasm/cache.h"
CacheHierarchy ch; ch.l1.access(0x1000,false,nullptr);
```

## ISA

`isa.h:15` — 6-bit opcode, `enc::` helpers:

```
R: [31:26] op | [25:21] rd | [20:16] ra | [15:11] rb | [10:0] func
I: [31:26] op | [25:21] rd | [20:16] ra | [15:0] imm
J: [31:26] op | [25:0] target
B: [31:26] op | [25:21] rs | [20:16] rt | [15:0] off
```

Ops: `NOP/HALT/JMP/BEQ/BNE/ADD/SUB/MUL/LOAD/STORE/MOV/MOVI/LOAD_VR/STORE_VR/VEC_ADD/MATMUL_TILE/DMA_LOAD/DMA_STORE` + trap `ECALL/EBREAK/CSRRW/MRET/SFENCE.VMA` (defined, handlers in `emulator.cpp:644`). `cycle_weight()` `isa.h:159` per-op, overridden by `TargetProfile`.

## Testing

`main.cpp` is the integration test; `tests/test_c_abi.c` is the C-ABI test. No external framework.

```bash
.\build\novium_emu.exe              # full suite, exits 0 on PASS
gcc -o build/test_c_abi.exe tests/test_c_abi.c build/libaism_core.a -Iinclude -std=c11 -lstdc++ && .\build\test_c_abi.exe
```

## C++ Standard

C++20 required.

---

##DEV notes

-Hello. Molor Davaa here. The Solo Lead developer of Novium Emulator. Starting from today, we're dropping the "patch version" semantic, so we will use 'major.minor' semantic from now on every project we do. 

---

##Credits

*Credits*: Thank you for Molor Davaa (@Uchiha Molsyh) for building this amazing project, and we wish you the best. 

---

##License

This project is under MIT license(see from license file). This product is copyrighted. 
