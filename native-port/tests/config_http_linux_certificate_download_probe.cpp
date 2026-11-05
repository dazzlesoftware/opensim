#include "opensim/framework/config_http_linux_certificate_download.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
int main() {
    ConfigXmlNativeHttpServices policy;
    policy.error = [](std::string, int) {
        return std::make_exception_ptr(std::runtime_error("Opaque certificate download failure"));
    };
    ConfigHttpX509ResourceClient client(policy);
    std::string line;
    auto hex = [](const auto& bytes) {
        constexpr char digits[] = "0123456789abcdef";
        for (auto byte : bytes)
            std::cout << digits[byte >> 4] << digits[byte & 15];
    };
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::int64_t ticks;
        std::string encoded;
        input >> ticks >> encoded;
        std::u16string uri;
        for (std::size_t i = 0; i < encoded.size(); i += 4)
            uri += static_cast<char16_t>(std::stoul(encoded.substr(i, 4), nullptr, 16));
        auto cert = config_http_linux_download_certificate(client, uri, ticks);
        if (!cert)
            std::cout << '_';
        else {
            hex(cert->der);
            std::cout << ' ';
            hex(cert->auxiliary_der);
        }
        std::cout << '\n';
    }
}
