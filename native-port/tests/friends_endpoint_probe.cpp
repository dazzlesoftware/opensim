#include "opensim/server/friends_endpoint.hpp"
#include <iostream>
using namespace opensim;
using namespace services;
using namespace server;
std::string hex(const std::string& s) {
    std::string out;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string unhex(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::string trace;
        FriendsOperations operations;
        auto get = [&](framework::Uuid id) -> FriendsOperations::Friends {
            if (id.str().ends_with('9'))
                throw std::runtime_error("fixture");
            if (id.str().ends_with('8'))
                return {};
            if (id.is_zero())
                return std::vector<std::shared_ptr<FriendInfo>>{};
            if (id.str().ends_with('1'))
                return std::vector<std::shared_ptr<FriendInfo>>{nullptr};
            auto a = std::make_shared<FriendInfo>();
            a->principal = id;
            a->friend_id = "friend<&>\r\n\t";
            a->my_flags = 7;
            a->their_flags = 5;
            auto b = std::make_shared<FriendInfo>();
            b->my_flags = -2147483647;
            b->their_flags = 2147483647;
            return std::vector<std::shared_ptr<FriendInfo>>{a, b};
        };
        operations.get = [&](auto id) {
            trace += "get:" + id.str() + ";";
            return get(id);
        };
        operations.get_string = [&](const std::string& id) {
            trace += "string:" + hex(id) + ";";
            framework::Uuid uuid;
            framework::try_parse_sdk_uuid(id, uuid);
            return get(uuid);
        };
        auto okay = [](const std::string& id) {
            if (id.ends_with('9'))
                throw std::runtime_error("fixture");
            return !id.ends_with('8');
        };
        operations.store = [&](const std::string& id, const std::string& f, std::int32_t flags) {
            trace += "store:" + hex(id) + ":" + hex(f) + ":" + std::to_string(flags) + ";";
            return okay(id);
        };
        operations.erase = [&](auto id, const std::string& f) {
            trace += "delete:" + id.str() + ":" + hex(f) + ";";
            return okay(id.str());
        };
        operations.erase_string = [&](const std::string& id, const std::string& f) {
            trace += "delete-string:" + hex(id) + ":" + hex(f) + ";";
            return okay(id);
        };
        server::IniConfig config;
        FriendsEndpoint endpoint(std::move(operations), config);
        network::HttpRequest request;
        request.method = "POST";
        request.path = "/friends";
        request.body = unhex(line);
        auto response = endpoint.handle(request);
        std::cout << hex(response.body) << "|" << hex(trace) << "\n";
    }
}
