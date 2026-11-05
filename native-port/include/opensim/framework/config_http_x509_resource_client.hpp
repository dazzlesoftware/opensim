#pragma once
#include "opensim/framework/config_xml_transport_http.hpp"
namespace opensim::framework {
// One X509ResourceClient instance retains its handler, cookies and default proxy.
class ConfigHttpX509ResourceClient {
    struct State;
    std::shared_ptr<State> state_;

  public:
    explicit ConfigHttpX509ResourceClient(ConfigXmlNativeHttpServices,
                                          std::int64_t maximum_buffer = 104857600);
    // Source TimeSpan ticks (100ns), including sub-millisecond positive values.
    std::optional<std::vector<std::uint8_t>> download_asset(std::optional<std::u16string_view>,
                                                            std::int64_t timeout_ticks) const;
};
}
