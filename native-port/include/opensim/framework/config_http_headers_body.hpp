// Consumed XML download CopyToAsync entity stream, .NET8 source.
#pragma once
#include "opensim/framework/config_http_headers.hpp"
namespace opensim::framework {
using ConfigHttpRawRead = std::function<std::int32_t(std::span<std::uint8_t>)>;
class ConfigHttpXmlBodyReader {
    struct State;
    std::shared_ptr<State> state_;
    std::int32_t read_impl(std::span<std::uint8_t>);

  public:
    ConfigHttpXmlBodyReader(const ConfigHttpResponseHead&, ConfigHttpRawRead,
                            std::size_t maximum_header_bytes = 65536);
    // Native pull adapter for the source XML downloader's CopyToAsync flow. Does
    // not claim general HttpContentReadStream.Read/CopyTo cancellation/drain APIs.
    std::int32_t read(std::span<std::uint8_t>);
    // A pool may reuse the transport only after a framed entity ended without
    // failure. EOF-delimited responses always require closing the connection.
    bool completed_for_reuse() const noexcept;
    bool can_drain_for_reuse(std::size_t buffered_bytes, std::size_t maximum_bytes) const noexcept;
};
}
