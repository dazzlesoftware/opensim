// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_service.hpp"
#include "opensim/services/login_completion.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim;
std::string decode(const std::string& value) {
    std::u16string text;
    for (std::size_t i = 0; i < value.size(); i += 4)
        text.push_back(static_cast<char16_t>(std::stoi(value.substr(i, 4), nullptr, 16)));
    return framework::utf8(text);
}
std::string encode(const std::string& value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : framework::utf16(value))
        out << std::setw(4) << static_cast<unsigned>(c);
    return out.str();
}
int main() {
    try {
        services::AccountStore accounts(":memory:", ":memory:", true);
        server::IniConfig config;
        config.set("LoginService", "MinLoginLevel", "9");
        services::LoginService login(accounts, config);
        services::LoginCompletionOptions options;
        options.welcome = "Initial";
        services::LoginCompletion completion({}, options);
        login.set_completion(&completion);
        std::string line;
        while (std::getline(std::cin, line)) {
            std::vector<std::string> words;
            std::size_t start = 0;
            while (true) {
                auto end = line.find('\t', start);
                words.push_back(decode(line.substr(start, end == std::string::npos ? end : end - start)));
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            auto output = login.console_command(words);
            std::cout << login.minimum_login_level() << '|' << encode(completion.welcome_message()) << '|'
                      << (output ? encode(*output) : "-") << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
