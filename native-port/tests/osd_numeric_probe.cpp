// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "osd_fixture_value.hpp"
#include <bit>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim::network;
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            const auto value = fixture::build(line);
            const auto integer = osd_integer(value);
            const auto real = osd_real(value);
            std::string extra;
            if (value.kind == Value::Kind::Long) {
                const auto decoded = parse_llsd_xml(llsd_xml(value));
                if (decoded.kind != Value::Kind::Binary)
                    throw std::runtime_error("Invalid long LLSD representation");
                extra = "|" + decoded.text;
            }
            const auto hex = [](const std::string& text) {
                constexpr char alphabet[] = "0123456789abcdef";
                std::string out;
                for (unsigned char c : text) {
                    out += alphabet[c >> 4];
                    out += alphabet[c & 15];
                }
                return out;
            };
            std::string binary;
            try {
                const auto bytes = osd_binary(value);
                binary = hex(std::string(bytes->begin(), bytes->end()));
            } catch (const std::exception&) {
                binary = "EX";
            }
            std::string alias = "-";
            if (value.kind == Value::Kind::Binary || value.kind == Value::Kind::Boolean) {
                const auto first = osd_binary(value);
                if (!first->empty()) {
                    const auto saved = first->front();
                    first->front() ^= 85;
                    alias = first == osd_binary(value) && osd_binary(value)->front() == first->front() ? "1"
                                                                                                       : "0";
                    first->front() = saved;
                }
            }
            std::cout << integer << '|' << std::hex << std::setw(16) << std::setfill('0')
                      << std::bit_cast<std::uint64_t>(real) << std::dec << '|' << osd_unsigned(value) << '|'
                      << hex(osd_string(value)) << '|' << binary << '|' << alias << extra << '\n';
        } catch (const std::exception&) {
            std::cout << "EX\n";
        }
    }
    return 0;
}
