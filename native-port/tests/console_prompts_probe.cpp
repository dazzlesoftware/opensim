// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_prompts.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
using Text = ConsolePrompts::Text;
std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> out;
    std::size_t at = 0;
    while (true) {
        auto end = value.find(delimiter, at);
        out.push_back(value.substr(at, end == value.npos ? end : end - at));
        if (end == value.npos)
            return out;
        at = end + 1;
    }
}
Text decode(const std::string& value) {
    if (value == "~")
        return {};
    std::u16string out;
    for (std::size_t at = 0; at < value.size(); at += 4)
        out += static_cast<char16_t>(std::stoul(value.substr(at, 4), nullptr, 16));
    return out;
}
std::string hex(const Text& value) {
    if (!value)
        return "~";
    const char* digits = "0123456789abcdef";
    std::string out;
    for (auto c : *value)
        for (int shift : {12, 8, 4, 0})
            out += digits[(c >> shift) & 15];
    return out;
}
std::vector<Text> list(const std::string& value) {
    std::vector<Text> out;
    if (value != "!")
        for (const auto& item : split(value.substr(1), ','))
            out.push_back(decode(item));
    return out;
}
struct Limit {};
int main() {
    std::string row;
    while (std::getline(std::cin, row)) {
        auto f = split(row, '|');
        auto input = list(f.at(6));
        std::size_t at = 0;
        std::string logs;
        auto log = [&](const std::string& value) {
            if (!logs.empty())
                logs += ';';
            logs += value;
        };
        ConsolePrompts::Excluded excluded;
        if (f[3] != "~") {
            auto value = decode(f[3]);
            excluded = std::vector<char16_t>(value->begin(), value->end());
        }
        ConsolePrompts::Options options;
        if (f[4] != "~")
            options = list(f[4]);
        auto mutate = [&](const std::string& action) {
            if (action.empty())
                return;
            auto parts = split(action, ':');
            log("mutation:" + action);
            auto op = parts[0];
            if (op == "cx")
                excluded = std::vector<char16_t>{u'Z'};
            else if (op == "ox")
                options = std::vector<Text>{u"yes"};
            else if (op == "ca")
                excluded->add(decode(parts.at(1))->at(0));
            else if (op == "cc")
                excluded->clear();
            else if (op == "cs")
                excluded->set(std::stoul(parts.at(1)), decode(parts.at(2))->at(0));
            else if (op == "cr")
                excluded->remove_at(std::stoul(parts.at(1)));
            else if (op == "oa")
                options->add(decode(parts.at(1)));
            else if (op == "oc")
                options->clear();
            else if (op == "os")
                options->set(std::stoul(parts.at(1)), decode(parts.at(2)));
            else if (op == "or")
                options->remove_at(std::stoul(parts.at(1)));
        };
        bool read_mutated = false, diag_mutated = false;
        ConsolePrompts prompts(
            [&](const auto& text, bool command, bool echo) -> Text {
                if (at >= 32)
                    throw Limit();
                log("read:" + hex(text) + ":" + (command ? "1" : "0") + ":" + (echo ? "1" : "0"));
                if (!read_mutated && f.size() > 7) {
                    read_mutated = true;
                    mutate(f[7]);
                }
                return at < input.size() ? input[at++] : (++at, Text{});
            },
            [&](const auto& text) {
                log("diag:" + hex(text));
                if (!diag_mutated && f.size() > 8) {
                    diag_mutated = true;
                    mutate(f[8]);
                }
            });
        std::string result;
        try {
            Text value;
            switch (f[0][0]) {
            case 'P':
                value = prompts.prompt(decode(f[1]));
                break;
            case 'D':
                value = prompts.prompt_default(decode(f[1]), decode(f[2]));
                break;
            case 'X':
                value = prompts.prompt_excluding(decode(f[1]), excluded);
                break;
            case 'E':
                value = prompts.prompt_default_excluding(decode(f[1]), decode(f[2]), excluded, f[5] == "1");
                break;
            case 'O':
                value = prompts.prompt_options(decode(f[1]), decode(f[2]), options);
                break;
            }
            result = "ok:" + hex(value);
        } catch (const ConsolePromptListModifiedError&) {
            result = "err:InvalidOperationException";
        } catch (const ConsolePromptNullError&) {
            result = "err:NullReferenceException";
        } catch (const Limit&) {
            result = "err:Limit";
        }
        std::cout << result << "|" << logs << "\n";
    }
}
