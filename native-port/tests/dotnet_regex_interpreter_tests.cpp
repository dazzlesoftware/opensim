// Copyright (c) Contributors, http://opensimulator.org/; BSD native-port/LICENSE.txt.
#include "opensim/framework/dotnet_regex_interpreter.hpp"
#include "opensim/framework/text.hpp"
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
using namespace opensim::framework;
DotnetRegexNodeText dec(const std::string& s) {
    if (s == "-")
        return {};
    std::u16string value;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 4)
            value.push_back(static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16)));
    return value.empty() ? runtime_empty_text() : std::make_shared<const std::u16string>(value);
}
std::string hex(std::u16string_view s) {
    if (s.empty())
        return ".";
    std::ostringstream out;
    for (auto c : s)
        out << std::hex << std::setfill('0') << std::setw(4) << unsigned(c);
    return out.str();
}
std::string hex(const DotnetRegexNodeText& s) {
    return s ? hex(std::u16string_view(*s)) : "-";
}
std::string format(const DotnetRegexScanResult& result) {
    if (!result.success)
        return "0:-1:-1:-1";
    std::string value = "1:" + std::to_string(result.index) + ":" + std::to_string(result.length) + ":" +
                        std::to_string(result.text_position);
    for (auto& group : result.groups) {
        value += "/" + std::to_string(group.number) + ":" + hex(std::u16string_view(group.name)) + ":" +
                 std::to_string(group.success) + ":" + std::to_string(group.index) + ":" +
                 std::to_string(group.length) + ":";
        for (auto capture : group.captures)
            value += std::to_string(capture.index) + "," + std::to_string(capture.length) + "," +
                     hex(std::u16string_view(*result.input).substr(capture.index, capture.length)) + ";";
    }
    return value;
}
int main() {
    for (std::string row; std::getline(std::cin, row);) {
        try {
            std::istringstream in(row);
            std::string op, name, expression, body;
            int options, begin, length, start, previous, mode, force;
            std::int64_t ticks;
            in >> op;
            if (op == "T") {
                in >> ticks;
                dotnet_regex_validate_timeout(ticks);
                std::cout << "T\n";
                continue;
            }
            in >> options >> name >> expression >> body >> begin >> length >> start >> previous >> mode >>
                ticks >> force;
            auto pattern = dec(expression), input = dec(body),
                 culture_name = name == "_" ? DotnetRegexNodeText{} : dec(name);
            int reads = 0;
            auto culture = [&]() {
                ++reads;
                if (!culture_name)
                    throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                                "Object reference not set to an instance of an object.");
                return *culture_name;
            };
            auto tree = dotnet_regex_parse(pattern, options, culture, [] { return true; });
            if (name == "_")
                tree.backreference_culture.reset();
            DotnetRegexInterpreter interpreter(std::move(tree), pattern, [] { return true; });
            std::string output;
            int calls = 0;
            DotnetRegexTickCount clock;
            if (force == 1)
                clock = [&]() {
                    return calls++ == 0 ? std::int64_t{0} : std::numeric_limits<std::int64_t>::max();
                };
            if (force == 2)
                clock = [&]() { return calls++ == 0 ? std::int64_t{1000000000000} : std::int64_t{0}; };
            int iterations = 0;
            do {
                calls = 0;
                auto result = interpreter.scan(input, begin, length, start, previous,
                                               static_cast<DotnetRegexRunnerMode>(mode), ticks, clock);
                if (iterations++)
                    output += '|';
                output += format(result);
                if (op != "A" || !result.success || mode == 0)
                    break;
                previous = result.length;
                start = result.text_position;
                if (iterations > 1000)
                    throw std::runtime_error("Match enumeration did not terminate");
            } while (true);
            std::cout << output << "#" << reads << '\n';
        } catch (const DotnetRegexMatchTimeout& e) {
            std::cout << "T:" << hex(e.input) << ":" << hex(e.pattern) << ":" << e.timeout_ticks << '\n';
        } catch (const DotnetRegexParseException& e) {
            std::cout << "P:" << e.error << ":" << e.offset << ":" << hex(std::u16string_view(e.message))
                      << '\n';
        } catch (const DotnetRegexClassError& e) {
            std::ostringstream h;
            h << std::hex << e.hresult;
            std::cout << "E:" << e.source_type << ":" << h.str() << ":"
                      << hex(std::u16string_view(utf16(e.what()))) << '\n';
        }
    }
    return 0;
}
