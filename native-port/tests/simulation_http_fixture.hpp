#pragma once
#include "opensim/network/osd_service_http.hpp"
#include "opensim/network/web_http_clients.hpp"

inline std::shared_ptr<opensim::network::OsdServiceRequester> simulation_http_requester() {
    using namespace opensim;
    framework::ConfigXmlNativeHttpServices http;
    http.error = [](std::string, int) {
        return std::make_exception_ptr(network::OsdServiceError(
            "System.Net.Http.HttpRequestException", u"An error occurred while sending the request."));
    };
    auto clients = std::make_shared<network::WebHttpClients>(network::native_web_http_handler_factory(http));
    clients->setup(false, false, nullptr, 32);
    network::OsdServiceNativeHttp transport(clients, http.error);
    auto number = std::make_shared<std::int32_t>(0);
    network::OsdServiceServices web;
    web.request_number = [number] {
        return *number;
    };
    web.set_request_number = [number](auto value) {
        *number = value;
    };
    web.debug_level = [] {
        return 0;
    };
    web.debug = web.info = [](const auto&) {};
    transport.bind(web);
    return std::make_shared<network::OsdServiceRequester>(std::move(web));
}
