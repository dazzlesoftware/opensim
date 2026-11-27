// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/remote_accounts.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;

int main() {
    try {
        const std::string zero = "00000000-0000-0000-0000-000000000000";
        const std::string id = "44444444-4444-4444-8444-444444444444";
        const std::string record =
            "<PrincipalID>" + id + "</PrincipalID><ScopeID>" + zero +
            "</ScopeID><FirstName>Remote</FirstName><LastName>Resident</LastName>"
            "<Email>remote@example.invalid</Email><Created>123</Created><UserLevel>7</UserLevel>"
            "<UserFlags>4</UserFlags><UserTitle>Grid title</UserTitle><LocalToGrid>True</LocalToGrid>"
            "<ServiceURLs>InventoryServerURI*http://inventory.example/;</ServiceURLs>";
        auto require = [](bool ok, const char *message) {
            if (!ok)
                throw std::runtime_error(message);
        };
        server::IniConfig config;
        config.set("UserAccountService", "UserAccountServerURI", "http://127.0.0.1:9000/");
        std::vector<network::Form> calls;
        bool scalar_store = false;
        auto request = [&](const std::string &uri, const std::string &body) {
            require(uri == "http://127.0.0.1:9000/accounts", "account endpoint normalization");
            auto fields = network::parse_service_form(body);
            calls.push_back(fields);
            const auto text = [&](const char *name) { return std::get<std::string>(fields.at(name)); };
            require(text("VERSIONMIN") == "1" && text("VERSIONMAX") == "1", "original protocol versions");
            if (text("METHOD") == "getaccount") {
                if (fields.contains("UserID") && text("UserID") != id)
                    return std::string("<ServerResponse><result>null</result></ServerResponse>");
                require(text("ScopeID") == zero, "lookup scope");
            }
            if (text("METHOD") == "getaccounts") {
                require(text("query") == "Remote Resident", "search preserves query");
                return "<ServerResponse><account0 type=\"List\">" + record + "</account0></ServerResponse>";
            }
            if (text("METHOD") == "setaccount") {
                require(text("PrincipalID") == id && text("UserTitle") == "Grid title", "store account fields");
                if (scalar_store)
                    return std::string("<ServerResponse><result>Success</result></ServerResponse>");
            }
            return "<ServerResponse><result type=\"List\">" + record + "</result></ServerResponse>";
        };
        services::AccountStore store(":memory:", ":memory:", true);
        const auto local = store.create_user("Local", "Only", "password", zero);
        store.set_backend(services::remote_account_backend(config, request));
        require(!store.find_by_id(zero, local), "remote miss must never expose local account row");
        auto account = store.find_by_id(zero, id);
        require(account && account->user_level == 7 && account->user_flags == 4 && account->created == 123 &&
                    account->user_title == "Grid title" && account->service_urls->size() == 1,
                "remote account metadata and service URLs");
        require(store.find(zero, "Remote", "Resident").has_value(), "remote name lookup");
        require(store.find_by_email(zero, "remote@example.invalid").has_value(), "remote email lookup");
        require(store.search_accounts(zero, "Remote Resident").size() == 1, "remote search");
        require(store.store_account(*account) && store.store_service_urls(*account), "remote stores");
        scalar_store = true;
        require(!store.store_account(*account), "original store needs a returned account, not scalar success");
        store.set_backend(services::remote_account_backend(
            config, [](const auto &, const auto &) -> std::string { throw std::runtime_error("offline"); }));
        require(!store.find_by_id(zero, local), "transport failure must not fall back to local data");
        require(store.search_accounts(zero, "Local").empty(), "failed remote search stays empty");
        std::cout << "PASS remote account lookup/search/store and local isolation\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
