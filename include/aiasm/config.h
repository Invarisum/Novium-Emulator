#pragma once

#include <cstdint>
#include <cstddef>

namespace aiasm {

// ============================================================================
// Memory configuration
// ============================================================================

inline constexpr size_t SCRATCHPAD_SIZE   = 64 * 1024 * 1024;        // 64 MB virtual SRAM
inline constexpr size_t MAIN_RAM_SIZE     = 64 * 1024 * 1024;       // 64 MB main RAM

// -- Scratchpad memory regions --
inline constexpr uint32_t TEXT_BASE       = 0x00000000;
inline constexpr size_t   TEXT_SIZE       =  8 * 1024 * 1024;       //  8 MB
inline constexpr uint32_t DATA_BASE       = TEXT_BASE + TEXT_SIZE;  // 0x00800000
inline constexpr size_t   DATA_SIZE       =  8 * 1024 * 1024;       //  8 MB
inline constexpr uint32_t TENSOR_BASE     = DATA_BASE + DATA_SIZE;  // 0x01000000
inline constexpr size_t   TENSOR_SIZE     = SCRATCHPAD_SIZE - TEXT_SIZE - DATA_SIZE; // 48 MB

// -- MMIO region (top of scratchpad address space) --
inline constexpr uint32_t MMIO_BASE       = 0x03F00000;             // 16 MB before end of scratchpad
inline constexpr size_t   MMIO_SIZE       = 0x00100000;             // 1 MB MMIO window
inline constexpr uint32_t MMIO_END        = MMIO_BASE + MMIO_SIZE;  // 0x04000000

// -- Peripheral MMIO register offsets (relative to MMIO_BASE) --
inline constexpr uint32_t TIMER_BASE      = 0x00000000;
inline constexpr uint32_t TIMER_COUNT     = TIMER_BASE + 0x00;   // R: current count
inline constexpr uint32_t TIMER_LIMIT     = TIMER_BASE + 0x04;   // RW: reload value
inline constexpr uint32_t TIMER_CTRL      = TIMER_BASE + 0x08;   // RW: control (1=en, 0=dis)
inline constexpr uint32_t TIMER_IRQ       = TIMER_BASE + 0x0C;   // R: interrupt status (1=fired)

inline constexpr uint32_t UART_BASE       = 0x00001000;
inline constexpr uint32_t UART_TX         = 0x00;    // W: transmit byte (offset from UART_BASE)
inline constexpr uint32_t UART_RX         = 0x04;    // R: receive byte
inline constexpr uint32_t UART_STATUS     = 0x08;    // R: status flags (bit0=tx_ready, bit1=rx_ready)

// -- Vector / tensor configuration --
inline constexpr size_t   TILE_SIZE       = 4;                       // 4x4 matrix tiles
inline constexpr size_t   VEC_WIDTH       = TILE_SIZE * TILE_SIZE;   // 16 floats per VR
inline constexpr size_t   VR_BYTE_SIZE    = VEC_WIDTH * sizeof(float); // 64 bytes

// -- DMA configuration --
inline constexpr size_t   DMA_LINE_SIZE   = 64;                      // cache-line granularity

// -- Register file --
inline constexpr size_t   NUM_GPR         = 16;                      // r0-r15
inline constexpr size_t   NUM_VR          = 8;                       // v0-v7
inline constexpr size_t   NUM_OPCODES     = 64;                      // 6-bit opcode space

// -- Instruction encoding --
inline constexpr size_t   INSTR_WIDTH     = 32;                      // bits
inline constexpr size_t   OPCODE_BITS     = 6;
inline constexpr size_t   OPCODE_SHIFT    = 26;
inline constexpr uint32_t OPCODE_MASK     = 0x3F;
inline constexpr uint32_t RD_SHIFT        = 21;
inline constexpr uint32_t RD_MASK         = 0x1F;
inline constexpr uint32_t RA_SHIFT        = 16;
inline constexpr uint32_t RA_MASK         = 0x1F;
inline constexpr uint32_t RB_SHIFT        = 11;
inline constexpr uint32_t RB_MASK         = 0x1F;
inline constexpr uint32_t FUNC_MASK       = 0x7FF;                   // 11 bits
inline constexpr uint32_t IMM_MASK        = 0xFFFF;                  // 16 bits
inline constexpr uint32_t TARGET_MASK     = 0x3FFFFFF;               // 26 bits

// -- System tick resolution --
inline constexpr uint64_t CYCLES_PER_TICK = 1000;                    // 1000 CPU cycles per peripheral tick

} // namespace aiasm
