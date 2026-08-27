#include "aiasm/tlm.h"
#include "aiasm/memory.h"
#include "aiasm/bus.h"

namespace aiasm {

void TLMAdapter::bind_bus(Bus& bus) {
    target.set_transport([&bus](TLMPayload& p){
        if (p.command==TLMCommand::Read) p.data[0]=0; // placeholder
        // Forward to bus: use address as-is
        if (p.command==TLMCommand::Write && p.length==4) {
            uint32_t v; std::memcpy(&v, p.data, 4); bus.write32(uint32_t(p.address), v);
        } else if (p.command==TLMCommand::Read && p.length==4) {
            uint32_t v = bus.read32(uint32_t(p.address)); std::memcpy(p.data, &v, 4);
        }
        p.response = TLMResponse::OK;
        p.tlm_delay_cycles = 2;
    });
    initiator.bind(target);
}
void TLMAdapter::bind_memory(VirtualSRAM& ram) {
    target.set_transport([&ram](TLMPayload& p){
        if (p.address + p.length > ram.size()) { p.response=TLMResponse::AddressError; return; }
        if (p.command==TLMCommand::Write) std::memcpy(ram.ptr(uint32_t(p.address)), p.data, p.length);
        else if (p.command==TLMCommand::Read) std::memcpy(p.data, ram.ptr(uint32_t(p.address)), p.length);
        p.response=TLMResponse::OK;
        p.tlm_delay_cycles=1;
    });
    initiator.bind(target);
}

} // namespace aiasm
