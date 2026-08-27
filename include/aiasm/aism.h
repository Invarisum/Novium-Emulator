/*
 * aism.h — Pure-C header for the Novium AIASM emulator shared library.
 *
 * No C++ dependencies.  Include this from C, Rust, Python (via cffi), etc.
 *
 * Usage:
 *   AismEmulator* emu = aism_create(0, 0);
 *   aism_load_program(emu, program, count);
 *   aism_run(emu);
 *   uint32_t val;
 *   aism_read_gpr(emu, 1, &val);
 *   aism_destroy(emu);
 */

#ifndef AISM_H
#define AISM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle — callers never see the struct layout. */
typedef struct AismEmulator AismEmulator;

/* --- Lifetime --- */
AismEmulator* aism_create(uint32_t scratchpad_bytes, uint32_t main_ram_bytes);
void          aism_destroy(AismEmulator* emu);
void          aism_reset(AismEmulator* emu);

/* --- Program / data loading --- */
int  aism_load_program(AismEmulator* emu, const uint32_t* instrs, uint32_t count);
int  aism_load_main_ram(AismEmulator* emu, uint32_t offset, const uint8_t* data, uint32_t count);
int  aism_load_tensor(AismEmulator* emu, uint32_t addr, const float* data, uint32_t count);
int  aism_write_scratchpad(AismEmulator* emu, uint32_t addr, const uint8_t* data, uint32_t count);
int  aism_write32(AismEmulator* emu, uint32_t addr, uint32_t value);
uint32_t aism_read32(AismEmulator* emu, uint32_t addr);

/* --- Execution --- */
void aism_run(AismEmulator* emu);
void aism_step(AismEmulator* emu);
void aism_set_verbose(AismEmulator* emu, int verbose);
int  aism_halted(AismEmulator* emu);

/* --- Debugger --- */
void aism_add_breakpoint(AismEmulator* emu, uint32_t addr);
void aism_remove_breakpoint(AismEmulator* emu, uint32_t addr);
void aism_clear_breakpoints(AismEmulator* emu);
int  aism_has_breakpoint(AismEmulator* emu, uint32_t addr);
void aism_enable_single_step(AismEmulator* emu);
void aism_disable_single_step(AismEmulator* emu);

/* --- Register access --- */
uint32_t aism_pc(AismEmulator* emu);
void     aism_read_gpr(AismEmulator* emu, uint8_t idx, uint32_t* out);
void     aism_read_vr(AismEmulator* emu, uint8_t idx, float* out, uint32_t count);

/* --- Profiler --- */
uint64_t aism_instruction_count(AismEmulator* emu);
uint64_t aism_cycle_count(AismEmulator* emu);
uint64_t aism_dma_bytes(AismEmulator* emu);
double   aism_dma_throughput(AismEmulator* emu);

/* --- Disassembly (into caller-supplied buffer) --- */
int aism_disassemble_at(AismEmulator* emu, uint32_t addr, char* buf, uint32_t bufsize);

/* --- Target / host translation --- */
void aism_set_target_cpu(AismEmulator* emu);
void aism_set_target_gpu(AismEmulator* emu);
void aism_set_target_asic(AismEmulator* emu);
void aism_set_acceleration(AismEmulator* emu, int enable);
const char* aism_host_info(void);
void aism_run_fast(AismEmulator* emu);
uint64_t aism_block_cache_hits(AismEmulator* emu);
uint64_t aism_block_cache_misses(AismEmulator* emu);
double   aism_block_cache_hit_rate(AismEmulator* emu);

#endif /* AISM_H */
