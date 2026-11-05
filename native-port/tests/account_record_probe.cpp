#include "opensim/services/account_store.hpp"
#include "opensim/framework/identity.hpp"
#include <sqlite3.h>
#include <iostream>
using namespace opensim;
int main(int argc, char **) {
    bool source_accounts = argc > 1;
    for (int mode = 0; mode < 24; ++mode) {
        services::AccountStore store(":memory:", ":memory:", true, source_accounts);
        services::UserAccount a(framework::Uuid(framework::ZeroUuid),
                                framework::Uuid("00000000-0000-0000-0000-000000000007"), "First", "Last",
                                "mail", [] { return 123; });
        a.user_level = -4;
        a.user_flags = 17;
        a.user_title = "Title";
        if (mode & 1)
            a.email = std::nullopt;
        if (mode & 2)
            a.user_title = "";
        if (mode & 4)
            a.user_country = "CA";
        if (mode & 8)
            a.first_name = std::nullopt;
        if (mode >= 16)
            (*a.service_urls)["null"] = std::any{};
        else
            (*a.service_urls)["url"] = std::string("https://host/two words/試験");
        // A second connection cannot see a separate in-memory database. Snapshot
        // through the public lookup, then expose replacement defaults in disk tests.
        std::string status = "OK";
        try {
            store.store_account(a);
        } catch (...) {
            status = "EX";
        }
        auto found = store.find(framework::ZeroUuid, "First", "Last");
        std::cout << mode << '|' << status << '|' << (found ? found->principal_id : "missing");
        if (found)
            std::cout << '|' << found->created << '|' << found->user_level << '|' << found->user_flags << '|'
                      << found->email.value_or("") << '|' << found->user_title.value_or("") << '|'
                      << std::any_cast<std::string>(found->service_urls->at("url"));
        std::cout << '\n';
    }
    services::AccountStore store(":memory:", ":memory:", true, source_accounts);
    const std::vector<std::string> scopes = {
        std::string(framework::ZeroUuid), "00000000-0000-0000-0000-000000000001",
        "00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000003"};
    const std::vector<std::string> ids = {
        "00000000-0000-0000-0000-000000000011", "00000000-0000-0000-0000-000000000012",
        "00000000-0000-0000-0000-000000000013", "00000000-0000-0000-0000-000000000014"};
    for (int i = 0; i < 3; ++i) {
        services::UserAccount a(framework::Uuid(scopes[i]), framework::Uuid(ids[i]), "First", "Last",
                                "shared@example", [] { return 123; });
        store.store_account(a);
    }
    for (const auto &scope : scopes) {
        auto named = store.find(scope, "First", "Last");
        std::cout << "name|" << (named ? named->principal_id : "missing") << '\n';
        for (const std::string email : {"shared@example", "Shared@example", "missing", "' OR 1=1 --"}) {
            auto a = store.find_by_email(scope, email);
            std::cout << "email|" << (a ? a->principal_id : "missing") << '\n';
        }
        for (const auto &id : ids) {
            auto a = store.find_by_id(scope, id);
            std::cout << "id|" << (a ? a->principal_id : "missing") << '\n';
        }
        for (const std::string query :
             {"First", "Las", "Fir xxx", "xxx Las", "fIr", "%%%", "___", "a b First", "a b c First", "a b",
              "First Last third", "a'bc", "\tFirst\t", "😀", "   ", "First  Last"}) {
            std::string result = "search|";
            try {
                for (const auto &a : store.search_accounts(scope, query))
                    result += a.principal_id + ",";
            } catch (...) {
                result += "EX";
            }
            std::cout << result << '\n';
        }
    }
}
