#include "simulation_http_fixture.hpp"
#include "opensim/framework/text.hpp"
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/simulation_connector.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/osd_request.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
std::string hex(std::string_view s) {
    std::string out;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
std::optional<std::string> opt(const network::Value& v) {
    return v.kind == network::Value::Kind::Undefined ? std::nullopt
                                                     : std::optional<std::string>(network::osd_string(v));
}
class Custom : public services::SimulationConnector {
  public:
    using SimulationConnector::SimulationConnector;

  protected:
    std::string agent_path() const override {
        return "custom/agent/";
    }
};
int main(int argc, char** argv) {
    if (argc > 3 && std::string(argv[1]) == "--transport") {
        std::cout << network::osd_json(simulation_http_requester()->send(framework::utf16_replacing(argv[2]),
                                                                         nullptr, u"DELETE",
                                                                         std::stoi(argv[3]), false, false),
                                       true);
        return 0;
    }
    if (argc > 1) {
        services::SimulationConnector connector(simulation_http_requester());
        services::GridRegion region;
        region.set_server_uri(argv[1]);
        region.id = framework::Uuid("00000000-0000-0000-0000-000000000008");
        const framework::Uuid agent("00000000-0000-0000-0000-000000000007");
        bool a = connector.release_agent(
            region.id, agent,
            std::string(argv[1]) +
                (argc > 2 && std::string(argv[2]) == "receiver"
                     ? "agent/" + agent.str() + "/" + region.id.str() + "/release/?auth=release%2Btoken"
                     : "release/?auth=release%2Btoken"));
        bool b = connector.close_agent(region, agent, std::string("close%2Btoken"));
        std::cout << a << "|" << b;
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int profile;
        std::string data;
        in >> profile >> data;
        auto args = network::parse_osd_json(decode(data));
        std::string trace;
        services::SimulationConnector::DeleteRequest request = [&](const std::optional<std::string>& uri,
                                                                   int timeout) {
            trace += (uri ? hex(*uri) : "-") + ":" + std::to_string(timeout) + ":DELETE:1:0:0;";
            if (profile & 1)
                throw std::runtime_error("request");
            return network::Value::object({{"success", network::Value(false)}});
        };
        std::unique_ptr<services::SimulationConnector> connector;
        if (profile & 4)
            connector = std::make_unique<Custom>(services::SimulationConnector::Request{}, request);
        else
            connector = std::make_unique<services::SimulationConnector>(
                services::SimulationConnector::Request{}, request);
        std::string result;
        try {
            services::GridRegion region;
            region.id = framework::Uuid("00000000-0000-0000-0000-000000000008");
            region.set_server_uri("http://127.0.0.1:9000/base/");
            const framework::Uuid agent("00000000-0000-0000-0000-000000000007");
            bool ok =
                network::osd_string(args.get("op")) == "release"
                    ? connector->release_agent(region.id, agent, opt(args.get("uri")))
                    : connector->close_agent(profile & 2 ? nullptr : &region, agent, opt(args.get("auth")));
            result = ok ? "1" : "0";
        } catch (const std::exception&) {
            result = "EX";
        }
        std::cout << result << "|" << trace << "\n";
    }
}
