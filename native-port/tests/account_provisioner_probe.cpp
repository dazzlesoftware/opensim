#include "opensim/services/account_provisioner.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using Id = framework::Uuid;
std::string hex(std::string_view s) {
    constexpr char d[] = "0123456789abcdef";
    std::string r;
    for (unsigned char c : s) {
        r += d[c >> 4];
        r += d[c & 15];
    }
    return r;
}
std::string nullable(const std::optional<std::string>& s) {
    return s.value_or("_");
}
std::string snapshot(const services::UserAccount& a) {
    std::string s = a.principal_id + ":" + a.scope_id + ":" + nullable(a.first_name) + ":" +
                    nullable(a.last_name) + ":" + nullable(a.email) + ":" + std::to_string(a.created) + ":";
    for (const auto& [k, v] : *a.service_urls)
        s += k + "=" + framework::service_url_object_text(v) + ",";
    return s;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        int mask, fault, variant;
        input >> mask >> fault >> variant;
        std::string trace;
        Id seven("00000000-0000-0000-0000-000000000007"), eight("00000000-0000-0000-0000-000000000008"),
            five("00000000-0000-0000-0000-000000000005");
        services::AccountProvisioning ops;
        ops.by_name = [&](Id scope, std::string first, std::string last) {
            trace += "lookup:" + scope.str() + ":" + first + ":" + last + ";";
            if (fault == 1)
                throw std::runtime_error("lookup");
            return fault == 16 ? std::make_shared<services::UserAccount>()
                               : std::shared_ptr<services::UserAccount>{};
        };
        ops.store = [&](auto& a) {
            trace += "store:" + snapshot(a) + ";";
            if (fault == 3)
                throw std::runtime_error("store");
            if (fault == 17) {
                a.principal_id = eight.str();
                a.first_name = "Changed";
            }
            return fault != 2;
        };
        if (mask & 1)
            ops.set_password = [&](Id id, auto password) {
                trace += "password:" + id.str() + ":" + nullable(password) + ";";
                if (fault == 5)
                    throw std::runtime_error("password");
                return fault != 4;
            };
        if (mask & 2)
            ops.default_regions = [&](Id scope) {
                trace += "grid:" + scope.str() + ";";
                if (fault == 7)
                    throw std::runtime_error("grid");
                std::vector<std::optional<services::GridRegion>> r;
                if (fault == 6)
                    return r;
                if (fault == 8) {
                    r.push_back({});
                    return r;
                }
                services::GridRegion home;
                home.id = five;
                r.push_back(home);
                r.push_back({});
                return r;
            };
        if (mask & 4)
            ops.set_home = [&](auto id, Id region, auto position, auto look) {
                trace += "home:" + id + ":" + region.str() + ":" +
                         std::to_string(static_cast<int>(position.x)) + "," +
                         std::to_string(static_cast<int>(position.y)) + "," +
                         std::to_string(static_cast<int>(position.z)) + ":" +
                         std::to_string(static_cast<int>(look.x)) + "," +
                         std::to_string(static_cast<int>(look.y)) + "," +
                         std::to_string(static_cast<int>(look.z)) + ";";
                if (fault == 11)
                    throw std::runtime_error("home");
                return fault != 10;
            };
        if (mask & 8)
            ops.create_inventory = [&](Id id) {
                trace += "inventory:" + id.str() + ";";
                if (fault == 13)
                    throw std::runtime_error("inventory");
                return fault != 12;
            };
        ops.default_appearance = [&](Id id) {
            trace += "default:" + id.str() + ";";
            if (fault == 14)
                throw std::runtime_error("appearance");
        };
        ops.model_appearance = [&](Id id, auto model) {
            trace += "model:" + id.str() + ":" + model + ";";
            if (fault == 15)
                throw std::runtime_error("model");
        };
        services::AccountProvisioner provisioner(std::move(ops), mask & 16, [] { return 123; });
        std::optional<std::string> first = " First ", last = " Last ", password = "secret", email = "mail",
                                   model = (mask & 32) ? "Model Avatar" : "";
        if (variant == 1)
            first = std::nullopt;
        if (variant == 2)
            last = std::nullopt;
        if (variant == 3)
            model = std::nullopt;
        if (variant == 4) {
            first = "\xc2\xa0試験\xe2\x80\x83";
            last = "";
        }
        if (variant == 5)
            email = std::nullopt;
        if (variant == 6)
            password = std::nullopt;
        std::string result = "null";
        try {
            auto account = provisioner.create(five, seven, first, last, password, email, model);
            if (account)
                result = snapshot(*account);
        } catch (...) {
            result = "EX";
        }
        std::cout << hex(result) << '|' << hex(trace) << '\n';
    }
}
