// coremark.cpp — C++ CoreMark kernel for Novium
// Input to the C++ frontend: Frontend::load("examples/coremark.cpp")
// The pragma below tells the frontend to lower this kernel to guest VEC/MATMUL.

#include <cstdint>

// [[novium::kernel]] marks a function to be lowered to Novium ISA
[[novium::kernel]]
void coremark_kernel(float* dst, const float* a, const float* b) {
    // novium: load_vr v0, r5, 0
    // novium: load_vr v1, r6, 0
    // novium: vec_add v2, v0, v1
    // novium: matmul_tile v3, v0, v1, N=4
    // novium: store_vr v3, r5, 0
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a[i*4+k] * b[k*4+j];
            dst[i*4+j] = s;
        }
}

// novium: dma_load r5, r7, 64
// novium: dma_store r9, r5, 64
// novium: halt
