#include "opensim/services/user_account.hpp"
#include "osd_fixture_value.hpp"
#include <iostream>
using namespace opensim;
using services::UserAccount;
std::string hx(const std::string& s) {
    constexpr char d[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) {
        out += d[c >> 4];
        out += d[c & 15];
    }
    return out;
}
std::string str(const std::optional<std::string>& v) {
    return v ? "s" + hx(*v) : "null";
}
std::any object(const std::string& kind, const std::string& value) {
    if (kind == "string")
        return value == "." ? std::string{} : fixture::unhex(value);
    if (kind == "int")
        return std::stoi(value);
    if (kind == "long")
        return static_cast<std::int64_t>(std::stoll(value));
    if (kind == "bool")
        return value == "1";
    if (kind == "real")
        return std::bit_cast<double>(std::stoull(value, nullptr, 16));
    if (kind == "uuid")
        return framework::Uuid("00000000-0000-0000-0000-000000000007");
    return {};
}
std::string snapshot(UserAccount& a) {
    std::string s = a.principal_id + ":" + a.scope_id + ":" + str(a.first_name) + ":" + str(a.last_name) +
                    ":" + str(a.email) + ":" + str(a.user_title) + ":" + str(a.user_country) + ":" +
                    std::to_string(a.user_level) + ":" + std::to_string(a.user_flags) + ":" +
                    std::to_string(a.created) + ":" + (a.local_to_grid ? "1" : "0") + ":" + hx(a.name()) +
                    ":";
    if (!a.service_urls)
        return s + "null";
    for (auto& [key, value] : *a.service_urls)
        s += hx(key) + "=" +
             (value.has_value() ? "s" + hx(framework::service_url_object_text(value)) : "null") + ";";
    return s;
}
int main() {
    UserAccount a;
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op, key, kind, value, status = "OK", out = "-";
        in >> op;
        try {
            if (op == "default")
                a = UserAccount{};
            else if (op == "principal")
                a = UserAccount(framework::Uuid("00000000-0000-0000-0000-000000000007"));
            else if (op == "init" || op == "random") {
                auto scope = framework::Uuid("00000000-0000-0000-0000-000000000008");
                a = op == "init" ? UserAccount(scope, framework::Uuid("00000000-0000-0000-0000-000000000007"),
                                               "First", "Last", std::nullopt, [] { return 123; })
                                 : UserAccount(scope, "First", "Last", std::nullopt, [] { return 123; });
                if (op == "random") {
                    framework::Uuid id(a.principal_id);
                    out = ((id.bytes()[6] >> 4) == 4 && (id.bytes()[8] & 192) == 128) ? "1" : "0";
                    a.principal_id = "00000000-0000-0000-0000-000000000007";
                }
            } else if (op == "set" || op == "all") {
                if (op == "set")
                    in >> key;
                in >> kind >> value;
                auto o = object(kind, value);
                services::AccountValues values;
                if (op == "set")
                    values[key] = o;
                else
                    for (auto name :
                         {"FirstName", "LastName", "Email", "PrincipalID", "ScopeID", "UserLevel",
                          "UserFlags", "UserTitle", "UserCountry", "LocalToGrid", "Created", "ServiceURLs"})
                        values[name] = o;
                a = UserAccount(values);
            } else if (op == "export") {
                auto pairs = a.to_key_value_pairs();
                out = "";
                for (auto& [k, v] : *pairs)
                    out += k + "=" +
                           (v.has_value() ? "s" + hx(framework::service_url_object_text(v)) : "null") + ";";
            } else if (op == "objects") {
                a.service_urls = std::make_shared<framework::AgentCircuitData::ServiceUrls>();
                (*a.service_urls)["null"] = std::any{};
                (*a.service_urls)["integer"] = 7;
                (*a.service_urls)["star;key"] = std::string("value*;");
            }
        } catch (const std::exception&) {
            status = "EX";
        }
        std::cout << status << '|' << out << '|' << snapshot(a) << '\n';
    }
}
