// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/login_application.hpp"
#include <iostream>
using namespace opensim;
struct Service : services::ILoginService {
    std::string peer;
    network::Value login(const services::LoginRequest& value) override {
        peer = value.remote_ip;
        return services::login_failure("GridProblem");
    }
    network::Value set_level(std::string_view, std::string_view, std::string_view, int) override {
        return {};
    }
};
int main() {
    try {
        Service service;
        server::IniConfig config;
        server::LoginApplication app(service, config);
        network::HttpRequest request{
            "POST", "/",
            network::xml_rpc_request(
                {"login_to_simulator",
                 {network::Value::object({{"first", "A"}, {"last", "B"}, {"passwd", "secret"}})}}),
            "192.0.2.9", "text/xml"};
        std::string line;
        while (std::getline(std::cin, line)) {
            std::string value;
            for (std::size_t i = 0; i < line.size(); i += 2)
                value.push_back(static_cast<char>(std::stoi(line.substr(i, 2), nullptr, 16)));
            request.headers["x-forwarded-for"] = value;
            service.peer.clear();
            app.handle(request);
            std::cout << service.peer << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
