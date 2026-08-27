#include "aiasm/mmu.h"

namespace aiasm {

std::optional<uint32_t> MMU::translate(uint32_t va, bool is_write, bool is_fetch, PageFaultCause* cause) {
    uint8_t need = 0;
    if (is_fetch) need |= 0x4; else if (is_write) need |= 0x2; else need |= 0x1;
    if (auto hit = tlb_lookup(va, need)) return hit;
    if (!enabled_ || !csr_ || (csr_->satp() == 0)) {
        // Bare mode — identity map with simple_map or direct
        if (!simple_map_.empty()) {
            uint32_t vpn = va >> PAGE_SHIFT;
            for (auto& m : simple_map_) if (m.valid && m.vpn == vpn) {
                bool need_w = is_write, need_x = is_fetch;
                if (need_w && !(m.perms & 0x2)) { if (cause) *cause = PageFaultCause::StorePageFault; last_fault_va_ = va; last_fault_ = PageFaultCause::StorePageFault; return std::nullopt; }
                if (need_x && !(m.perms & 0x4)) { if (cause) *cause = PageFaultCause::FetchPageFault; last_fault_va_ = va; last_fault_ = PageFaultCause::FetchPageFault; return std::nullopt; }
                // Fill TLB on simple_map hit
                TLBEntry e; e.vpn = vpn; e.ppn = m.ppn; e.perms = m.perms; e.valid = true; e.asid = 0;
                tlb_[tlb_next_ % TLB_SIZE] = e; tlb_next_++;
                return (m.ppn << PAGE_SHIFT) | (va & (PAGE_SIZE-1));
            }
        }
        return va; // identity
    }
    return walk(va, is_write, is_fetch, cause);
}

std::optional<uint32_t> MMU::tlb_lookup(uint32_t va, uint8_t need_perms) {
    uint64_t vpn = va >> PAGE_SHIFT;
    uint64_t asid = csr_ ? (csr_->satp() >> 44) & 0xFFFF : 0;
    for (auto& e : tlb_) if (e.valid && e.vpn == vpn && e.asid == asid) {
        if ((e.perms & need_perms) == need_perms) { ++hits_; return uint32_t((e.ppn << PAGE_SHIFT) | (va & (PAGE_SIZE-1))); }
        // permission fault
        return std::nullopt;
    }
    ++misses_;
    return std::nullopt;
}

std::optional<uint32_t> MMU::walk(uint32_t va, bool is_write, bool is_fetch, PageFaultCause* cause) {
    // Sv39 walk — but for emulation we synthesize from simple_map if satp is set
    // Otherwise walk the actual page table in ram_ (Sv39 3 levels)
    if (!ram_ || !csr_) {
        if (cause) *cause = PageFaultCause::LoadAccessFault;
        return std::nullopt;
    }
    uint64_t satp = csr_->satp();
    uint64_t ppn = satp & ((1ULL<<44)-1);
    uint64_t vpn[3] = { (va >> 12) & 0x1FF, (va >> 21) & 0x1FF, (va >> 30) & 0x1FF };
    uint64_t cur_ppn = ppn;
    PageTableEntry pte;
    for (int level = 2; level >= 0; --level) {
        uint64_t pte_addr = (cur_ppn * PAGE_SIZE) + vpn[level] * 8;
        if (pte_addr + 8 > ram_->size()) { if (cause) *cause = PageFaultCause::LoadAccessFault; last_fault_=PageFaultCause::LoadAccessFault; last_fault_va_=va; return std::nullopt; }
        uint64_t raw; std::memcpy(&raw, ram_->ptr(uint32_t(pte_addr)), 8);
        pte.raw = raw;
        if (!pte.valid()) { if (cause) *cause = is_fetch?PageFaultCause::FetchPageFault : is_write?PageFaultCause::StorePageFault : PageFaultCause::LoadPageFault; last_fault_ = *cause; last_fault_va_ = va; return std::nullopt; }
        bool is_leaf = pte.read() || pte.write() || pte.execute();
        if (is_leaf) {
            // permission check
            if (is_write && !pte.write()) { if (cause) *cause=PageFaultCause::StorePageFault; last_fault_=*cause; last_fault_va_=va; return std::nullopt; }
            if (is_fetch && !pte.execute()){ if (cause) *cause=PageFaultCause::FetchPageFault; last_fault_=*cause; last_fault_va_=va; return std::nullopt; }
            if (!is_write && !is_fetch && !pte.read()){ if (cause) *cause=PageFaultCause::LoadPageFault; last_fault_=*cause; last_fault_va_=va; return std::nullopt; }
            // Check privilege (U bit)
            PrivilegeLevel priv = csr_->priv;
            if (priv == PrivilegeLevel::User && !pte.user()) { if (cause) *cause=PageFaultCause::LoadPageFault; return std::nullopt; }
            uint64_t pa = (pte.ppn() << PAGE_SHIFT) | (va & (PAGE_SIZE-1));
            // Insert into TLB
            TLBEntry e; e.vpn = va >> PAGE_SHIFT; e.ppn = pte.ppn(); e.valid = true;
            e.perms = (pte.read()?1:0) | (pte.write()?2:0) | (pte.execute()?4:0) | (pte.user()?8:0);
            e.asid = (satp >> 44) & 0xFFFF;
            tlb_[tlb_next_ % TLB_SIZE] = e; tlb_next_++;
            return uint32_t(pa);
        } else {
            cur_ppn = pte.ppn();
        }
    }
    if (cause) *cause = PageFaultCause::LoadPageFault;
    last_fault_=*cause; last_fault_va_=va;
    return std::nullopt;
}

void MMU::tlb_flush() noexcept { for (auto& e: tlb_) e.valid=false; tlb_next_=0; }
void MMU::tlb_flush_va(uint32_t va) noexcept {
    uint64_t vpn = va >> PAGE_SHIFT;
    for (auto& e: tlb_) if (e.valid && e.vpn == vpn) e.valid=false;
}

void MMU::identity_map(uint32_t va_base, uint32_t pa_base, size_t pages, uint8_t perms) {
    for (size_t i=0;i<pages;++i) {
        simple_map_.push_back({(va_base >> PAGE_SHIFT)+uint32_t(i), (pa_base >> PAGE_SHIFT)+uint32_t(i), perms, true});
    }
}

} // namespace aiasm
