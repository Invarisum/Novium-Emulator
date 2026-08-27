#pragma once

#include "config.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace aiasm {

// ============================================================================
// Peripheral: Timer (MMIO-mapped)
//
// Registers (offsets relative to MMIO_BASE + TIMER_BASE):
//   +0x00  COUNT   (R)  Current cycle count
//   +0x04  LIMIT   (RW)  Reload value
//   +0x08  CTRL    (RW)  1 = running, 0 = stopped
//   +0x0C  IRQ     (R)   1 = interrupt pending (auto-clears on read)
// ============================================================================

class TimerDevice {
public:
    TimerDevice() = default;

    // Called every CYCLES_PER_TICK CPU cycles
    void tick(uint64_t cycles) {
        if (ctrl_) {
            count_ += cycles;
            if (count_ >= limit_) {
                count_ -= limit_;
                irq_pending_ = true;
            }
        }
    }

    // MMIO read
    uint32_t read_reg(uint32_t offset) {
        switch (offset) {
            case TIMER_COUNT:  return static_cast<uint32_t>(count_ & 0xFFFFFFFF);
            case TIMER_LIMIT:  return static_cast<uint32_t>(limit_ & 0xFFFFFFFF);
            case TIMER_CTRL:   return ctrl_ ? 1u : 0u;
            case TIMER_IRQ:    { uint32_t v = irq_pending_ ? 1u : 0u; irq_pending_ = false; return v; }
            default:           return 0;
        }
    }

    // MMIO write
    void write_reg(uint32_t offset, uint32_t value) {
        switch (offset) {
            case TIMER_COUNT:  count_ = value; break;
            case TIMER_LIMIT:  limit_ = value; break;
            case TIMER_CTRL:   ctrl_ = (value != 0); break;
            case TIMER_IRQ:    irq_pending_ = false; break;
        }
    }

    bool interrupt_pending() const noexcept { return irq_pending_; }
    void clear_interrupt() noexcept { irq_pending_ = false; }

    // Serialization
    struct State {
        uint64_t count;
        uint64_t limit;
        bool ctrl;
        bool irq_pending;
    };

    State save() const { return {count_, limit_, ctrl_, irq_pending_}; }
    void restore(const State& s) { count_ = s.count; limit_ = s.limit; ctrl_ = s.ctrl; irq_pending_ = s.irq_pending; }

private:
    uint64_t count_       = 0;
    uint64_t limit_       = 10000;
    bool     ctrl_        = false;
    bool     irq_pending_ = false;
};

// ============================================================================
// Peripheral: UART (MMIO-mapped, line-buffered to stdout)
//
// Registers (offsets relative to MMIO_BASE + UART_BASE):
//   +0x00  TX    (W)  Write byte to transmit buffer (auto-flush on newline)
//   +0x04  RX    (R)  Read received byte
//   +0x08  STATUS (R) Bit0 = TX ready, Bit1 = RX ready
// ============================================================================

class UartDevice {
public:
    UartDevice() = default;

    void write_reg(uint32_t offset, uint32_t value) {
        switch (offset) {
            case UART_TX:
                tx_buffer_ += static_cast<char>(value & 0xFF);
                if ((value & 0xFF) == '\n' || tx_buffer_.size() >= 256) {
                    // Auto-flush on newline or buffer full
                    std::printf("[UART TX] %s\n", tx_buffer_.c_str());
                    tx_buffer_.clear();
                }
                break;
        }
    }

    uint32_t read_reg(uint32_t offset) {
        switch (offset) {
            case UART_RX:   return rx_buffer_.empty() ? 0 : static_cast<uint32_t>(rx_buffer_.front());
            case UART_STATUS: return 0x01; // TX always ready, RX never ready (no input)
            default: return 0;
        }
    }

    void tick(uint64_t) {}  // No per-tick work needed

    // Flush any buffered TX output (call on halt / destroy)
    void flush() {
        if (!tx_buffer_.empty()) {
            std::printf("[UART TX] %s\n", tx_buffer_.c_str());
            tx_buffer_.clear();
        }
    }

    bool interrupt_pending() const noexcept { return false; }
    void clear_interrupt() noexcept {}

    struct State {
        std::string tx_buffer;
        std::string rx_buffer;
    };

    State save() const { return {tx_buffer_, rx_buffer_}; }
    void restore(const State& s) { tx_buffer_ = s.tx_buffer; rx_buffer_ = s.rx_buffer; }

private:
    std::string tx_buffer_;
    std::string rx_buffer_;
};

} // namespace aiasm
