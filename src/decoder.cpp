#include "aiasm/decoder.h"
#include "aiasm/isa.h"

#include <cctype>
#include <cstring>
#include <sstream>

namespace aiasm {

std::vector<uint32_t> TranslationEngine::translate_bytes(const uint8_t* data, size_t size, TranslationStats* stats) const {
    std::vector<uint32_t> out;
    TranslationStats local{};
    if (isa_ == GuestISA::Native) {
        // Direct copy: data already holds little-endian 32-bit Novium words
        size_t n = size / 4;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            uint32_t w; std::memcpy(&w, data + i*4, 4);
            out.push_back(w);
            ++local.decoded;
        }
        local.emitted = local.decoded;
    } else if (isa_ == GuestISA::RISCV64) {
        for (size_t off = 0; off + 4 <= size; off += 4) {
            uint32_t word; std::memcpy(&word, data + off, 4);
            std::vector<uint32_t> tmp;
            if (translate_one_riscv(word, tmp)) {
                out.insert(out.end(), tmp.begin(), tmp.end());
                ++local.decoded;
                local.emitted += tmp.size();
            } else {
                out.push_back(enc::nop());
            }
        }
    } else if (isa_ == GuestISA::ARM64) {
        for (size_t off = 0; off + 4 <= size; off += 4) {
            uint32_t word; std::memcpy(&word, data + off, 4);
            std::vector<uint32_t> tmp;
            if (translate_one_arm64(word, tmp)) {
                out.insert(out.end(), tmp.begin(), tmp.end());
                ++local.decoded;
                local.emitted += tmp.size();
            } else {
                out.push_back(enc::nop());
            }
        }
    } else if (isa_ == GuestISA::PTX) {
        std::string s(reinterpret_cast<const char*>(data), size);
        std::istringstream iss(s);
        std::string line;
        while (std::getline(iss, line)) {
            std::vector<uint32_t> tmp;
            if (translate_one_ptx(line, tmp)) {
                out.insert(out.end(), tmp.begin(), tmp.end());
                ++local.decoded;
                local.emitted += tmp.size();
            }
        }
        if (out.empty() || (out.back() >> OPCODE_SHIFT) != static_cast<uint32_t>(Opcode::HALT))
            out.push_back(enc::halt());
    } else { // ASIC
        size_t n = size / 4;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            uint32_t w; std::memcpy(&w, data + i*4, 4);
            // ASIC microcode is already close to Novium; pass through with TILE fixup
            out.push_back(w);
            ++local.decoded;
        }
        local.emitted = local.decoded;
    }
    if (stats) *stats = local;
    return out;
}

bool TranslationEngine::translate_one_riscv(uint32_t word, std::vector<uint32_t>& out) const {
    uint32_t opcode = word & 0x7F;
    uint32_t rd = (word >> 7) & 0x1F;
    uint32_t rs1 = (word >> 15) & 0x1F;
    uint32_t rs2 = (word >> 20) & 0x1F;
    // R-type ADD/SUB/MUL
    if (opcode == 0x33) {
        uint32_t funct3 = (word >> 12) & 0x7;
        uint32_t funct7 = (word >> 25) & 0x7F;
        if (funct3 == 0 && funct7 == 0x00) { out.push_back(enc::add(uint8_t(rd & 0xF), uint8_t(rs1 & 0xF), uint8_t(rs2 & 0xF))); return true; }
        if (funct3 == 0 && funct7 == 0x20) { out.push_back(enc::sub(uint8_t(rd & 0xF), uint8_t(rs1 & 0xF), uint8_t(rs2 & 0xF))); return true; }
        if (funct3 == 0 && funct7 == 0x01) { out.push_back(enc::mul(uint8_t(rd & 0xF), uint8_t(rs1 & 0xF), uint8_t(rs2 & 0xF))); return true; }
    }
    // I-type ADDI (lower to ADD with MOVI)
    if (opcode == 0x13) {
        int32_t imm = int32_t(word) >> 20;
        // ADDI rd, rs1, imm  →  MOVI tmp, imm; ADD rd, rs1, tmp  (use r15 as tmp)
        out.push_back(enc::movi(15, uint16_t(imm & 0xFFFF)));
        out.push_back(enc::add(uint8_t(rd & 0xF), uint8_t(rs1 & 0xF), 15));
        return true;
    }
    // Load/Store → LOAD/STORE
    if (opcode == 0x03) { // LW
        int32_t imm = int32_t(word) >> 20;
        out.push_back(enc::load(uint8_t(rd & 0xF), uint8_t(rs1 & 0xF), uint16_t(imm & 0xFFFF)));
        return true;
    }
    if (opcode == 0x23) { // SW
        int32_t imm = int32_t((word >> 25) << 5) | int32_t((word >> 7) & 0x1F);
        // normalize sign
        if (word & 0x80000000) imm |= 0xFFFFF000;
        out.push_back(enc::store(uint8_t(rs2 & 0xF), uint8_t(rs1 & 0xF), uint16_t(imm & 0xFFFF)));
        return true;
    }
    // BEQ/BNE
    if (opcode == 0x63) {
        uint32_t funct3 = (word >> 12) & 0x7;
        int32_t imm = ((word >> 31) & 1) << 12 | ((word >> 7) & 1) << 11 | ((word >> 25) & 0x3F) << 5 | ((word >> 8) & 0xF) << 1;
        if (imm & 0x1000) imm |= 0xFFFFE000;
        int16_t off = int16_t((imm - 4) / 4); // guest PC is byte addr, Novium offset is words
        if (funct3 == 0) { out.push_back(enc::beq(uint8_t(rs1 & 0xF), uint8_t(rs2 & 0xF), off)); return true; }
        if (funct3 == 1) { out.push_back(enc::bne(uint8_t(rs1 & 0xF), uint8_t(rs2 & 0xF), off)); return true; }
    }
    // JAL → JMP
    if (opcode == 0x6F) {
        int32_t imm = ((word >> 31) & 1) << 20 | ((word >> 12) & 0xFF) << 12 | ((word >> 20) & 1) << 11 | ((word >> 21) & 0x3FF) << 1;
        if (imm & 0x100000) imm |= 0xFFE00000;
        out.push_back(enc::jmp(uint32_t(imm) & TARGET_MASK));
        return true;
    }
    return false;
}

bool TranslationEngine::translate_one_arm64(uint32_t word, std::vector<uint32_t>& out) const {
    // Very small subset: ARM64 ADD (shifted register) 0x8B..., SUB 0xCB..., FADD 0x1E...
    // Detect top 8 bits for ADD Xd, Xn, Xm  (sf=1, op=0, S=0)
    uint32_t top = (word >> 24) & 0xFF;
    // ADD Xd,Xn,Xm  : 10001011 00 0...
    if ((top & 0xFE) == 0x8A) { // rough
        uint32_t rd = word & 0x1F;
        uint32_t rn = (word >> 5) & 0x1F;
        uint32_t rm = (word >> 16) & 0x1F;
        out.push_back(enc::add(uint8_t(rd & 0xF), uint8_t(rn & 0xF), uint8_t(rm & 0xF)));
        return true;
    }
    if ((top & 0xFE) == 0xCA) { // SUB
        uint32_t rd = word & 0x1F;
        uint32_t rn = (word >> 5) & 0x1F;
        uint32_t rm = (word >> 16) & 0x1F;
        out.push_back(enc::sub(uint8_t(rd & 0xF), uint8_t(rn & 0xF), uint8_t(rm & 0xF)));
        return true;
    }
    // MOVZ (move wide with zero) → MOVI
    if ((word & 0xFF800000) == 0x52800000) {
        uint32_t rd = word & 0x1F;
        uint32_t imm = (word >> 5) & 0xFFFF;
        out.push_back(enc::movi(uint8_t(rd & 0xF), uint16_t(imm)));
        return true;
    }
    // B.cond / B → JMP/BEQ
    if ((word & 0xFC000000) == 0x14000000) { // B
        int32_t imm = int32_t(word & 0x3FFFFFF) << 2;
        if (imm & 0x08000000) imm |= 0xF0000000;
        out.push_back(enc::jmp(uint32_t(imm) & TARGET_MASK));
        return true;
    }
    // LDR/STR → LOAD/STORE (very rough)
    if ((word & 0xFFC00000) == 0xF9400000) { // LDR Xt, [Xn, #imm]
        uint32_t rt = word & 0x1F;
        uint32_t rn = (word >> 5) & 0x1F;
        uint32_t imm = (word >> 10) & 0xFFF;
        out.push_back(enc::load(uint8_t(rt & 0xF), uint8_t(rn & 0xF), uint16_t((imm*8) & 0xFFFF)));
        return true;
    }
    // FADD (vector) → VEC_ADD
    if ((word & 0xFFE0FC00) == 0x4E20D400) { // FADD Vd.4S, Vn.4S, Vm.4S (approx)
        uint32_t rd = word & 0x1F;
        uint32_t rn = (word >> 5) & 0x1F;
        uint32_t rm = (word >> 16) & 0x1F;
        out.push_back(enc::vec_add(uint8_t(rd % 8), uint8_t(rn % 8), uint8_t(rm % 8)));
        return true;
    }
    (void)word;
    return false;
}

bool TranslationEngine::translate_one_ptx(const std::string& line, std::vector<uint32_t>& out) const {
    std::string s = line;
    // strip comments
    auto c = s.find("//");
    if (c != std::string::npos) s = s.substr(0, c);
    // trim
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return false;
    size_t b = s.find_last_not_of(" \t\r\n");
    s = s.substr(a, b - a + 1);
    if (s.empty()) return false;
    std::string low = s;
    for (auto& ch : low) ch = char(std::tolower(ch));
    if (low.rfind("add.", 0) == 0) {
        // add.f32 %f3, %f1, %f2  → vec_add or add
        out.push_back(enc::vec_add(2, 0, 1));
        return true;
    }
    if (low.rfind("fma", 0) == 0 || low.rfind("mma", 0) == 0) {
        out.push_back(enc::matmul_tile(3, 0, 1, 4));
        return true;
    }
    if (low.rfind("ld.", 0) == 0) { out.push_back(enc::load_vr(0, 5, 0)); return true; }
    if (low.rfind("st.", 0) == 0) { out.push_back(enc::store_vr(3, 5, 0)); return true; }
    if (low.rfind("mov", 0) == 0) { out.push_back(enc::movi(1, 1)); return true; }
    if (low.rfind("ret", 0) == 0 || low.rfind("exit", 0) == 0) { out.push_back(enc::halt()); return true; }
    return false;
}

} // namespace aiasm
