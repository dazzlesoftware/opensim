#include "opensim/framework/config_http_linux_aia.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
int main() {
    auto uris = native_config_uri_object_backend();
    std::string line;
    constexpr char hex[] = "0123456789abcdef";
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string oid, encoded;
        in >> oid >> encoded;
        std::vector<std::uint8_t> bytes;
        if (encoded != "_")
            for (std::size_t i = 0; i < encoded.size(); i += 2)
                bytes.push_back(static_cast<std::uint8_t>(std::stoul(encoded.substr(i, 2), nullptr, 16)));
        auto value = config_http_linux_find_http_aia_record(bytes, oid == "_" ? "" : oid, *uris);
        if (!value)
            std::cout << '_';
        else
            for (auto c : *value)
                for (int shift = 12; shift >= 0; shift -= 4)
                    std::cout << hex[(c >> shift) & 15];
        std::cout << '\n';
    }
}
