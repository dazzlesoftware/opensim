// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/account_store.hpp"
#include "opensim/services/login_destination.hpp"
#include "opensim/services/inventory_store.hpp"
#include "opensim/services/presence_service.hpp"
#include "opensim/network/rpc.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include <variant>
namespace opensim::services {
struct PreparedLogin {
    UserAccount account;
    framework::Uuid scope, session, secure_session;
    GridUserInfo grid_user;
    std::optional<GridRegion> home;
    std::vector<InventoryFolder> skeleton;
    LoginDestination destination;
    std::shared_ptr<framework::AvatarAppearance> appearance;
};
struct LoginDependencies {
    std::function<std::optional<GridUserInfo>(const std::string&)> grid_user;
    std::function<bool(const std::string&, const framework::Uuid&, const framework::Uuid&,
                       const framework::Vector3&, const framework::Vector3&)>
        logged_out;
    std::function<std::optional<std::vector<InventoryFolder>>(const framework::Uuid&)> inventory;
    std::function<void(const framework::Uuid&)> suitcase_root;
    // Implements the complete duplicate-session recovery helper, including
    // its IM send and first grid-user logout when it returns true.
    std::function<bool(const framework::Uuid&, const framework::Uuid&, const GridUserInfo&)>
        recover_duplicate;
    std::function<std::shared_ptr<framework::AvatarAppearance>(const framework::Uuid&)> appearance;
    GridService* grid = nullptr;
    IPresenceService* presence = nullptr;
};
class LoginOrchestrator {
  public:
    LoginOrchestrator(LoginDependencies dependencies, const server::IniConfig& config);
    std::variant<network::Value, PreparedLogin>
    prepare(const UserAccount& account, const framework::Uuid& scope, const framework::Uuid& session,
            const framework::Uuid& secure_session, const std::optional<std::string>& start);
    void abort(const framework::Uuid& session);
    void cleanup(const framework::Uuid& session);

  private:
    LoginDependencies dependencies_;
    bool require_inventory_, allow_duplicates_, fallback_to_any_;
};
}
