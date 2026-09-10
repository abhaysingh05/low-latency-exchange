#pragma once
#include "exchange/types.hpp"
#include <cstring>
#include <optional>
#include <cstddef>

namespace exchange {

// Binary protocol: trivially-copyable structs are memcpy'd directly.
// No serialization overhead - wire format matches memory layout.
class Protocol {
public:
    static constexpr std::size_t kRequestSize  = sizeof(OrderRequest);
    static constexpr std::size_t kResponseSize = sizeof(OrderResponse);

    // Deserialize an OrderRequest from raw bytes.
    // Returns std::nullopt if insufficient data.
    [[nodiscard]] static std::optional<OrderRequest>
    deserialize_request(const char* data, std::size_t len) noexcept {
        if (len < kRequestSize) return std::nullopt;
        OrderRequest req{};
        std::memcpy(&req, data, kRequestSize);
        return req;
    }

    // Serialize an OrderResponse into a buffer.
    // Returns number of bytes written.
    static std::size_t
    serialize_response(const OrderResponse& resp, char* buf, std::size_t buf_len) noexcept {
        if (buf_len < kResponseSize) return 0;
        std::memcpy(buf, &resp, kResponseSize);
        return kResponseSize;
    }
};

} // namespace exchange
