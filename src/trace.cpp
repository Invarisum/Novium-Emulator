#include "aiasm/trace.h"
#include <bitset>
#include <cstdio>

namespace aiasm {

bool InstructionTrace::export_csv(const std::string& path) const {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "cycle,pc,raw,result,mem_addr\n");
    for (auto& e: entries_) std::fprintf(f, "%llu,0x%08X,0x%08X,%u,0x%08X\n",
        (unsigned long long)e.cycle, e.pc, e.instr.raw, e.result, e.mem_addr);
    std::fclose(f);
    return true;
}
bool VCDWriter::open(const std::string& path) {
    ofs_.open(path);
    return ofs_.is_open();
}
void VCDWriter::close() { if (ofs_.is_open()) ofs_.close(); }
void VCDWriter::header(const std::string& timescale) {
    if (!ofs_.is_open()) return;
    ofs_ << "$timescale " << timescale << " $end\n$scope module novium $end\n";
    for (size_t i=0;i<signals_.size();++i) ofs_ << "$var wire 32 s" << i << " " << signals_[i] << " $end\n";
    ofs_ << "$upscope $end\n$enddefinitions $end\n";
    header_written_=true;
}
void VCDWriter::add_signal(const std::string& name, uint32_t width) {
    (void)width; signals_.push_back(name);
}
void VCDWriter::dump(uint64_t cycle, uint32_t pc, const Instruction& instr, uint32_t gpr0) {
    if (!ofs_.is_open()) return;
    if (!header_written_) header("1ns");
    ofs_ << "#" << cycle << "\n";
    ofs_ << "b" << std::bitset<32>(pc).to_string() << " s0\n";
    ofs_ << "b" << std::bitset<32>(instr.raw).to_string() << " s1\n";
    ofs_ << "b" << std::bitset<32>(gpr0).to_string() << " s2\n";
}
bool GDBStub::start(uint16_t port) { port_=port; running_=true; return true; }
void GDBStub::stop() { running_=false; }
void GDBStub::poll(uint32_t pc) { (void)pc; /* stub: would handle RSP packets */ }

} // namespace aiasm
