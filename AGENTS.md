# Novium Emulator v1.0 — Build & Development Guide (`VERSION` `v1.0.0`)

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Run

```bash
# Windows
.\build\novium_emu.exe

# Linux / macOS
./build/novium_emu
```

## Verbose Mode

Edit `src/main.cpp` and set `emu.set_verbose(true)` before `emu.run()` to see
a step-by-step execution trace.

## Shared Library (C-ABI)

The `aism_emulator` shared library exports stable C symbols (`aism_*` prefix).

**Pure C consumers** use the standalone header `include/aiasm/aism.h` (no C++
dependencies, uses opaque `AismEmulator*` handle):

```c
#include "aiasm/aism.h"

AismEmulator* emu = aism_create(0, 0);   // 0 = defaults (64 MB each)
aism_load_program(emu, program, count);
aism_set_verbose(emu, 1);
aism_run(emu);
uint64_t cycles = aism_cycle_count(emu);
aism_destroy(emu);
```

**C++ consumers** can include `aiasm/emulator.h` directly and use the
`aiasm::Emulator` class, or call the same `aism_*` C functions.

Compile against the static library (link `-lstdc++` for the C++ runtime):

```bash
gcc -o my_app my_app.c build/libaism_core.a -Iinclude -std=c11 -lstdc++
```

Or compile against the DLL:

```bash
gcc -o my_app my_app.c -Lbuild -laism_emulator -Iinclude -std=c11 -DAISM_USING_DLL
```

## Inputs — C++ / SystemVerilog / Chisel

The emulator accepts all three source languages via the frontend (`include/aiasm/frontend.h:16`):

```bash
# C++ kernel (pragma [[novium::kernel]] or // novium: annotations)
.\build\novium_emu.exe examples/coremark.cpp

# SystemVerilog NPU tile (parameter TILE/LANES/BW extracted → ASIC target)
.\build\novium_emu.exe examples/accel.sv

# Chisel (Scala) tile (class Foo extends Module → ASIC target)
.\build\novium_emu.exe examples/npu_tile.scala

# Guest assembly
.\build\novium_emu.exe program.asm
```

API:

```cpp
#include "aiasm/frontend.h"
auto r = aiasm::frontend::Frontend::load("kernel.cpp"); // or .sv/.scala
emu.set_target(r.target);
emu.load_program(r.program.data(), r.program.size());
emu.run_fast();
```

Host translation layer (`include/aiasm/host.h:11`, `include/aiasm/translate.h:33`) lowers guest `VEC_ADD`/`MATMUL_TILE`/`DMA` to host `AVX2`/`AVX512` on x86_64 or `NEON`/`SVE` on `aarch64` — bit-identical FP32, `profile_cycle_weight()` keeps timing target-accurate. `BlockCache` (`translate.h:33`) caches decoded blocks; `run_fast()` (`emulator.h:43`) replays them.

Target profiles (`include/aiasm/target.h:16`): `make_cpu_profile()` (OoO, 8-lane), `make_gpu_profile()` (warp32, 4 tensor cores), `make_asic_profile()` (NPU 4×4 reference), or `make_custom_profile(name,lanes,cores,bw)`.

## CoreMark Test Suite

`include/aiasm/coremark.h:14` + `src/coremark.cpp:11` — synthetic CoreMark (list / matrix / state / CRC) mapped to guest ISA. Runs `N` iterations on any target, reports `CoreMark/MHz` (cycles-accurate) and `iter/sec` (host wall time):

```cpp
#include "aiasm/coremark.h"
Emulator emu; emu.set_target(make_cpu_profile());
auto r = coremark::run(emu, 100, true, mat_a, mat_b);
std::printf("%s", coremark::report(r).c_str());
```

`main.cpp:278` runs the suite on CPU/GPU/ASIC and via the C++ frontend (`examples/coremark.cpp`).

## Test Framework

This project does not use an external test framework. The `main.cpp` driver
serves as the integration test: it exercises the loop, DMA, vector, and
matrix-tile instruction paths and verifies results via assertions in stdout.

The `tests/test_c_abi.c` file verifies the C-ABI (MOVI + HALT, profiler
counters, reset). Compile and run it with the command above.

## C++ Standard

C++20 required. Use a conforming compiler (GCC 11+, Clang 14+, MSVC 19.30+).
