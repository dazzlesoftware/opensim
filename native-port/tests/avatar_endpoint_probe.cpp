#include "opensim/server/avatar_endpoint.hpp"
#include "opensim/framework/filesystem.hpp"
#include "osd_fixture_value.hpp"
#include <iostream>
using namespace opensim;
std::string hex(std::string_view s) {
    constexpr char d[] = "0123456789abcdef";
    std::string r;
    for (unsigned char c : s) {
        r += d[c >> 4];
        r += d[c & 15];
    }
    return r;
}
int main(int argc, char** argv) {
    try {
        auto config = server::IniConfig::load(framework::utf8_path(argv[1]));
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.starts_with("name ")) {
                std::cout << hex(network::service_xml_name(fixture::unhex(line.substr(5)))) << '\n';
                continue;
            }
            std::string trace;
            server::AvatarOperations ops;
            auto touch = [&](auto id) {
                if (id.str().back() == '9')
                    throw std::runtime_error("callback");
                return id.str().back() != '8';
            };
            ops.get = [&](auto id) -> std::shared_ptr<services::AvatarData> {
                trace += "get:" + id.str() + ";";
                if (!touch(id))
                    return {};
                auto a = std::make_shared<services::AvatarData>();
                a->avatar_type = 1;
                if (id.is_zero())
                    return a;
                a->data = std::make_shared<services::AvatarData::Fields>();
                (*a->data)["Wearable 0:0"] = "item:asset";
                (*a->data)["_x0041_"] = "literal";
                (*a->data)["9name"] = "<&>\r\n\t";
                (*a->data)["試験"] = std::nullopt;
                (*a->data)["Emoji 🦊"] = "value";
                if (id.str().back() == '1')
                    (*a->data)["controls"] = std::string("\0\x01\x0b\x1f", 4) + "\xef\xbf\xbe\xef\xbf\xbf";
                return a;
            };
            ops.set = [&](auto id, auto& a) {
                trace += "set:" + id.str() + ":" + std::to_string(a.avatar_type) + ":";
                for (const auto& [key, value] : *a.data)
                    trace += hex(key) + "=" + (value ? hex(*value) : "null") + ",";
                trace += ';';
                return touch(id);
            };
            ops.reset = [&](auto id) {
                trace += "reset:" + id.str() + ";";
                return touch(id);
            };
            auto list = [&](const auto& values) {
                for (const auto& s : values)
                    trace += hex(s.value()) + ",";
                trace += ';';
            };
            ops.set_items = [&](auto id, const auto& names, const auto& values) {
                trace += "items:" + id.str() + ":";
                list(names);
                list(values);
                return touch(id) && names.size() == values.size();
            };
            ops.remove_items = [&](auto id, const auto& names) {
                trace += "remove:" + id.str() + ":";
                list(names);
                return touch(id);
            };
            server::AvatarEndpoint endpoint(std::move(ops), config);
            network::HttpRequest request{"POST", "/avatar", fixture::unhex(line), "127.0.0.1", "", {}};
            auto response = endpoint.handle(request);
            std::cout << hex(response.body) << '|' << hex(trace) << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    }
}
