#include "opensim/network/service_protocol.hpp"
#include <iostream>
using opensim::network::ServiceXmlValue;
std::string hex(const std::string& text) {
    const char* h = "0123456789abcdef";
    std::string out;
    for (unsigned char c : text) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string snap(const ServiceXmlValue& value) {
    if (!value.list)
        return "S" + hex(value.text);
    std::string out = "D" + std::to_string(value.fields.size()) + "{";
    for (const auto& [key, child] : value.fields)
        out += hex(key) + "=" + snap(child) + ";";
    return out + "}";
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string xml;
        for (std::size_t i = 0; i < line.size(); i += 2)
            xml += static_cast<char>(std::stoi(line.substr(i, 2), nullptr, 16));
        std::cout << snap(opensim::network::parse_service_xml(xml)) << "\n";
    }
}
