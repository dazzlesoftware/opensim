#include "opensim/services/user_agent_connector.hpp"
#include "opensim/network/osd_service_http.hpp"
#include "opensim/framework/config_http_dns_error.hpp"
#include "opensim/framework/config_http_write_error.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
int main() {
    framework::ConfigXmlNativeHttpServices http;
    http.proxy = [](auto, auto) -> std::optional<framework::ConfigXmlHttpRoute> { return {}; };
    http.connect_error = framework::config_http_connect_error;
    http.dns_error = framework::config_http_dns_error;
    http.read_error = framework::config_http_read_error;
    http.write_error = framework::config_http_write_error;
    http.error = [](std::string phase, int code) {
        return std::make_exception_ptr(
            std::runtime_error("UNMAPPED HTTP " + phase + ":" + std::to_string(code)));
    };
    network::OsdServiceNativeHttp transport(http);
    network::OsdServiceServices web;
    int number = 0;
    web.request_number = [&] { return number; };
    web.set_request_number = [&](int n) { number = n; };
    web.debug_level = [] { return 0; };
    web.debug = web.info = [](const auto&) {};
    web.tick_count = [] { return 0; };
    web.elapsed = [](int) { return 0; };
    transport.bind(web);
    auto requester = std::make_shared<network::OsdServiceRequester>(std::move(web));
    auto id = [](int n) {
        return framework::Uuid("00000000-0000-0000-0000-00000000000" + std::to_string(n));
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::string url;
        int profile;
        input >> profile >> url;
        services::UserAgentConnectorServices services;
        services.debug = services.error = services.warn = [](const auto&) {};
        services.client = [](int) -> std::shared_ptr<network::XmlRpcRequesterClient> {
            throw std::logic_error("XMLRPC is outside grid-login path");
        };
        services.simulation_options.automatic_purge = false;
        services.simulation_request = [requester](const auto& u, const auto& data, const auto& method,
                                                  bool compressed, bool keepalive) {
            return requester->send(framework::utf16(u), &data, framework::utf16(method), 30000, compressed,
                                   false, keepalive);
        };
        services::UserAgentServiceConnector connector(framework::utf16(url), std::move(services));
        services::GridRegion source;
        source.id = id(2);
        source.x = -256;
        source.y = 1024;
        source.name = "Source";
        source.raw_server_uri = "https://SOURCE/raw";
        auto gate = std::make_shared<services::GridRegion>();
        gate->id = id(3);
        gate->external_host = "GATE.host";
        gate->http_port = 9001;
        gate->set_server_uri("https://GATE/Case");
        auto destination = std::make_shared<services::GridRegion>();
        destination->id = id(1);
        destination->x = 25600;
        destination->y = 51200;
        destination->name = "Destination";
        destination->set_server_uri("https://Destination/Case");
        auto circuit = std::make_shared<framework::AgentCircuitData>();
        circuit->agent_id = id(7);
        circuit->session_id = id(8);
        circuit->secure_session_id = id(9);
        circuit->circuit_code = 0xffffffff;
        circuit->first_name = "First";
        circuit->last_name = "Last";
        circuit->caps_path = "caps/";
        circuit->start_position = {1.25f, 2, 30};
        if (profile & 64) {
            circuit->service_urls = std::make_shared<framework::AgentCircuitData::ServiceUrls>();
            (*circuit->service_urls)["Large"] = std::string(150000, 'x');
            (*circuit->service_urls)["Unicode"] = framework::utf8(u"\u00e9\u4e2d\U0001f600");
            (*circuit->service_urls)["Bool"] = true;
            (*circuit->service_urls)["Integer"] = std::int32_t(-7);
            (*circuit->service_urls)["Null"] = nullptr;
            circuit->children_cap_seeds = std::make_shared<framework::AgentCircuitData::ChildrenSeeds>();
            (*circuit->children_cap_seeds)[0xffffffffffffffffULL] = "https://seed/Case";
            (*circuit->children_cap_seeds)[1] = std::nullopt;
            circuit->start_far = 512.25f;
        }
        if (profile & 2)
            circuit->appearance = std::make_shared<framework::AvatarAppearance>();
        if (profile & 8)
            destination.reset();
        if (profile & 16)
            gate.reset();
        if (profile & 32)
            circuit.reset();
        std::string reason, status;
        try {
            status = connector.login_agent_to_grid_owned((profile & 1) ? &source : nullptr, circuit, gate,
                                                         destination, (profile & 4) != 0, reason)
                         ? "1"
                         : "0";
        } catch (const services::SimulationConnectorError& e) {
            status = "E:" + e.source_type + ":" + framework::utf8(e.message);
        } catch (const std::exception& e) {
            status = "UNEXPECTED:" + std::string(e.what());
        }
        std::cout << network::osd_json(network::Value::object({{"status", status}, {"reason", reason}}), true)
                  << '\n';
    }
}
