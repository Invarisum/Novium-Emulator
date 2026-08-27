/*
 * test_c_abi.c — Minimal C test for the aism_emulator shared library.
 *
 * Compile (C linker + C++ runtime):
 *   gcc -o test_c_abi.exe tests/test_c_abi.c build/libaism_core.a -Iinclude -std=c11 -lstdc++
 */

#include "aiasm/aism.h"
#include <stdio.h>

int main(void) {
    AismEmulator* emu = aism_create(0, 0);
    if (!emu) { fprintf(stderr, "create failed\n"); return 1; }

    /* MOVI r1, #10; HALT */
    uint32_t program[] = {
        0x2C20000A,  /* MOVI r1, #10 */
        0x04000000   /* HALT         */
    };
    int rc = aism_load_program(emu, program, 2);
    if (rc != 0) { fprintf(stderr, "load failed\n"); aism_destroy(emu); return 1; }

    aism_run(emu);

    uint32_t val = 0;
    aism_read_gpr(emu, 1, &val);
    printf("r1 = %u  (expected 10)  %s\n", val, val == 10 ? "PASS" : "FAIL");

    uint64_t insns = aism_instruction_count(emu);
    uint64_t cycles = aism_cycle_count(emu);
    printf("Instructions: %llu  Cycles: %llu\n", (unsigned long long)insns, (unsigned long long)cycles);

    aism_reset(emu);
    uint32_t val2 = 0;
    aism_read_gpr(emu, 1, &val2);
    printf("After reset r1 = %u  %s\n", val2, val2 == 0 ? "PASS" : "FAIL");

    aism_destroy(emu);
    printf("All C-ABI tests passed.\n");
    return 0;
}
