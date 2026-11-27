// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/rest_forms.hpp"
#include "opensim/network/web_http_clients.hpp"
#include <iostream>

int main(int argc, char **argv) {
    try {
        std::optional<std::pair<std::string, std::string>> auth;
        bool raw = false, unshared = false;
        for (int i = 3; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--raw")
                raw = true;
            else if (std::string_view(argv[i]) == "--unshared")
                unshared = true;
            else
                auth = {{"service-user", "service-password"}};
        }
        opensim::framework::ConfigXmlNativeHttpServices policy;
        policy.error = [](std::string, int) {
            return std::make_exception_ptr(std::runtime_error("Service HTTP request failed"));
        };
        auto clients = std::make_shared<opensim::network::WebHttpClients>(
            opensim::network::native_web_http_handler_factory(std::move(policy)));
        clients->setup(false, false, nullptr, 32);
        auto request = unshared ? opensim::network::rest_forms_transport(auth, -1, raw)
                                : opensim::network::rest_forms_transport(clients, auth, -1, raw);
        auto result = request(argv[1], argc > 2 ? argv[2] : "METHOD=probe");
        std::string hex;
        for (unsigned char c : result) {
            hex += "0123456789abcdef"[c >> 4];
            hex += "0123456789abcdef"[c & 15];
        }
        std::cout << "OK|" << hex << std::endl;
    } catch (...) {
        std::cout << "EX" << std::endl;
    }
    return 0;
}
