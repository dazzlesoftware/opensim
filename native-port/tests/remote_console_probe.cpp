// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/remote_console.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim;
using namespace framework;
using namespace server;
std::vector<std::string> split(const std::string& value, char delimiter) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (true) {
        auto end = value.find(delimiter, start);
        out.push_back(value.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}
std::u16string decode(const std::string& value) {
    std::u16string text;
    for (std::size_t i = 0; i < value.size(); i += 4)
        text.push_back(static_cast<char16_t>(std::stoi(value.substr(i, 4), nullptr, 16)));
    return text;
}
std::string hex(std::u16string_view value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : value)
        out << std::setw(4) << static_cast<unsigned>(c);
    return out.str();
}
int main() {
    std::string row;
    while (std::getline(std::cin, row)) {
        std::vector<std::u16string> logs;
        std::vector<Uuid> ids;
        std::vector<std::shared_ptr<services::CapabilityPollHandler>> polls;
        std::int32_t ticks = 0;
        auto commands = std::make_shared<ConsoleCommandResolver>();
        std::unique_ptr<RemoteConsole> console;
        auto callback = std::make_shared<ConsoleCommandResolver::Callback::element_type>(
            [&](const auto& module, auto& words) {
                std::u16string text = u"cmd:" + utf16(module.value_or(""));
                for (const auto& word : words)
                    text += u"|" + utf16(word);
                logs.push_back(text);
                if (words[0] == "echo") {
                    std::u16string echo = u"ECHO:";
                    for (std::size_t i = 1; i < words.size(); ++i) {
                        if (i > 1)
                            echo += u' ';
                        echo += utf16(words[i]);
                    }
                    console->output(echo);
                }
            });
        commands->add({"Help", "help", "help", "Help", "", false}, callback);
        commands->add({"General", "quit", "quit", "Quit", "", false}, callback);
        commands->add({"Test", "echo", "echo <text>", "Echo", "", false}, callback);
        RemoteConsoleOptions options;
        options.commands = commands;
        options.prompt = u"Region";
        options.user = u"user";
        options.password = u"pass";
        options.expiration_timer = false;
        options.tick_count = [&] { return ticks; };
        options.add_poll = [&](auto handler) {
            ids.push_back(handler->id);
            polls.push_back(handler);
            logs.push_back(u"add:" + utf16(handler->url.value_or("")) + u":" +
                           utf16(std::to_string(handler->timeout_ms)) + u":" +
                           (handler->request ? u"1" : u"0") + u":" + (handler->drop ? u"1" : u"0"));
        };
        options.remove_poll = [&](const auto& method, const auto& path) {
            logs.push_back(u"remove:" + utf16(method) + u":" + utf16(path));
        };
        options.on_output = [&](const auto& text) { logs.push_back(u"out:" + text); };
        options.debug = [&](const auto& text) { logs.push_back(u"debug:" + text); };
        console = std::make_unique<RemoteConsole>(options);
        auto normalize = [&](std::u16string text) {
            for (std::size_t i = 0; i < ids.size(); ++i) {
                auto id = utf16(ids[i].str());
                auto replacement = u"@" + utf16(std::to_string(i));
                for (std::size_t at = 0; (at = text.find(id, at)) != std::u16string::npos;
                     at += replacement.size())
                    text.replace(at, id.size(), replacement);
            }
            return text;
        };
        auto expand = [&](std::u16string text) {
            for (int i = static_cast<int>(ids.size()) - 1; i >= 0; --i) {
                auto key = u"@" + utf16(std::to_string(i)), id = utf16(ids[i].str());
                for (std::size_t at = 0; (at = text.find(key, at)) != std::u16string::npos; at += id.size())
                    text.replace(at, key.size(), id);
            }
            return text;
        };
        auto selected = [&](const std::string& value) {
            auto i = std::stoi(value);
            return i >= 0 && static_cast<std::size_t>(i) < ids.size() ? ids[i] : Uuid{};
        };
        auto reply = [&](RemoteConsoleReply value) {
            return std::to_string(value.status) + "," + hex(value.content_type) + "," +
                   (value.keep_alive ? (*value.keep_alive ? "1" : "0") : "-") + "," +
                   (value.allowed_origin ? hex(*value.allowed_origin) : "-") + "," +
                   hex(normalize(value.body));
        };
        bool first = true;
        for (const auto& action : split(row, ';')) {
            auto fields = split(action, ',');
            logs.clear();
            std::string result;
            try {
                const auto op = fields[0];
                if (op == "S")
                    result = reply(console->start_session(expand(decode(fields[1]))));
                else if (op == "C")
                    result = reply(console->close_session(expand(decode(fields[1]))));
                else if (op == "I")
                    result = reply(console->session_command(expand(decode(fields[1]))));
                else if (op == "O") {
                    int flags = std::stoi(fields[3]);
                    console->output(fields[1] == "-" ? std::optional<std::u16string>{} : decode(fields[1]),
                                    fields[2] == "-" ? std::optional<std::u16string>{} : decode(fields[2]),
                                    flags & 1, flags & 2, flags & 4);
                    result = "O";
                } else if (op == "T") {
                    ticks = std::stoi(fields[1]);
                    result = "T";
                } else if (op == "H")
                    result = console->has_events(selected(fields[1])) ? "1" : "0";
                else if (op == "G")
                    result = reply(console->get_events(selected(fields[1])));
                else if (op == "N")
                    result = reply(console->no_events());
                else if (op == "E") {
                    console->expire();
                    result = "E";
                } else if (op == "Z") {
                    console->close_connection(selected(fields[1]));
                    result = "Z";
                } else if (op == "A") {
                    IniConfig config;
                    config.set("Network", "ConsoleUser", utf8(decode(fields[1])));
                    config.set("Network", "ConsolePass", utf8(decode(fields[2])));
                    config.set("Network", "ConsoleAllowedOrigin", utf8(decode(fields[3])));
                    console->read_config(config);
                    result = "A";
                } else if (op == "M") {
                    console->read_config(IniConfig{});
                    result = "M";
                } else if (op == "L") {
                    result = console->state().input_count
                                 ? "L:" + hex(console->read_line(decode(fields[1]), std::stoi(fields[2]) != 0,
                                                                 std::stoi(fields[3]) != 0))
                                 : "WAIT";
                } else if (op == "W") {
                    for (int i = 0; i < std::stoi(fields[1]); ++i)
                        console->output(u"line " + utf16(std::to_string(i)));
                    result = "W";
                } else if (op == "D") {
                    result = "D";
                    for (const auto& [key, value] : RemoteConsole::decode_post(decode(fields[1])))
                        result += "," + hex(key) + ":" + hex(value);
                } else
                    throw std::runtime_error("Unknown action");
            } catch (const std::exception&) {
                result = "EX";
            }
            auto state = console->state();
            if (!first)
                std::cout << '~';
            first = false;
            std::cout << result << "!" << state.line_number << "," << state.scrollback_count << ","
                      << state.input_count << "," << state.connection_count << "," << state.expecting_input
                      << "," << state.expecting_command << "," << hex(state.last_prompt);
            for (const auto& log : logs)
                std::cout << "!" << hex(normalize(log));
        }
        std::cout << '\n';
    }
}
