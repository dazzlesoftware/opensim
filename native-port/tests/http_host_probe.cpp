#include "opensim/framework/http_host.hpp"
#include "opensim/framework/config_uri_host_name.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace opensim::framework;
ConfigUriText D(const std::string& h) {
    if (h == "N")
        return {};
    std::u16string out;
    if (h != ".")
        for (std::size_t i = 0; i < h.size(); i += 4)
            out += char16_t(std::stoi(h.substr(i, 2), nullptr, 16) |
                            (std::stoi(h.substr(i + 2, 2), nullptr, 16) << 8));
    return std::make_shared<const std::u16string>(std::move(out));
}
std::string H(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::optional<std::string> T(ConfigUriText t) {
    return t ? std::optional{utf8(*t)} : std::nullopt;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        int mode;
        std::string a, b;
        input >> mode >> a >> b;
        std::string result;
        try {
            if (mode == 6) {
                std::cout << H("TYPE:" + std::to_string(static_cast<int>(config_uri_check_host_name(D(a)))))
                          << '\n';
                continue;
            }
            std::string dns;
            RegionUri::Resolver resolver = [&](const auto& host) -> std::optional<std::string> {
                if (!dns.empty())
                    dns += ",";
                dns += host;
                if (mode == 4 || mode == 5)
                    throw std::runtime_error("dns");
                return host == "bad.invalid" ? std::nullopt : std::optional<std::string>{"203.0.113.7"};
            };
            HttpHost host(T(D(a)), mode == 1 || mode == 4, resolver);
            if (mode == 2 || mode == 5)
                host.resolve_dns(resolver);
            std::string error;
            host.valid_and_resolved(error);
            result = std::to_string(host.flags) + "|" + std::to_string(host.port) + "|" + host.host + "|" +
                     host.uri + "|" + host.ip.value_or("NULL") + "|" + host.uri_slash() + "|" +
                     host.host_port() + "|" + (host.valid() ? "1" : "0") + "|" +
                     (host.resolved() ? "1" : "0") + "|" + error + "|" + dns;
            if (mode == 3) {
                HttpHost other(T(D(b)));
                result += "|" + std::string(host.equals(other) ? "1" : "0") + "|" +
                          std::to_string(host.compare(other));
            }
        } catch (...) {
            result = "EX";
        }
        std::cout << H(result) << '\n';
    }
}
