// Copyright (c) Contributors, http://opensimulator.org/; BSD native-port/LICENSE.txt.
#include "opensim/framework/dotnet_regex_class_analysis.hpp"
#include "opensim/framework/text.hpp"
#include <functional>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim::framework;
std::optional<std::u16string> dec(const std::string& s) {
    if (s == "-")
        return {};
    std::u16string text;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 4)
            text.push_back(static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16)));
    return text;
}
std::string hex(std::u16string_view s) {
    if (s.empty())
        return ".";
    std::ostringstream out;
    for (auto c : s)
        out << std::hex << std::setfill('0') << std::setw(4) << unsigned(c);
    return out.str();
}
std::string bit(bool b) {
    return b ? "1" : "0";
}
std::string capture(const std::function<std::string()>& f) {
    try {
        return f();
    } catch (const DotnetRegexClassError& e) {
        std::ostringstream out;
        out << "E:" << e.source_type << ':' << std::hex << e.hresult << ':' << hex(utf16(e.what()));
        return out.str();
    }
}
int main() {
    for (std::string row; std::getline(std::cin, row);) {
        std::istringstream in(row);
        std::string mode, text, other;
        in >> mode;
        std::vector<std::string> output;
        std::optional<std::u16string> storage;
        if (mode != "W" && mode != "J" && mode != "D") {
            in >> text;
            storage = dec(text);
        }
        DotnetRegexClassText set = storage ? DotnetRegexClassText(*storage) : std::nullopt;
        auto span = storage ? std::span<const char16_t>(*storage) : std::span<const char16_t>{};
        if (mode == "A") {
            for (auto f : {dotnet_regex_class_mergeable, dotnet_regex_class_empty,
                           dotnet_regex_class_singleton, dotnet_regex_class_singleton_inverse})
                output.push_back(capture([&]() { return bit(f(set)); }));
            output.push_back(capture([&]() { return bit(dotnet_regex_class_negated(set)); }));
            output.push_back(capture([&]() { return bit(dotnet_regex_class_subtraction(set)); }));
            output.push_back(
                capture([&]() { return std::to_string(unsigned(dotnet_regex_singleton_char(set))); }));
            output.push_back(capture([&]() {
                auto r = dotnet_regex_analyze_class(set);
                return bit(r.only_ranges) + "," + bit(r.contains_only_ascii) + "," +
                       bit(r.contains_no_ascii) + "," + bit(r.all_ascii_contained) + "," +
                       bit(r.all_non_ascii_contained) + "," + std::to_string(r.lower_bound) + "," +
                       std::to_string(r.upper_bound);
            }));
            char16_t lo = 0xaaaa, hi = 0xbbbb;
            auto first = capture([&]() { return bit(dotnet_regex_single_range(set, lo, hi)); });
            output.push_back(first + ":" + std::to_string(unsigned(lo)) + "," + std::to_string(unsigned(hi)));
            DotnetRegexRange r0{0x1111, 0x2222}, r1{0x3333, 0x4444};
            first = capture([&]() { return bit(dotnet_regex_double_range(set, r0, r1)); });
            output.push_back(first + ":" + std::to_string(unsigned(r0.first)) + "," +
                             std::to_string(unsigned(r0.last)) + "," + std::to_string(unsigned(r1.first)) +
                             "," + std::to_string(unsigned(r1.last)));
        } else if (mode == "K") {
            int capacity;
            in >> capacity;
            std::vector<std::int32_t> buffer(capacity, 0x76543210);
            int count = -7;
            bool negative = true;
            auto value =
                capture([&]() { return bit(dotnet_regex_only_categories(set, buffer, count, negative)); });
            value += ":" + std::to_string(count) + ":" + bit(negative) + ":";
            for (std::size_t i = 0; i < buffer.size(); ++i) {
                if (i)
                    value += ',';
                value += std::to_string(buffer[i]);
            }
            output.push_back(value);
        } else if (mode == "T") {
            int capacity;
            in >> capacity;
            std::vector<char16_t> buffer(capacity, 0xa55a);
            auto value = capture([&]() { return std::to_string(dotnet_regex_set_chars(set, buffer)); });
            output.push_back(value + ":" + hex(std::u16string_view(buffer.data(), buffer.size())));
        } else if (mode == "F") {
            std::optional<std::u16string> result = u"\u9999";
            auto value = capture([&]() {
                result = dotnet_regex_ascii_set_chars(set);
                return bit(result.has_value());
            });
            output.push_back(value + ":" + (result ? hex(*result) : "-"));
        } else if (mode == "O") {
            in >> other;
            auto value = dec(other);
            DotnetRegexClassText second = value ? DotnetRegexClassText(*value) : std::nullopt;
            output.push_back(capture([&]() { return bit(dotnet_regex_may_overlap(set, second)); }));
        } else if (mode == "N") {
            int offset;
            in >> offset;
            output.push_back(capture([&]() { return bit(dotnet_regex_class_negated(set, offset)); }));
        } else if (mode == "H") {
            std::shared_ptr<const std::u16string> first, second;
            output.push_back(capture([&]() {
                first = dotnet_regex_chars_to_string_class(span);
                second = dotnet_regex_chars_to_string_class(span);
                return hex(*first) + ":" + bit(first == second) + ":" +
                       bit(dotnet_regex_participates_in_case_conversion(span)) + ":" +
                       bit(dotnet_regex_is_ascii(span));
            }));
            if (span.size() == 1)
                output.push_back(bit(first == dotnet_regex_one_to_string_class(span[0])));
        } else if (mode == "I") {
            in >> other;
            auto second = dec(other);
            auto other_span = second ? std::span<const char16_t>(*second) : std::span<const char16_t>{};
            auto first = dotnet_regex_chars_to_string_class(span);
            output.push_back(bit(first == dotnet_regex_chars_to_string_class(other_span)));
        } else if (mode == "R") {
            output.push_back(capture([&]() {
                auto ranges = dotnet_regex_compute_ranges(span);
                if (!ranges)
                    return std::string("-");
                std::string value;
                for (std::size_t i = 0; i < ranges->size(); ++i) {
                    if (i)
                        value += ',';
                    value += std::to_string(unsigned((*ranges)[i].first)) + ":" +
                             std::to_string(unsigned((*ranges)[i].last));
                }
                return value;
            }));
        } else if (mode == "J") {
            std::int32_t value;
            in >> value;
            output.push_back(bit(dotnet_regex_participates_in_case_conversion(value)));
        } else if (mode == "D") {
            unsigned a, b;
            in >> a >> b;
            int mask = -1;
            bool result =
                dotnet_regex_differ_by_one_bit(static_cast<char16_t>(a), static_cast<char16_t>(b), mask);
            output.push_back(bit(result) + ":" + std::to_string(mask));
        } else if (mode == "W") {
            std::string value;
            for (int i = 0; i < 65536; ++i) {
                char16_t c = static_cast<char16_t>(i);
                std::span<const char16_t> single(&c, 1);
                value += char('0' + dotnet_regex_participates_in_case_conversion(i) +
                              (dotnet_regex_participates_in_case_conversion(single) << 1) +
                              (dotnet_regex_is_ascii(single) << 2));
            }
            output.push_back(value);
        }
        for (std::size_t i = 0; i < output.size(); ++i) {
            if (i)
                std::cout << '|';
            std::cout << output[i];
        }
        std::cout << '\n';
    }
}
