// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"
#include "opensim/services/user_account.hpp"
namespace opensim::server {
struct AccountOperations {
    using Account = std::optional<services::UserAccount>;
    using Accounts = std::vector<Account>;
    using Id = framework::Uuid;
    std::function<Account(Id, Id)> by_id;
    std::function<Account(Id, std::string)> by_email;
    std::function<Account(Id, std::string, std::string)> by_name;
    std::function<Accounts(Id, std::string)> search;
    std::function<Accounts(Id, std::vector<std::string>)> multi;
    std::function<bool(services::UserAccount&)> store;
    std::function<Account(Id, Id, std::string, std::string, std::string, std::string, std::string)> create;
};
class AccountEndpoint {
  public:
    AccountEndpoint(AccountOperations operations, const IniConfig& config)
        : operations_(std::move(operations)), auth_(config, "UserAccountService"),
          allow_create_(config.boolean("UserAccountService", "AllowCreateUser", false)),
          allow_set_(config.boolean("UserAccountService", "AllowSetAccount", false)) {}
    network::HttpResponse handle(const network::HttpRequest& request);

  private:
    AccountOperations operations_;
    network::ServiceAuth auth_;
    bool allow_create_, allow_set_;
};
}
