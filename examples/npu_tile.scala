// npu_tile.scala — Chisel NPU tile for Novium Chisel frontend
// Frontend::load("examples/npu_tile.scala") extracts Chisel Module params.

import chisel3._
import chisel3.util._

class NPUTile(val tileSize: Int = 4, val lanes: Int = 16) extends Module {
  val io = IO(new Bundle {
    val aAddr = Input(UInt(32.W))
    val bAddr = Input(UInt(32.W))
    val done  = Output(Bool())
  })

  // novium: dma_load r5, r7, 64
  // novium: load_vr v0, r5, 0
  // novium: load_vr v1, r6, 0
  // novium: vec_add v2, v0, v1
  // novium: matmul_tile v3, v0, v1, N=4
  // novium: store_vr v3, r5, 0
  // novium: dma_store r9, r5, 64
  // novium: halt

  io.done := false.B
}

object NPUTile extends App {
  emitVerilog(new NPUTile(4, 16))
}
