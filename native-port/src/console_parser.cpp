// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_parser.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
namespace opensim::framework {
namespace {
bool option(std::u16string_view word) {
    if (word.size() < 4 || word[0] != u'-' || word[1] != u'-' || word.back() != u'=')
        return false;
    for (std::size_t i = 2; i + 1 < word.size(); ++i) {
        auto c = word[i];
        if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'-'))
            return false;
    }
    return true;
}
}
std::vector<std::u16string> parse_console_command(std::optional<std::u16string_view> text) {
    if (!text)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    std::vector<std::u16string_view> segments;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text->size(); ++i)
        if ((*text)[i] == u'"') {
            segments.push_back(text->substr(start, i - start));
            start = i + 1;
        }
    segments.push_back(text->substr(start));
    std::vector<std::u16string> result;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index % 2) {
            result.emplace_back(segments[index]);
            continue;
        }
        auto segment = segments[index];
        bool is_option = false;
        for (std::size_t i = 0; i < segment.size();) {
            while (i < segment.size() && unicode_whitespace(segment[i]))
                ++i;
            auto first = i;
            while (i < segment.size() && !unicode_whitespace(segment[i]))
                ++i;
            if (first != i) {
                auto word = segment.substr(first, i - first);
                is_option = option(word);
                result.emplace_back(word);
            }
        }
        if (is_option && index + 1 < segments.size()) {
            result.back().push_back(u'"');
            result.back().append(segments[index + 1]);
            result.back().push_back(u'"');
            ++index;
        }
    }
    return result;
}
std::vector<std::string> parse_console_command_utf8(std::string_view text) {
    auto units = utf16_replacing(text);
    auto parsed = parse_console_command(std::u16string_view(units));
    std::vector<std::string> result;
    result.reserve(parsed.size());
    for (const auto &value : parsed)
        result.push_back(utf8(value));
    return result;
}
}
