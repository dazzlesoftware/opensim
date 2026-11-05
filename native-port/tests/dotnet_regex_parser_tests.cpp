// Copyright (c) Contributors, http://opensimulator.org/; BSD native-port/LICENSE.txt.
#include "opensim/framework/dotnet_regex_parser.hpp"
#include "opensim/framework/dotnet_regex_bytecode.hpp"
#include "opensim/framework/dotnet_regex_prefix.hpp"
#include "opensim/framework/dotnet_regex_find.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim::framework;
DotnetRegexNodeText dec(std::string s) {
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
std::string graph(DotnetRegexNode* root) {
    std::vector<DotnetRegexNode*> nodes;
    auto id = [&](DotnetRegexNode* n) {
        if (!n)
            return std::string("-");
        auto it = std::find(nodes.begin(), nodes.end(), n);
        if (it == nodes.end()) {
            nodes.push_back(n);
            return std::to_string(nodes.size() - 1);
        }
        return std::to_string(it - nodes.begin());
    };
    id(root);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        auto n = nodes[i];
        for (int j = 0; j < n->child_count(); ++j)
            id(n->child(j));
        id(n->parent());
    }
    std::string result;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        auto n = nodes[i];
        if (i)
            result += '/';
        result += std::to_string(unsigned(n->kind())) + ":" + std::to_string(n->options()) + ":" +
                  std::to_string(unsigned(n->character())) + ":" + std::to_string(n->minimum()) + ":" +
                  std::to_string(n->maximum()) + ":" + hex(n->text()) + ":" + id(n->parent()) + ":";
        for (int j = 0; j < n->child_count(); ++j)
            result += id(n->child(j)) + ",";
        std::size_t first = 0;
        while (nodes[first]->text() != n->text())
            ++first;
        result += "@" + std::to_string(first);
    }
    return result;
}
std::string fixed_sets(const std::optional<std::vector<DotnetRegexFixedDistanceSet>>& sets) {
    if (!sets)
        return "-";
    std::string result;
    for (auto& item : *sets) {
        std::u16string chars;
        if (item.chars)
            chars.assign(item.chars->begin(), item.chars->end());
        result +=
            hex(item.set) + ":" + std::to_string(item.distance) + ":" + std::to_string(item.negated) + ":" +
            (item.chars ? hex(std::u16string_view(chars)) : "-") + ":" +
            (item.range ? std::to_string(item.range->first) + "," + std::to_string(item.range->last) : "-") +
            ";";
    }
    return result;
}
std::string prefix_data(DotnetRegexNode* root) {
    auto stack = [] { return true; };
    auto first = dotnet_regex_find_prefix(root, stack);
    auto ordinal = dotnet_regex_find_ordinal_case_insensitive_prefix(root);
    auto first_class = dotnet_regex_find_first_char_class(root, stack);
    auto plain = dotnet_regex_find_fixed_distance_sets(root, false, stack);
    auto thorough = dotnet_regex_find_fixed_distance_sets(root, true, stack);
    std::string result = hex(first) + "|" + hex(ordinal) + "|" + hex(first_class) + "|" +
                         std::to_string(unsigned(dotnet_regex_find_leading_anchor(root, stack))) + "," +
                         std::to_string(unsigned(dotnet_regex_find_trailing_anchor(root, stack))) + "|" +
                         fixed_sets(plain) + "|" + fixed_sets(thorough) + "|";
    if (thorough)
        dotnet_regex_sort_fixed_distance_sets(*thorough);
    result += fixed_sets(thorough) + "|";
    auto literal = dotnet_regex_find_literal_after_loop(root);
    if (!literal)
        return result + "-";
    std::u16string chars;
    if (literal->chars)
        chars.assign(literal->chars->begin(), literal->chars->end());
    return result + graph(literal->loop) + ":" + std::to_string(literal->character) + ":" +
           hex(literal->string) + ":" + (literal->chars ? hex(std::u16string_view(chars)) : "-");
}
std::string caches(const DotnetRegexFindOptimizations& find) {
    if (!find.ascii_lookup_count())
        return "-";
    std::string result;
    for (std::size_t i = 0; i < find.ascii_lookup_count(); ++i) {
        auto value = find.ascii_lookup(i).load();
        if (!value)
            result += "-";
        else
            for (auto& word : *value)
                result += std::to_string(word.load()) + ",";
        result += ';';
    }
    return result;
}
std::string find_data(const DotnetRegexFindOptimizations& find) {
    auto& d = find.data();
    std::string result =
        std::to_string(static_cast<int>(d.mode)) + ":" + std::to_string(find.is_useful()) + ":" +
        std::to_string(unsigned(d.leading_anchor)) + ":" + std::to_string(unsigned(d.trailing_anchor)) + ":" +
        std::to_string(d.minimum_length) + ":" +
        (d.maximum_length ? std::to_string(*d.maximum_length) : "-") + ":" + hex(d.leading_prefix) + ":" +
        std::to_string(d.fixed_literal.character) + "," + hex(d.fixed_literal.string) + "," +
        std::to_string(d.fixed_literal.distance) + ":" + fixed_sets(d.fixed_sets) + ":";
    if (d.literal_after_loop) {
        auto& item = *d.literal_after_loop;
        std::u16string chars;
        if (item.chars)
            chars.assign(item.chars->begin(), item.chars->end());
        result += graph(item.loop) + ":" + std::to_string(item.character) + ":" + hex(item.string) + ":" +
                  (item.chars ? hex(std::u16string_view(chars)) : "-");
    } else
        result += "-";
    return result + ":" + caches(find);
}
int main() {
    for (std::string line; std::getline(std::cin, line);) {
        try {
            std::istringstream in(line);
            std::string op, encoded, name;
            int options;
            in >> op >> options >> name >> encoded;
            auto pattern = dec(encoded);
            if (op == "T") {
                std::cout << dotnet_regex_opcode_backtracks(options) << '\n';
                continue;
            }
            if (op == "C") {
                auto cultureName = dec(name);
                int reads = 0;
                auto target = dotnet_regex_target_culture(options, [&]() {
                    ++reads;
                    return *cultureName;
                });
                std::cout << hex(std::u16string_view(target())) << ":" << reads << '\n';
                continue;
            }
            if (op == "E" || op == "U") {
                auto output = op == "E" ? dotnet_regex_escape(pattern) : dotnet_regex_unescape(pattern);
                std::cout << hex(output) << ":" << (output == pattern) << '\n';
                continue;
            }
            if (op == "R" || op == "M" || op == "G") {
                int context = std::stoi(name), size = context == 1   ? 3
                                                      : context == 3 ? 10
                                                      : context == 4 ? 2
                                                      : context == 5 ? 10
                                                      : context == 6 ? -1
                                                                     : 0;
                std::optional<std::map<int, int>> caps;
                std::optional<std::map<std::u16string, int>> names;
                if (context == 2) {
                    caps = std::map<int, int>{{0, 0}, {2, 1}, {5, 2}};
                    names = std::map<std::u16string, int>{{u"x", 2}, {u"y", 5}, {u"0", 0}};
                    size = 3;
                }
                if (context == 3)
                    names = std::map<std::u16string, int>{{u"a", 1}, {u"b", 9}};
                if (context == 4) {
                    caps = std::map<int, int>{{0, 0}, {1, 2147483647}};
                    names = std::map<std::u16string, int>{{u"x", 1}};
                }
                if (context == 5) {
                    caps.emplace();
                    names = std::map<std::u16string, int>{{u"x", 1}};
                }
                if (context == 6)
                    names = std::map<std::u16string, int>{{u"x", -1}};
                if (op == "M") {
                    std::cout << dotnet_regex_map_capture_number(std::stoi(utf8(*pattern)), caps) << '\n';
                    continue;
                }
                if (op == "G") {
                    std::optional<std::vector<std::u16string>> list;
                    if (context != 0 && context != 8)
                        list = std::vector<std::u16string>{u"zero", u"one", u"two"};
                    if (context == 8)
                        size = -1;
                    std::cout << hex(std::u16string_view(dotnet_regex_group_name_from_number(
                                     caps, list, size, std::stoi(utf8(*pattern)))))
                              << '\n';
                    continue;
                }
                auto result = dotnet_regex_parse_replacement(
                    pattern, options, caps, size, names, [] { return std::u16string{}; },
                    [] { return true; });
                std::cout << hex(result.pattern) << ":" << (pattern == result.pattern) << ":"
                          << result.has_backreferences << ":";
                for (auto value : result.rules)
                    std::cout << value << ",";
                std::cout << ":";
                for (auto& value : result.strings)
                    std::cout << hex(value) << ",";
                std::cout << '\n';
                continue;
            }
            auto culture = dec(name);
            int reads = 0;
            auto result = dotnet_regex_parse(
                pattern, options,
                [&]() {
                    ++reads;
                    if (!culture)
                        throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                                    "Object reference not set to an instance of an object.");
                    return *culture;
                },
                [] { return true; });
            if (op == "O" || op == "F") {
                DotnetRegexFindOptimizations find(result, [] { return true; });
                auto findOutput = find_data(find);
                if (op == "F") {
                    std::string body;
                    int pos, start;
                    char direction;
                    in >> body >> pos >> start >> direction;
                    auto input = dec(body);
                    bool success =
                        direction == 'r'
                            ? find.try_find_right_to_left(
                                  input ? std::u16string_view(*input) : std::u16string_view{}, pos, start)
                            : find.try_find_left_to_right(
                                  input ? std::u16string_view(*input) : std::u16string_view{}, pos, start);
                    findOutput +=
                        "|" + std::to_string(success) + ":" + std::to_string(pos) + ":" + caches(find);
                }
                std::cout << findOutput << '\n';
                continue;
            }
            if (op == "X") {
                std::cout << prefix_data(result.root) << '\n';
                continue;
            }
            if (op == "W") {
                auto bytecode = dotnet_regex_write(result);
                std::cout << bytecode.options << ":" << bytecode.track_count << ":";
                for (auto code : bytecode.codes)
                    std::cout << code << ",";
                std::cout << ":";
                for (auto& value : bytecode.strings)
                    std::cout << hex(value) << ",";
                std::cout << ":";
                for (std::size_t i = 0; i < bytecode.strings.size(); ++i)
                    std::cout << bool(bytecode.strings_ascii_lookup[i].load()) << ",";
                std::cout << '\n';
                continue;
            }
            std::cout << reads << ":" << result.options << ":" << result.capture_count << ":"
                      << bool(result.backreference_culture) << ":";
            if (result.capture_names) {
                for (auto& s : *result.capture_names)
                    std::cout << hex(std::u16string_view(s)) << ",";
            } else
                std::cout << "-";
            std::cout << ":";
            if (result.capture_name_to_number) {
                for (auto& [key, value] : *result.capture_name_to_number)
                    std::cout << hex(std::u16string_view(key)) << "=" << value << ",";
            } else
                std::cout << "-";
            std::cout << ":";
            if (result.capture_number_sparse_mapping) {
                for (auto& [key, value] : *result.capture_number_sparse_mapping)
                    std::cout << key << "=" << value << ",";
            } else
                std::cout << "-";
            std::cout << ":" << graph(result.root) << '\n';
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
