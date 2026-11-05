// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/file_system_name.hpp"
#include "opensim/framework/file_system_name_case_data.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace opensim::framework::file_system_name;
namespace {
String decode(const std::string& text) {
    if (text == "~")
        return {};
    std::u16string out;
    if (text != ".") {
        if (text.size() % 4)
            throw std::runtime_error("Invalid UTF16 fixture");
        for (std::size_t i = 0; i < text.size(); i += 4)
            out.push_back(static_cast<char16_t>(std::stoul(text.substr(i, 2), nullptr, 16) |
                                                (std::stoul(text.substr(i + 2, 2), nullptr, 16) << 8)));
    }
    return std::make_shared<const std::u16string>(std::move(out));
}
std::string encode(std::u16string_view text) {
    if (text.empty())
        return ".";
    std::string out;
    for (auto c : text)
        for (auto b : {unsigned(c) & 255, unsigned(c) >> 8}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out;
}
std::u16string_view view(const String& s) {
    return s ? std::u16string_view(*s) : std::u16string_view{};
}
void require(bool b, const char* name) {
    if (!b)
        throw std::runtime_error(name);
}
void probe(CasingMode mode) {
    for (std::string line; std::getline(std::cin, line);) {
        try {
            std::istringstream row(line);
            std::string command;
            row >> command;
            if (command == "T") {
                std::string hex, intern;
                row >> hex >> intern;
                auto input = decode(hex);
                if (intern == "intern" && input && *input == u"*")
                    input = star_literal();
                auto out = translate_win32_expression(input);
                std::cout << encode(*out) << '|' << (input == out) << '|' << (out == star_literal()) << '\n'
                          << std::flush;
            } else if (command == "M") {
                std::string kind, ignore, e, n;
                row >> kind >> ignore >> e >> n;
                auto expression = decode(e), name = decode(n);
                std::cout << (kind == "w" ? matches_win32_expression(view(expression), view(name),
                                                                     ignore == "1", mode)
                                          : matches_simple_expression(view(expression), view(name),
                                                                      ignore == "1", mode))
                          << '\n'
                          << std::flush;
            } else
                throw std::runtime_error("Unknown query");
        } catch (const std::exception& e) {
            std::cout << "EX:" << e.what() << '\n' << std::flush;
        }
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            probe(argc > 2 && std::string(argv[2]) == "invariant" ? CasingMode::GlobalizationInvariant
                                                                  : CasingMode::Normal);
            return 0;
        }
        auto plain = std::make_shared<const std::u16string>(u"abc");
        require(translate_win32_expression(plain) == plain, "Unmodified expression identity");
        auto dot = std::make_shared<const std::u16string>(u"a.b");
        require(*translate_win32_expression(dot) == *dot && translate_win32_expression(dot) != dot,
                "Dot retains modified identity even when value unchanged");
        require(translate_win32_expression({}) == star_literal(), "Null star literal");
        require(*translate_win32_expression(std::make_shared<const std::u16string>(u"*.")) == u"<",
                "DOS terminal dot");
        require(!matches_simple_expression(u"*", u""), "Empty name rejection");
        require(matches_simple_expression(u"\\?", u"x"), "Source escaped-question behavior");
        require(matches_simple_expression(u"\\", u"x") && !matches_simple_expression(u"\\", u"xx"),
                "Source trailing-escape behavior");
        require(matches_simple_expression(u"*\\a", u"x\\a") && !matches_simple_expression(u"*\\a", u"xa"),
                "Fast suffix treats escape as literal");
        require(matches_simple_expression(u"s", u"\u017f") &&
                    !matches_simple_expression(u"s", u"\u017f", true, CasingMode::GlobalizationInvariant) &&
                    !matches_simple_expression(u"*s", u"\u017f"),
                "Mode-dependent code-unit case vs ordinal suffix");
        require(matches_simple_expression(u"*\U00010400", u"\U00010428") &&
                    !matches_simple_expression(u"\U00010400", u"\U00010428"),
                "Surrogate-aware suffix vs unit NFA");
        std::u16string calls;
        int suffix_calls = 0;
        CasingBackend selected{[&](char16_t c) {
                                   calls += c;
                                   return c;
                               },
                               [&](auto text, auto suffix) {
                                   ++suffix_calls;
                                   return text.ends_with(suffix);
                               }};
        require(!matches_simple_expression(u"b", u"a", true, selected) && calls == u"ba" && suffix_calls == 0,
                "Unit backend source call order");
        calls.clear();
        require(matches_simple_expression(u"*a", u"za", true, selected) && calls.empty() && suffix_calls == 1,
                "Distinct ordinal backend branch");
        CasingBackend absent;
        require(matches_simple_expression(u"??", u"ab", true, absent) &&
                    matches_simple_expression(u"a", u"a", false, absent),
                "Case backend is consulted lazily");
        std::cout << "PASS: FileSystemName identity/NFA/casing checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
