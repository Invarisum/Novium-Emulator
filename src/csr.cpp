#include "aiasm/csr.h"
#include <cstdio>
#include <string>

namespace aiasm {

std::string CSRFile::dump() const {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
        "priv=%u mstatus=0x%llx mtvec=0x%llx mepc=0x%llx mcause=0x%llx satp=0x%llx",
        unsigned(priv), (unsigned long long)read(CSR_MSTATUS), (unsigned long long)read(CSR_MTVEC),
        (unsigned long long)read(CSR_MEPC), (unsigned long long)read(CSR_MCAUSE), (unsigned long long)read(CSR_SATP));
    return buf;
}

} // namespace aiasm
