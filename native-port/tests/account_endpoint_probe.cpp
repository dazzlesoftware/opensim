#include "opensim/server/account_endpoint.hpp"
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
services::UserAccount account(const framework::Uuid& id) {
    services::UserAccount a(id);
    a.first_name = "First";
    a.last_name = "Last";
    a.created = 123;
    a.user_level = 7;
    a.user_flags = 9;
    a.service_urls = std::make_shared<framework::AgentCircuitData::ServiceUrls>();
    (*a.service_urls)["url"] = std::string("http://host/a;b*c");
    return a;
}
int main(int argc, char** argv) {
    try {
        auto config = server::IniConfig::load(framework::utf8_path(argv[1]));
        std::string line;
        while (std::getline(std::cin, line)) {
            std::string trace;
            const framework::Uuid seven("00000000-0000-0000-0000-000000000007");
            server::AccountOperations ops;
            ops.by_id = [&](auto scope, auto id) -> server::AccountOperations::Account {
                trace += "id:" + scope.str() + ":" + id.str() + ";";
                if (id.str().back() == '9')
                    throw std::runtime_error("lookup");
                return id == seven ? server::AccountOperations::Account(account(id)) : std::nullopt;
            };
            ops.by_email = [&](auto scope, auto email) -> server::AccountOperations::Account {
                trace += "email:" + scope.str() + ":" + email + ";";
                return email == "mail" ? server::AccountOperations::Account(account(seven)) : std::nullopt;
            };
            ops.by_name = [&](auto scope, auto first, auto last) -> server::AccountOperations::Account {
                trace += "name:" + scope.str() + ":" + first + ":" + last + ";";
                return first == "First" && last == "Last" ? server::AccountOperations::Account(account(seven))
                                                          : std::nullopt;
            };
            ops.search = [&](auto scope, auto query) {
                trace += "search:" + scope.str() + ":" + query + ";";
                server::AccountOperations::Accounts r;
                if (query == "throw")
                    throw std::runtime_error("search");
                if (query == "empty")
                    return r;
                if (query == "nullitem")
                    r.push_back(std::nullopt);
                r.push_back(account(seven));
                if (query == "two")
                    r.push_back(account(framework::Uuid("00000000-0000-0000-0000-000000000008")));
                return r;
            };
            ops.multi = [&](auto scope, auto ids) {
                trace += "multi:" + scope.str() + ":";
                server::AccountOperations::Accounts r;
                for (const auto& raw : ids) {
                    trace += raw + ",";
                    framework::Uuid id;
                    framework::try_parse_sdk_uuid(raw, id);
                    r.push_back(id == seven ? server::AccountOperations::Account(account(seven))
                                            : std::nullopt);
                }
                trace += ';';
                return r;
            };
            ops.store = [&](auto& a) {
                auto nullable = [](const auto& s) { return s.value_or("_"); };
                trace += "store:" + nullable(a.first_name) + ":" + nullable(a.last_name) + ":" +
                         nullable(a.email) + ":" + std::to_string(a.created) + ":" +
                         std::to_string(a.user_level) + ":" + std::to_string(a.user_flags) + ":" +
                         nullable(a.user_title) + ";";
                if (a.user_level == -98)
                    throw std::runtime_error("store");
                return a.user_level != -99;
            };
            ops.create = [&](auto scope, auto id, auto first, auto last, auto password, auto email,
                             auto model) -> server::AccountOperations::Account {
                trace += "create:" + scope.str() + ":" + id.str() + ":" + first + ":" + last + ":" +
                         password + ":" + email + ":" + model + ";";
                if (password == "throw")
                    throw std::runtime_error("create");
                if (password == "fail")
                    return {};
                return services::UserAccount(scope, id, first, last, email, [] { return 123; });
            };
            server::AccountEndpoint endpoint(std::move(ops), config);
            network::HttpRequest request{"POST", "/accounts", fixture::unhex(line), "127.0.0.1", "", {}};
            auto response = endpoint.handle(request);
            std::cout << hex(response.body) << '|' << hex(trace) << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    }
}
