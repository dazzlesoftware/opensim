#include "opensim/services/user_agent_connector.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using Text = std::optional<std::u16string>;
Text D(const std::string& h) {
    if (h == "-")
        return {};
    if (h == ".")
        return u"";
    std::u16string out;
    for (std::size_t i = 0; i < h.size(); i += 4)
        out += char16_t(std::stoi(h.substr(i, 2), nullptr, 16) |
                        (std::stoi(h.substr(i + 2, 2), nullptr, 16) << 8));
    return out;
}
std::string H(const Text& t) {
    if (!t)
        return "-";
    if (t->empty())
        return ".";
    std::string out;
    for (auto c : *t)
        for (unsigned b : {unsigned(c) & 255, unsigned(c) >> 8}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        int flags;
        std::string data;
        input >> flags >> data;
        std::u16string trace;
        auto hit = [&](std::u16string text) {
            if (!trace.empty())
                trace += u'\n';
            trace += text;
        };
        services::UserAgentConnectorServices services;
        services.simulation_options.automatic_purge = false;
        services.client = [](int) -> std::shared_ptr<network::XmlRpcRequesterClient> {
            throw std::logic_error("Unexpected XMLRPC");
        };
        services.debug = [&](const auto& t) {
            hit(u"debug:" + t);
            if (flags & 4)
                throw network::NwcXmlRpcError("System.InvalidOperationException", u"debug");
        };
        services.error = [&](const auto& t) {
            hit(u"error:" + t);
            if (flags & 2)
                throw network::NwcXmlRpcError("System.InvalidOperationException", u"error");
        };
        services.warn = [](const auto&) {};
        Text url;
        std::string status = "OK";
        try {
            std::shared_ptr<framework::NiniConfigSourceBase> source;
            if (auto text = D(data)) {
                auto arena = std::make_shared<framework::NiniConfigArena>(
                    framework::native_nini_invariant_config_backend());
                auto ini = arena->create_ini_source();
                ini->case_sensitive((flags & 1) != 0);
                ini->load(framework::nini_string_reader(*text));
                source = ini;
            }
            services::UserAgentServiceConnector connector(
                source, std::move(services), [&](const auto& host) -> std::optional<std::string> {
                    hit(u"dns:" + framework::utf16(host));
                    if (flags & 8)
                        throw std::runtime_error("dns");
                    return host == "bad.invalid" ? std::nullopt : std::optional<std::string>{"203.0.113.7"};
                });
            url = connector.server_url();
        } catch (const network::NwcXmlRpcError& e) {
            status = "E:" + e.source_type + ":" + H(e.message);
        } catch (const framework::NiniConfigError& e) {
            status = "E:" + std::string(e.source_type()) + ":" + H(e.message);
        } catch (const std::exception& e) {
            status = "UNEXPECTED:" + std::string(e.what());
        }
        std::cout << status << '|' << H(url) << '|' << H(trace) << '\n';
    }
}
