#include "opensim/framework/invariant_number_parse.hpp"
#include <bit>
#include <iostream>
#include <sstream>
using namespace opensim::framework;
namespace {
std::optional<std::u16string> decode(const std::string& s) {
    if (s == "-")
        return {};
    std::u16string out;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 4)
            out += static_cast<char16_t>(std::stoul(s.substr(i, 2), nullptr, 16) |
                                         (std::stoul(s.substr(i + 2, 2), nullptr, 16) << 8));
    return out;
}
std::string hex(const std::u16string& s) {
    if (s.empty())
        return ".";
    std::string out;
    for (auto c : s)
        for (unsigned char b : {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8)}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out;
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            for (std::string row; std::getline(std::cin, row);) {
                std::istringstream in(row);
                int op;
                std::string encoded;
                in >> op >> encoded;
                auto value = decode(encoded);
                try {
                    std::string result;
                    if (op == 0)
                        result = std::to_string(invariant_convert_to_int32(value));
                    else if (op == 1)
                        result = std::to_string(invariant_convert_to_int64(value));
                    else if (op == 2)
                        result =
                            std::to_string(std::bit_cast<std::uint32_t>(invariant_convert_to_single(value)));
                    else
                        result =
                            std::to_string(std::bit_cast<std::uint64_t>(invariant_convert_to_double(value)));
                    std::cout << "V:" << result << '\n';
                } catch (const InvariantNumberError& e) {
                    std::cout << "EX:"
                              << (e.kind == InvariantNumberError::Kind::Format ? "System.FormatException:"
                                                                               : "System.OverflowException:")
                              << std::to_string(e.kind == InvariantNumberError::Kind::Format ? 0x80131537u
                                                                                             : 0x80131516u)
                              << ':' << hex(e.message) << '\n';
                }
            }
            return 0;
        }
        require(invariant_convert_to_int32({}) == 0, "Convert null");
        require(invariant_convert_to_int64(u"-9223372036854775808") == INT64_MIN, "Int64 minimum");
        require(invariant_convert_to_int32(std::u16string(u"42 \0\0", 5)) == 42, "Trailing source NULs");
        require(std::bit_cast<std::uint32_t>(
                    invariant_convert_to_single(u"1.0000000596046447753906250000001")) == 0x3f800001,
                "Direct Single midpoint rounding");
        require(std::bit_cast<std::uint64_t>(invariant_convert_to_double(u"-0e999999999999")) ==
                    0x8000000000000000ull,
                "Signed zero and saturated exponent");
        require(std::bit_cast<std::uint32_t>(invariant_convert_to_single(u"NaN")) == 0xffc00000,
                "Source NaN bits");
        std::cout << "PASS invariant numeric conversion\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
