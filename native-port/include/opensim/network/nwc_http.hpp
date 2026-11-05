// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/nwc_xml_rpc.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
namespace opensim::network {
class WebHttpClients;
// Source WebUtil policy providers (proxy/TLS/platform failures) are explicit.
// The transport sends real HTTP; it does not supply synthetic profile replies.
class NwcXmlRpcHttpTransport {
    struct State;
    std::shared_ptr<State> state_;

  public:
    explicit NwcXmlRpcHttpTransport(framework::ConfigXmlNativeHttpServices);
    explicit NwcXmlRpcHttpTransport(std::shared_ptr<WebHttpClients>);
    std::shared_ptr<NwcXmlRpcResponse> send(const std::shared_ptr<NwcXmlRpcRequest>&,
                                            const std::optional<std::u16string>& url,
                                            std::int32_t timeout_milliseconds = 30000);
};
}
