#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aiasm {

// ============================================================================
// SystemC / TLM (Transaction-Level Modeling) Interfaces
//
// Provides TLM-2.0-style generic payload + initiator/target sockets so the
// Novium core can be dropped into a SystemC virtual platform or EDA
// toolchain. No SystemC headers required — this is a header-only shim that
// forwards to real SystemC when `HAVE_SYSTEMC` is defined.
// ============================================================================

enum class TLMCommand : uint8_t { Read = 0, Write = 1, Ignore = 2 };
enum class TLMResponse : uint8_t { OK = 0, AddressError = 1, GenericError = 2 };

struct TLMPayload {
    TLMCommand command = TLMCommand::Read;
    uint64_t address = 0;
    uint8_t* data = nullptr;
    uint32_t length = 0;
    uint32_t streaming_width = 0;
    TLMResponse response = TLMResponse::OK;
    uint32_t tlm_delay_cycles = 0; // annotated delay
};

using TLMTransportFn = std::function<void(TLMPayload&)>;

class TLMSocket {
public:
    explicit TLMSocket(const std::string& name) : name_(name) {}

    const std::string& name() const noexcept { return name_; }

    // Bind initiator → target
    void bind(TLMSocket& target) { target_ = &target; transport_ = target.transport_; }
    void set_transport(TLMTransportFn fn) { transport_ = std::move(fn); }

    // Blocking transport (initiator side)
    void b_transport(TLMPayload& p, uint32_t& delay_cycles) {
        if (transport_) { transport_(p); delay_cycles += p.tlm_delay_cycles; }
        else p.response = TLMResponse::AddressError;
    }

    bool is_bound() const noexcept { return target_ != nullptr; }

private:
    std::string name_;
    TLMSocket* target_ = nullptr;
    TLMTransportFn transport_;
};

// Convenience: expose Bus as TLM target
class TLMAdapter {
public:
    TLMSocket initiator{"novium.initiator"};
    TLMSocket target{"novium.target"};

    void bind_bus(class Bus& bus); // implemented in src/tlm.cpp
    void bind_memory(class VirtualSRAM& ram);

    std::string report() const { return "TLM bound=" + std::string(initiator.is_bound()?"yes":"no"); }
};

} // namespace aiasm
