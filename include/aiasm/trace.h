#pragma once

#include "isa.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// Trace Generation — VCD / FSDB / GDB
//
// VCDWriter emits IEEE 1364 Value Change Dump for GTKWave/Verdi.
// GDBStub exposes a minimal RSP loop for `target remote :1234`.
// InstructionTrace is a structured per-commit log for offline analysis.
// ============================================================================

struct TraceEntry {
    uint64_t cycle = 0;
    uint32_t pc = 0;
    Instruction instr{};
    uint32_t result = 0;
    bool is_branch_taken = false;
    uint32_t mem_addr = 0;
};

class InstructionTrace {
public:
    void push(const TraceEntry& e) { entries_.push_back(e); if (entries_.size() > max_) entries_.erase(entries_.begin()); }
    void set_max(size_t n) noexcept { max_ = n; }
    size_t size() const noexcept { return entries_.size(); }
    const std::vector<TraceEntry>& entries() const noexcept { return entries_; }
    void clear() { entries_.clear(); }
    // Export to CSV
    bool export_csv(const std::string& path) const;

private:
    std::vector<TraceEntry> entries_;
    size_t max_ = 8192;
};

class VCDWriter {
public:
    explicit VCDWriter(const std::string& path = "") { if (!path.empty()) open(path); }
    ~VCDWriter() { close(); }

    bool open(const std::string& path);
    void close();

    bool is_open() const noexcept { return ofs_.is_open(); }

    // Header
    void header(const std::string& timescale = "1ns");

    // Declare a signal (call before dump)
    void add_signal(const std::string& name, uint32_t width);

    // Dump a cycle
    void dump(uint64_t cycle, uint32_t pc, const Instruction& instr, uint32_t gpr0);

    void flush() { if (ofs_.is_open()) ofs_.flush(); }

private:
    std::ofstream ofs_;
    std::vector<std::string> signals_;
    bool header_written_ = false;
};

// Minimal GDB RSP stub (blocking, single-core)
class GDBStub {
public:
    bool start(uint16_t port = 1234);
    void stop();
    bool is_running() const noexcept { return running_; }
    // Poll — call from Emulator::step() when debugger single-step is enabled
    void poll(uint32_t pc);

private:
    bool running_ = false;
    uint16_t port_ = 0;
};

} // namespace aiasm
