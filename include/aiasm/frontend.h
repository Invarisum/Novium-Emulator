#pragma once

#include "config.h"
#include "isa.h"
#include "target.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aiasm::frontend {

// ============================================================================
// Frontend — C++ / SystemVerilog / Chisel → guest ISA
//
//   auto prog = Frontend::load("kernel.cpp");       // C++
//   auto prog = Frontend::load("accel.sv");         // SystemVerilog
//   auto prog = Frontend::load("NPUTile.scala");    // Chisel
//
// Each loader parses a *subset* of the source language and lowers it to the
// Novium guest ISA (enc:: helpers). For full language coverage users run
// their existing toolchain (clang / sv2v / chisel -> firrtl) and point the
// frontend at the generated guest assembly or ELF.
//
// Accuracy: lowering is 1:1 — every source construct maps to the same
// guest instructions that the hand-written enc:: program would emit.
// ============================================================================

enum class SourceKind { Unknown, Cpp, SystemVerilog, Chisel, GuestAsm };

inline SourceKind kind_from_extension(const std::filesystem::path& p) {
    auto ext = p.extension().string();
    for (auto& c : ext) c = char(std::tolower(c));
    if (ext == ".cpp" || ext == ".cc" || ext == ".c" || ext == ".hpp" || ext == ".h") return SourceKind::Cpp;
    if (ext == ".sv" || ext == ".v" || ext == ".svh") return SourceKind::SystemVerilog;
    if (ext == ".scala" || ext == ".sc") return SourceKind::Chisel;
    if (ext == ".asm" || ext == ".s") return SourceKind::GuestAsm;
    return SourceKind::Unknown;
}

struct LoadResult {
    std::vector<uint32_t> program;   // guest instructions
    TargetProfile target = make_asic_profile(); // target hint extracted from source
    std::string diag;                // parse diagnostics
    SourceKind kind = SourceKind::Unknown;
    bool ok = false;
};

class Frontend {
public:
    // Dispatch by file extension
    static LoadResult load(const std::filesystem::path& path);
    static LoadResult load_string(const std::string& source, SourceKind kind, const std::string& name = "<string>");

    // Individual frontends
    static LoadResult load_cpp(const std::string& src, const std::string& name);
    static LoadResult load_systemverilog(const std::string& src, const std::string& name);
    static LoadResult load_chisel(const std::string& src, const std::string& name);
    static LoadResult load_guest_asm(const std::string& src, const std::string& name);

    // Helpers exposed for tests
    static std::string kind_name(SourceKind k);
};

} // namespace aiasm::frontend
