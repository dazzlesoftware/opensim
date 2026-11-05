// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/user_account.hpp"
#include "opensim/services/region_store.hpp"
namespace opensim::services {
struct AccountProvisioning {
    using Id = framework::Uuid;
    std::function<std::shared_ptr<UserAccount>(Id, std::string, std::string)> by_name;
    std::function<bool(UserAccount&)> store;
    std::function<bool(Id, std::optional<std::string>)> set_password;
    std::function<std::vector<std::optional<GridRegion>>(Id)> default_regions;
    std::function<bool(std::string, Id, framework::Vector3, framework::Vector3)> set_home;
    std::function<bool(Id)> create_inventory;
    std::function<void(Id)> default_appearance;
    std::function<void(Id, std::string)> model_appearance;
};
class AccountProvisioner {
  public:
    AccountProvisioner(AccountProvisioning operations, bool create_default_entries = false,
                       UserAccount::Clock clock = {})
        : operations_(std::move(operations)), create_default_entries_(create_default_entries),
          clock_(std::move(clock)) {}
    std::shared_ptr<UserAccount> create(framework::Uuid scope, framework::Uuid principal,
                                        std::optional<std::string> first, std::optional<std::string> last,
                                        std::optional<std::string> password, std::optional<std::string> email,
                                        std::optional<std::string> model = std::string{});

  private:
    AccountProvisioning operations_;
    bool create_default_entries_;
    UserAccount::Clock clock_;
};
}
