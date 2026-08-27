#pragma once

#include "config.h"
#include "memory.h"
#include "csr.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace aiasm {

// ============================================================================
// MMU & Virtual Memory Emulator
//
// Implements Sv39-style guest page tables (3-level, 4 KiB pages), TLB with
// LRU, and permission enforcement (R/W/X, U/S, A/D bits). Mirrors OS-visible
// translation that C++/SV/Chisel kernels would see on CPU/GPU/ASIC.
// ============================================================================

inline constexpr uint32_t PAGE_SHIFT = 12;
inline constexpr uint32_t PAGE_SIZE  = 1u << PAGE_SHIFT;
inline constexpr uint64_t VPN_MASK   = 0x1FF; // 9 bits per level (Sv39)

enum class PageFaultCause : uint8_t {
    None = 0, LoadAccessFault, StoreAccessFault, FetchAccessFault,
    LoadPageFault, StorePageFault, FetchPageFault
};

struct PageTableEntry {
    uint64_t raw = 0;
    bool valid()  const noexcept { return (raw & 1ULL) != 0; }
    bool read()   const noexcept { return (raw & (1ULL<<1)) != 0; }
    bool write()  const noexcept { return (raw & (1ULL<<2)) != 0; }
    bool execute()const noexcept { return (raw & (1ULL<<3)) != 0; }
    bool user()   const noexcept { return (raw & (1ULL<<4)) != 0; }
    bool accessed()const noexcept{ return (raw & (1ULL<<6)) != 0; }
    bool dirty()  const noexcept { return (raw & (1ULL<<7)) != 0; }
    uint64_t ppn() const noexcept{ return (raw >> 10) & ((1ULL<<44)-1); }
};

struct TLBEntry {
    uint64_t vpn = 0;
    uint64_t ppn = 0;
    uint8_t  perms = 0; // bit0=R,1=W,2=X,3=U
    bool     valid = false;
    uint64_t asid = 0;
};

class MMU {
public:
    static constexpr size_t TLB_SIZE = 32;

    explicit MMU(VirtualSRAM* ram = nullptr) : ram_(ram) {}

    void attach_ram(VirtualSRAM* ram) noexcept { ram_ = ram; }
    void set_csr(CSRFile* csr) noexcept { csr_ = csr; }
    void set_enabled(bool en) noexcept { enabled_ = en; }
    bool enabled() const noexcept { return enabled_; }

    // Translate VA → PA. Returns nullopt on fault (also fills fault_*).
    std::optional<uint32_t> translate(uint32_t va, bool is_write, bool is_fetch,
                                      PageFaultCause* cause = nullptr);

    // Direct TLB control
    void tlb_flush() noexcept;
    void tlb_flush_va(uint32_t va) noexcept;
    size_t tlb_hits() const noexcept { return hits_; }
    size_t tlb_misses() const noexcept { return misses_; }
    double tlb_hit_rate() const noexcept {
        size_t tot = hits_ + misses_;
        return tot ? double(hits_) / double(tot) : 0.0;
    }

    // Page-table setup helpers (identity map for tests)
    void identity_map(uint32_t va_base, uint32_t pa_base, size_t pages, uint8_t perms);

    PageFaultCause last_fault() const noexcept { return last_fault_; }
    uint32_t last_fault_va() const noexcept { return last_fault_va_; }

private:
    std::optional<uint32_t> walk(uint32_t va, bool is_write, bool is_fetch, PageFaultCause* cause);
    std::optional<uint32_t> tlb_lookup(uint32_t va, uint8_t need_perms);

    VirtualSRAM* ram_ = nullptr;
    CSRFile* csr_ = nullptr;
    bool enabled_ = false;
    std::array<TLBEntry, TLB_SIZE> tlb_{};
    size_t tlb_next_ = 0;
    size_t hits_ = 0, misses_ = 0;
    PageFaultCause last_fault_ = PageFaultCause::None;
    uint32_t last_fault_va_ = 0;

    // Simple linear page table for emulation (not hardware-accurate Sv39 walk)
    // Maps VPN → PPN + perms, used when no CSR satp is programmed
    struct SimpleMap { uint32_t vpn; uint32_t ppn; uint8_t perms; bool valid; };
    std::vector<SimpleMap> simple_map_;
};

} // namespace aiasm
