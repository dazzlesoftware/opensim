#include "opensim/framework/config_http_linux_certificate_persistence.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
CertificateDer bytes(const std::string& text) {
    CertificateDer out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(text.substr(i, 2), nullptr, 16)));
    return out;
}
std::string hex(std::span<const std::uint8_t> value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : value)
        out << std::setw(2) << unsigned(c);
    return out.str();
}
std::string flags(const std::vector<std::uint32_t>& values) {
    std::string out;
    for (auto v : values) {
        if (!out.empty())
            out += ',';
        out += std::to_string(v);
    }
    return out.empty() ? "-" : out;
}
int main(int argc, char** argv) {
    if (argc != 3)
        return 2;
    int mode = std::stoi(argv[1]);
    auto ticks = std::stoll(argv[2]);
    std::string value;
    std::cin >> value;
    auto leaf = bytes(value);
    auto list = []() {
        std::size_t count;
        std::cin >> count;
        std::vector<CertificateDer> out;
        while (count--) {
            std::string text;
            std::cin >> text;
            out.push_back(bytes(text));
        }
        return out;
    };
    ConfigHttpLinuxCertificatePolicy policy;
    policy.custom_roots = list();
    auto extra = list();
    policy.check_name = false;
    policy.application_policies.clear();
    if (mode == 1)
        policy.certificate_policies.push_back(std::string("1.2.3.4.5"));
    policy.verification_unix_seconds = mode == 2 ? 2524608000 : 1893456000;
    ConfigXmlNativeHttpServices services;
    services.error = [](std::string, int) {
        return std::make_exception_ptr(std::runtime_error("Opaque chain download failure"));
    };
    ConfigHttpX509ResourceClient client(services);
    for (int i = 0; i < 2; ++i) {
        auto result = config_http_linux_certificate_errors_with_downloads(leaf, extra, u"", policy, client,
                                                                          ticks, mode == 3);
        std::cout << flags(result.status);
        for (const auto& element : result.elements)
            std::cout << ' ' << hex(element.certificate) << ':' << flags(element.status);
        std::cout << '\n';
    }
}
