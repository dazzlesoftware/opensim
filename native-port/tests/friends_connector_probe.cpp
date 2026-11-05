#include "opensim/services/friends_connector.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace services;
std::string hex(const std::optional<std::string>& s) {
    if (!s)
        return "-";
    const char* h = "0123456789abcdef";
    std::string out;
    for (unsigned char c : *s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out.empty() ? "." : out;
}
std::optional<std::string> decode(std::string token) {
    if (token == "-")
        return {};
    if (token == ".")
        return std::string{};
    std::string out;
    for (std::size_t i = 0; i < token.size(); i += 2)
        out += static_cast<char>(std::stoi(token.substr(i, 2), nullptr, 16));
    return out;
}
int main(int argc, char** argv) {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode;
        std::string p, f, r;
        in >> mode >> p >> f >> r;
        auto principal = decode(p), friend_id = decode(f), reply = decode(r);
        std::string trace, out;
        try {
            FriendsConnector::Request request = [&](const std::string& uri, const std::string& body) {
                trace += hex(uri) + ":" + hex(body) + ";";
                if (reply == std::optional<std::string>("#throw"))
                    throw std::runtime_error("transport");
                return reply.value_or("");
            };
            std::unique_ptr<FriendsConnector> connector;
            if (argc > 2 && std::string(argv[1]) == "--network")
                connector = std::make_unique<FriendsConnector>(server::IniConfig::load(argv[2]));
            else if (argc > 1)
                connector = std::make_unique<FriendsConnector>(server::IniConfig::load(argv[1]), request);
            else
                connector = std::make_unique<FriendsConnector>(std::string("http://friends.invalid/base///"),
                                                               request);
            framework::Uuid id;
            if (principal)
                framework::try_parse_sdk_uuid(*principal, id);
            if (mode < 2) {
                auto rows = mode ? connector->get(principal) : connector->get(id);
                out = std::to_string(rows.size());
                for (const auto& row : rows)
                    out += "|" + row.principal.str() + ":" + hex(row.friend_id) + ":" +
                           std::to_string(row.my_flags) + ":" + std::to_string(row.their_flags);
            } else
                out = (mode == 2   ? connector->store(principal, friend_id, -2147483647)
                       : mode == 3 ? connector->erase(id, friend_id)
                                   : connector->erase(principal, friend_id))
                          ? "T"
                          : "F";
        } catch (const std::exception&) {
            out = "E";
        }
        std::cout << trace << "|" << out << "\n";
    }
}
