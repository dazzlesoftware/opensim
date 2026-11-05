#include "opensim/framework/config_http_windows_certificate.hpp"
#include <iostream>
#include <sstream>
#include <string>
using namespace opensim::framework;
std::vector<std::uint8_t> bytes(std::string value) {
    std::vector<std::uint8_t> out;
    if (value == "_")
        return out;
    for (std::size_t i = 0; i < value.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(value.substr(i, 2), nullptr, 16)));
    return out;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string text;
        int count, custom, name, downloads;
        ConfigHttpWindowsCertificatePolicy policy;
        std::vector<std::vector<std::uint8_t>> extra;
        in >> text;
        auto leaf = bytes(text);
        in >> count;
        while (count--) {
            in >> text;
            extra.push_back(bytes(text));
        }
        in >> custom >> count;
        if (custom)
            policy.custom_roots.emplace();
        while (count--) {
            in >> text;
            policy.custom_roots->push_back(bytes(text));
        }
        std::uint64_t time;
        in >> time >> name >> downloads >> text;
        if (!in)
            return 2;
        policy.verification_filetime = time;
        policy.check_name = name != 0;
        policy.disable_certificate_downloads = downloads != 0;
        std::u16string host;
        if (text != "_")
            for (std::size_t i = 0; i < text.size(); i += 4)
                host += static_cast<char16_t>(std::stoul(text.substr(i, 4), nullptr, 16));
        std::cout << config_http_windows_certificate_errors(leaf, extra, host, policy) << '\n';
    }
}
