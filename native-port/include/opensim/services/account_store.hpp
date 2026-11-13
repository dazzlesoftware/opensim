/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
// Native port of the account lookup, password/web-key and SQLite token paths.
#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "opensim/framework/agent_circuit.hpp"
#include "opensim/services/user_account.hpp"
#include "opensim/services/auth_info.hpp"
struct sqlite3;
namespace opensim::services {
class SqliteUserAccountTable;
class SqliteAuthenticationTable;
struct AuthenticationResult {
    std::string token, real_id;
    explicit operator bool() const noexcept { return !token.empty(); }
};
class AccountStore {
public:
    AccountStore(
        const std::string& accounts_path,
        const std::string& auth_path,
        bool initialize = false,
        bool source_accounts = false,
        std::string account_realm = "UserAccounts",
        std::string auth_realm = "auth",
        bool local_authentication = true);
    ~AccountStore();
    AccountStore(const AccountStore&) = delete;
    AccountStore& operator=(const AccountStore&) = delete;
    std::optional<UserAccount> find(std::string_view scope, std::string_view first, std::string_view last);
    std::optional<UserAccount> find_by_email(std::string_view scope, std::string_view email);
    std::optional<UserAccount> find_by_id(std::string_view scope, std::string_view principal);
    std::vector<UserAccount> search_accounts(std::string_view scope, std::string_view query);
    bool store_service_urls(const UserAccount& account);
    bool store_account(const UserAccount& account);
    bool set_password(std::string_view principal,const std::optional<std::string>& password,
        std::function<framework::Uuid()> random={});
    std::string create_user(std::string_view first, std::string_view last, std::string_view password,
        std::string_view scope, int level = 0);
    AuthenticationResult authenticate(std::string_view principal, std::string_view password_hash,
        int lifetime_minutes, bool allow_web_keys = true);
    bool verify(std::string_view principal, std::string_view token, int lifetime_minutes);
    bool release(std::string_view principal, std::string_view token);
    std::shared_ptr<AuthInfo> get_auth_info(framework::Uuid principal);
    bool set_auth_info(const std::shared_ptr<AuthInfo>& info);
private:
    std::optional<UserAccount> find_fields(std::string_view scope, const std::vector<std::pair<std::string,std::string>>& fields);
    sqlite3* db_ = nullptr;
    std::string auth_schema_;
    std::recursive_mutex mutex_;
    bool has_country_=false;
    std::unique_ptr<SqliteUserAccountTable> account_table_;
    std::unique_ptr<SqliteAuthenticationTable> authentication_table_;
};
}
