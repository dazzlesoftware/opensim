// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_service.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/identity.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
std::string decode(const std::string& value) {
    std::u16string text;
    for (std::size_t i = 0; i < value.size(); i += 4)
        text.push_back(static_cast<char16_t>(std::stoul(value.substr(i, 4), nullptr, 16)));
    return framework::utf8(text);
}
int main() {
    services::AccountStore accounts(":memory:", ":memory:", true);
    for (std::string line; std::getline(std::cin, line);) {
        try {
            std::vector<std::string> fields;
            std::size_t start = 0;
            for (;;) {
                auto end = line.find('\t', start);
                fields.push_back(line.substr(start, end - start));
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            if (fields.size() != 12)
                throw std::invalid_argument("Expected twelve login access fields");
            server::IniConfig config;
            const char* keys[] = {"DeniedMacs", "DeniedID0s", "AllowedClients", "DeniedClients"};
            for (int i = 0; i < 8; ++i)
                if (fields[i] != "-")
                    config.set(i % 2 ? "LoginService" : "AccessControl", keys[i / 2], decode(fields[i]));
            services::LoginService service(accounts, config);
            services::LoginRequest request;
            request.first = "Absent";
            request.last = "Resident";
            request.scope = framework::ZeroUuid;
            request.mac = decode(fields[8]);
            request.id0 = decode(fields[9]);
            request.version = decode(fields[10]);
            request.channel = decode(fields[11]);
            auto response = service.login(request);
            auto reason = response.get("reason").as_string();
            std::cout << (reason == "presence" ? "blocked" : reason == "key" ? "user" : "error") << '\n';
        } catch (const std::exception& e) {
            std::cout << "EXCEPTION:" << e.what() << '\n';
        }
    }
}
