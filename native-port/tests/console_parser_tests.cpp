// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_parser.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim::framework;
namespace {
std::u16string decode(const std::string& hex) {
    if (hex == ".")
        return {};
    if (hex.size() % 4)
        throw std::runtime_error("Invalid UTF16 fixture");
    std::u16string result;
    for (std::size_t i = 0; i < hex.size(); i += 4) {
        auto lo = std::stoul(hex.substr(i, 2), nullptr, 16),
             hi = std::stoul(hex.substr(i + 2, 2), nullptr, 16);
        result.push_back(static_cast<char16_t>(lo | (hi << 8)));
    }
    return result;
}
std::string encode(const std::u16string& value) {
    std::string out;
    for (auto c : value) {
        for (unsigned byte : {unsigned(c) & 255, unsigned(c) >> 8}) {
            out += "0123456789abcdef"[byte >> 4];
            out += "0123456789abcdef"[byte & 15];
        }
    }
    return out;
}
std::string probe(const std::string& text) {
    try {
        std::u16string units;
        std::optional<std::u16string_view> value;
        if (text != "-") {
            units = decode(text);
            value = units;
        }
        auto parsed = parse_console_command(value);
        std::string output = std::to_string(parsed.size());
        for (auto& token : parsed)
            output += '|' + encode(token);
        return output;
    } catch (const std::exception& e) {
        return std::string("EX:") + e.what();
    }
}
void expect(std::u16string_view text, std::vector<std::u16string> expected) {
    if (parse_console_command(text) != expected)
        throw std::runtime_error("Original console parser behavior");
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string text;
            while (std::getline(std::cin, text))
                std::cout << probe(text) << '\n';
            return 0;
        }
        expect(u"fcache clear \"memory\"", {u"fcache", u"clear", u"memory"});
        expect(u"--key=\"a b\"tail", {u"--key=\"a b\"", u"tail"});
        expect(u"--key=\"unterminated", {u"--key=\"unterminated\""});
        expect(u"a\"b\"c", {u"a", u"b", u"c"});
        expect(u"\"\"", {u""});
        expect(u"\t\u00a0\u2003x\u2028y", {u"x", u"y"});
        if (parse_console_command_utf8("fcache clear \"memory\"") !=
            std::vector<std::string>{"fcache", "clear", "memory"})
            throw std::runtime_error("UTF8 console boundary");
        if (!probe("-").starts_with("EX:Object reference"))
            throw std::runtime_error("Null console input");
        std::cout << "PASS: console parser quote/option/Unicode/null behavior\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
