#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Cache Hierarchy & Memory Controller
//
// Configurable L1/L2/L3 with MESI/MOESI coherence and DDR5/HBM3 memory
// controller (bank contention, row hits, tCAS/tRCD). Each access is
// classified as hit/miss and adds stall cycles to the core.
// ============================================================================

enum class CoherenceProtocol : uint8_t { None = 0, MESI = 1, MOESI = 2 };
enum class MemoryType : uint8_t { DDR5 = 0, HBM3 = 1 };

struct CacheConfig {
    size_t size_bytes = 32 * 1024; // 32 KiB
    size_t line_bytes = 64;
    size_t associativity = 8;
    CoherenceProtocol coherence = CoherenceProtocol::MESI;
    uint32_t hit_latency = 1;
    uint32_t miss_penalty = 10;
};

struct CacheStats {
    uint64_t hits = 0, misses = 0, writebacks = 0;
    double hit_rate() const noexcept { uint64_t tot=hits+misses; return tot?double(hits)/double(tot):0; }
};

class Cache {
public:
    explicit Cache(CacheConfig cfg = {}) { configure(cfg); }

    void configure(CacheConfig cfg);
    const CacheConfig& config() const noexcept { return cfg_; }

    // Access: returns true if hit, false if miss (and fills on miss)
    bool access(uint32_t addr, bool is_write, uint32_t* stall_cycles = nullptr);

    void flush() noexcept;
    CacheStats stats() const noexcept { return stats_; }
    void reset_stats() noexcept { stats_ = {}; }

    std::string report(const char* name) const {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s %zuKiB/%zu-way %s hit=%llu miss=%llu rate=%.1f%%",
            name, cfg_.size_bytes/1024, cfg_.associativity,
            cfg_.coherence==CoherenceProtocol::MESI?"MESI":cfg_.coherence==CoherenceProtocol::MOESI?"MOESI":"none",
            (unsigned long long)stats_.hits, (unsigned long long)stats_.misses, stats_.hit_rate()*100);
        return buf;
    }

private:
    struct Line { bool valid=false; bool dirty=false; uint32_t tag=0; uint8_t mesi=0; uint64_t lru=0; };
    CacheConfig cfg_{};
    std::vector<std::vector<Line>> sets_;
    uint64_t tick_ = 0;
    CacheStats stats_{};
};

struct MemoryControllerConfig {
    MemoryType type = MemoryType::DDR5;
    uint32_t tCAS = 16, tRCD = 16, tRP = 16;
    uint32_t banks = 16, bank_groups = 4;
    uint32_t bandwidth_GBs = 64;
};

class MemoryController {
public:
    explicit MemoryController(MemoryControllerConfig cfg = {}) : cfg_(cfg) {}

    void configure(MemoryControllerConfig c) noexcept { cfg_ = c; }
    const MemoryControllerConfig& config() const noexcept { return cfg_; }

    // Row hit/miss model; returns stall cycles
    uint32_t access(uint32_t addr, bool is_write);

    uint64_t row_hits() const noexcept { return row_hits_; }
    uint64_t row_misses() const noexcept { return row_misses_; }
    uint64_t total_accesses() const noexcept { return row_hits_ + row_misses_; }
    double row_hit_rate() const noexcept { uint64_t tot=total_accesses(); return tot?double(row_hits_)/double(tot):0; }

    void reset() { row_hits_=0; row_misses_=0; open_rows_.fill(UINT32_MAX); }

    std::string report() const {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "MC %s banks=%u row_hit=%.1f%% tCAS=%u",
            cfg_.type==MemoryType::DDR5?"DDR5":"HBM3", cfg_.banks, row_hit_rate()*100, cfg_.tCAS);
        return buf;
    }

private:
    MemoryControllerConfig cfg_{};
    uint64_t row_hits_ = 0, row_misses_ = 0;
    std::array<uint32_t, 32> open_rows_{};
};

class CacheHierarchy {
public:
    Cache l1{CacheConfig{32*1024, 64, 8, CoherenceProtocol::MESI, 1, 10}};
    Cache l2{CacheConfig{256*1024, 64, 8, CoherenceProtocol::MESI, 4, 20}};
    Cache l3{CacheConfig{2*1024*1024, 64, 16, CoherenceProtocol::MOESI, 12, 40}};
    MemoryController mc;

    // Single access through hierarchy; returns total stall cycles
    uint32_t access(uint32_t addr, bool is_write);

    void reset() { l1.reset_stats(); l2.reset_stats(); l3.reset_stats(); mc.reset(); }
    std::string report() const;
};

} // namespace aiasm
