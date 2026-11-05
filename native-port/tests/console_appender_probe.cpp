// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/console_appender.hpp"
#include <iostream>
#include <vector>
using namespace opensim::framework;
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
std::u16string decode(const std::string& value) {
    std::u16string out;
    for (std::size_t at = 0; at < value.size(); at += 4)
        out += static_cast<char16_t>(std::stoul(value.substr(at, 4), nullptr, 16));
    return out;
}
std::string hex(const std::u16string& value) {
    const char* digits = "0123456789abcdef";
    std::string out;
    for (auto c : value)
        for (int shift : {12, 8, 4, 0})
            out += digits[(c >> shift) & 15];
    return out;
}
int main() {
    std::string row;
    while (std::getline(std::cin, row)) {
        auto f = split(row, '|');
        std::string logs;
        auto log = [&](const std::string& value) {
            if (!logs.empty())
                logs += ';';
            logs += value;
        };
        auto message = decode(f.at(4));
        int writer_mode = std::stoi(f[3]), writes = 0;
        ConsoleAppender appender([&](const auto& text, bool newline) {
            auto value = text;
            if (value.starts_with(u"Couldn't write out log message: ")) {
                auto at = value.find(u'\n');
                if (at != value.npos)
                    value.resize(at);
                if (!value.empty() && value.back() == u'\r')
                    value.pop_back();
            }
            log(std::string(newline ? "line:" : "write:") + hex(value));
            ++writes;
            if (writer_mode == 2 || (writer_mode == 1 && writes == 1) ||
                (writer_mode == 3 && value.starts_with(u"Couldn't write out log message: ")))
                throw ConsoleAppenderFailure(u"write failure");
        });
        auto target = [&](int mode) -> std::shared_ptr<ConsoleAppender::Output> {
            if (mode == 0)
                return {};
            return std::make_shared<ConsoleAppender::Output>([&, mode](const auto& text, const auto& level) {
                log("output:" + hex(text) + ":" + hex(level));
                if (mode == 2)
                    throw ConsoleAppenderFailure(u"output failure");
                if (mode == 3)
                    appender.console({});
            });
        };
        appender.console(target(std::stoi(f[1])));
        std::optional<ConsoleLogEvent> event;
        if (f[0] != "~")
            event = ConsoleLogEvent{f[0] == "#" ? std::optional<std::int32_t>{}
                                                : std::optional<std::int32_t>{std::stoi(f[0])}};
        std::string result = "ok";
        try {
            appender.append(event, [&] {
                log("render");
                int mutation = std::stoi(f[2]);
                if (mutation == 1)
                    throw ConsoleAppenderFailure(u"render failure");
                if (mutation >= 2)
                    appender.console(target(mutation == 2 ? 0 : mutation == 3 ? 1 : 2));
                return message;
            });
        } catch (const ConsoleAppenderFailure& error) {
            result = "err:" + hex(error.source_text);
        } catch (const std::exception&) {
            result = "err:unexpected";
        }
        std::cout << result << "|" << (appender.console() ? "1" : "0") << "|" << logs << "\n";
    }
}
