// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_command_resolver.hpp"
#include "opensim/framework/console_parser.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
std::string encode(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : utf16(value))
        out << std::setw(4) << static_cast<unsigned>(c);
    return out.str();
}
std::string list(const std::vector<std::string>& values) {
    std::string text;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i)
            text += ',';
        text += encode(values[i]);
    }
    return text;
}
int main() {
    std::string row;
    while (std::getline(std::cin, row)) {
        auto split = row.find('\t');
        int seed = std::stoi(row.substr(0, split));
        auto encoded = row.substr(split + 1);
        std::u16string units;
        for (std::size_t i = 0; i < encoded.size(); i += 4)
            units.push_back(static_cast<char16_t>(std::stoi(encoded.substr(i, 4), nullptr, 16)));
        auto query = utf8(units);
        ConsoleCommandResolver commands;
        std::vector<std::string> events;
        auto metadata = [](std::string module, bool shared) {
            return ConsoleCommandDescription{module, "run", "run", "description", "", shared};
        };
        ConsoleCommandResolver::Callback a, b, c;
        c = std::make_shared<
            std::function<void(const std::optional<std::string>&, std::vector<std::string>&)>>(
            [&](const auto& module, auto&) { events.push_back("C:" + module.value_or("")); });
        a = std::make_shared<
            std::function<void(const std::optional<std::string>&, std::vector<std::string>&)>>(
            [&](const auto& module, auto& words) {
                events.push_back("A:" + module.value_or(""));
                if (seed & 32)
                    words[0] = "changed";
                if (seed & 64)
                    throw std::runtime_error("callback failure");
                if (seed & 128)
                    commands.add(metadata("New", false), c);
                if (seed & 256)
                    commands.add(metadata("New", false), a);
                if (seed & 512)
                    commands.add({"Other", "other", "other", "", "", false}, c);
            });
        b = std::make_shared<
            std::function<void(const std::optional<std::string>&, std::vector<std::string>&)>>(
            [&](const auto& module, auto& words) {
                events.push_back("B:" + module.value_or(""));
                if ((seed & 1024) && words.size() > 1)
                    words[1] = "tail";
                if (seed & 2048)
                    throw std::runtime_error("callback failure");
            });
        auto first = (seed & 2) ? ConsoleCommandResolver::Callback{} : a;
        commands.add(metadata("Original é", seed & 1), first);
        if (seed & 4)
            commands.add(metadata("Duplicate", false), first);
        if (seed & 8)
            commands.add(metadata("New", false), b);
        if (seed & 16)
            commands.add(metadata("New", false), {});
        if (seed & 4096)
            commands.add({"Other", "rush", "rush", "", "", false}, c);
        for (int iteration = 0; iteration < 2; ++iteration) {
            auto words = parse_console_command_utf8(query);
            std::vector<std::string> result;
            bool has = false, aliased = false;
            std::string status = "OK";
            try {
                has = commands.has_command(query);
                const auto invoked = commands.invoke(words);
                aliased = invoked && &invoked->get() == &words;
                if (invoked)
                    result = invoked->get();
            } catch (const std::exception&) {
                status = "EX";
            }
            std::cout << status << ';' << has << ';' << aliased << ';' << list(result) << ';' << list(words)
                      << ';' << list(events) << (iteration == 0 ? '/' : '\n');
        }
    }
}
