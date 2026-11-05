// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/osd_service_requester.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
namespace opensim::network {
class WebHttpClients;
// Actual URI/HTTP/StreamReader/timer binding. Proxy, TLS trust and native error
// policy remain explicit host inputs; wrappers retain their shared handler.
class OsdServiceNativeHttp {
    struct State;
    std::shared_ptr<State> state_;

  public:
    explicit OsdServiceNativeHttp(framework::ConfigXmlNativeHttpServices);
    OsdServiceNativeHttp(std::shared_ptr<WebHttpClients>,
                         std::function<std::exception_ptr(std::string, int)>);
    void bind(OsdServiceServices&) const;
};
}
