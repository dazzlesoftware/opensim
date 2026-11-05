// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/console_parser.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
std::string hex(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : utf16(value))
        out << std::setw(4) << static_cast<unsigned>(c);
    return out.str();
}
int main(int argc, char**) {
    ConsoleCommandResolver resolver;
    std::size_t registration = 0;
    const std::vector<std::string> modules = {"General", "Users", "Assets", "café", "CAFÉ", "İ", "I"};
    for (const auto* command : {"login level", "login reset", "login text", "show", "show users", "show user",
                                "shutdown", "quit", "fcache clear", "fcache clearnegatives", "fcache status",
                                "café test", "café other", "x-y", "x-z", "soft­hyphen tail"}) {
        auto i = registration++;
        resolver.add(ConsoleCommandDescription{
            modules[i % modules.size()], command, std::string(command) + " <value>",
            "Description " + std::to_string(i), i % 3 == 0 ? "Details\nLine" : "", i % 2 == 0});
    }
    if (argc > 1)
        for (int extra = 0; extra < 40; ++extra) {
            auto i = registration++;
            auto command = "extra " + std::to_string(extra);
            resolver.add(ConsoleCommandDescription{modules[i % modules.size()], command, command + " <value>",
                                                   "Description " + std::to_string(i),
                                                   i % 3 == 0 ? "Details\nLine" : "", i % 2 == 0});
        }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string mode;
        auto separator = line.find('\t');
        if (separator != std::string::npos) {
            mode = line.substr(0, separator);
            line.erase(0, separator + 1);
        }
        std::u16string text;
        for (std::size_t i = 0; i < line.size(); i += 4)
            text.push_back(static_cast<char16_t>(std::stoi(line.substr(i, 4), nullptr, 16)));
        auto words = parse_console_command_utf8(utf8(text));
        std::string status;
        if (!mode.empty()) {
            try {
                std::vector<std::optional<std::string>> values;
                if (mode == "H")
                    values = resolver.help(words);
                else
                    for (const auto& value : resolver.next_options(words, mode == "N1"))
                        values.emplace_back(value);
                std::cout << "L";
                for (const auto& value : values)
                    std::cout << '|' << (value ? hex(*value) : "-");
                std::cout << '\n';
            } catch (const std::exception&) {
                std::cout << "EX\n";
            }
            continue;
        }

        try {
            status = resolver.resolve(words) ? "1" : "0";
        } catch (const std::exception&) {
            status = "EX";
        }
        std::cout << status;
        for (const auto& word : words)
            std::cout << '|' << hex(word);
        std::cout << '\n';
    }
}
