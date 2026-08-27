#pragma once

#include "config.h"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Exception type
// ============================================================================

class EmulatorError : public std::runtime_error {
public:
    explicit EmulatorError(const std::string& msg) : std::runtime_error(msg) {}
};

// ============================================================================
// Virtual SRAM (scratchpad) — 64 MB contiguous address space
//
//   .text   : executable instructions
//   .data   : scalars / static data
//   .tensor : matrix buffers
//
// Zero-copy pointer slicing: ptr(addr) returns a raw uint8_t* into the
// backing store so tensor views can be cast directly.
// ============================================================================

class VirtualSRAM {
public:
    explicit VirtualSRAM(size_t total_bytes = SCRATCHPAD_SIZE)
        : data_(total_bytes) {}

    // --- Raw pointer access (zero-copy) ---
    uint8_t*       ptr(uint32_t addr)       { return data_.data() + addr; }
    const uint8_t* ptr(uint32_t addr) const { return data_.data() + addr; }

    // --- Scalar 32-bit access ---
    void    write32(uint32_t addr, uint32_t value) {
        check_bounds(addr, sizeof(uint32_t));
        std::memcpy(data_.data() + addr, &value, sizeof(uint32_t));
    }
    uint32_t read32(uint32_t addr) const {
        check_bounds(addr, sizeof(uint32_t));
        uint32_t value;
        std::memcpy(&value, data_.data() + addr, sizeof(uint32_t));
        return value;
    }

    // --- Block access ---
    void write_bytes(uint32_t addr, const uint8_t* src, size_t count) {
        check_bounds(addr, count);
        std::memcpy(data_.data() + addr, src, count);
    }
    void read_bytes(uint32_t addr, uint8_t* dest, size_t count) const {
        check_bounds(addr, count);
        std::memcpy(dest, data_.data() + addr, count);
    }

    // --- Typed access ---
    template <typename T>
    void write(uint32_t addr, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        write_bytes(addr, reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    template <typename T>
    T read(uint32_t addr) const {
        static_assert(std::is_trivially_copyable_v<T>);
        T value{};
        read_bytes(addr, reinterpret_cast<uint8_t*>(&value), sizeof(T));
        return value;
    }

    // --- Program / data loading ---
    void load(uint32_t addr, const uint8_t* src, size_t count) {
        check_bounds(addr, count);
        std::memcpy(data_.data() + addr, src, count);
    }

    void load_words(uint32_t addr, const uint32_t* instrs, size_t count) {
        load(addr, reinterpret_cast<const uint8_t*>(instrs), count * sizeof(uint32_t));
    }

    // --- Region helpers ---
    bool in_text_region(uint32_t addr) const    { return addr >= TEXT_BASE && addr < TEXT_BASE + TEXT_SIZE; }
    bool in_data_region(uint32_t addr) const    { return addr >= DATA_BASE && addr < DATA_BASE + DATA_SIZE; }
    bool in_tensor_region(uint32_t addr) const  { return addr >= TENSOR_BASE && addr < TENSOR_BASE + TENSOR_SIZE; }

    size_t size() const { return data_.size(); }

    // --- Buffer management ---
    void clear() { std::memset(data_.data(), 0, data_.size()); }

private:
    void check_bounds(uint32_t addr, size_t size) const {
        if (addr + size > data_.size()) {
            throw EmulatorError("VirtualSRAM access out of bounds: addr=0x"
                + std::to_string(addr) + ", size=" + std::to_string(size));
        }
    }

    std::vector<uint8_t> data_;
};

// ============================================================================
// Main RAM (separate backing store for DMA transfers)
// DMA_LOAD  copies main_ram → scratchpad
// DMA_STORE copies scratchpad → main_ram
// ============================================================================

class MainRAM {
public:
    explicit MainRAM(size_t total_bytes = MAIN_RAM_SIZE)
        : data_(total_bytes) {}

    uint8_t*       ptr(uint32_t addr)       { return data_.data() + addr; }
    const uint8_t* ptr(uint32_t addr) const { return data_.data() + addr; }

    void write(uint32_t addr, const uint8_t* src, size_t count) {
        check_bounds(addr, count);
        std::memcpy(data_.data() + addr, src, count);
    }
    void read(uint32_t addr, uint8_t* dest, size_t count) const {
        check_bounds(addr, count);
        std::memcpy(dest, data_.data() + addr, count);
    }

    template <typename T>
    void write(uint32_t addr, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        write(addr, reinterpret_cast<const uint8_t*>(&value), sizeof(T));
    }

    size_t size() const { return data_.size(); }
    void clear() { std::memset(data_.data(), 0, data_.size()); }

private:
    void check_bounds(uint32_t addr, size_t count) const {
        if (addr + count > data_.size())
            throw EmulatorError("MainRAM access out of bounds");
    }
    std::vector<uint8_t> data_;
};

} // namespace aiasm
