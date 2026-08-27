// accel.sv — SystemVerilog NPU tile for Novium SV frontend
// Frontend::load("examples/accel.sv") extracts parameters and lowers to guest ISA.

module npu_tile #(
    parameter TILE  = 4,
    parameter LANES = 16,
    parameter BW    = 64
)(
    input  logic        clk,
    input  logic        rst_n,
    input  logic [31:0] a_addr,
    input  logic [31:0] b_addr,
    output logic        done
);
    // novium: dma_load r5, r7, 64
    // novium: load_vr v0, r5, 0
    // novium: load_vr v1, r6, 0
    // novium: vec_add v2, v0, v1
    // novium: matmul_tile v3, v0, v1, N=4
    // novium: store_vr v3, r5, 0
    // novium: dma_store r9, r5, 64
    // novium: halt

    logic [15:0] tile_reg;
    assign tile_reg = TILE;
    assign done = 1'b0;
endmodule
