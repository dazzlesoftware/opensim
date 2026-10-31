// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_orchestration.hpp"
#include "opensim/services/login_service.hpp"
#include <stdexcept>
namespace opensim::services {
LoginOrchestrator::LoginOrchestrator(LoginDependencies dependencies,const server::IniConfig& config):dependencies_(std::move(dependencies)),require_inventory_(config.boolean("LoginService","RequireInventory",true)),allow_duplicates_(config.boolean("PresenceService","AllowDuplicatePresences",false)),fallback_to_any_(config.boolean("LoginService","AllowLoginFallbackToAnyRegion",true)){}
std::variant<network::Value,PreparedLogin> LoginOrchestrator::prepare(const UserAccount& account,const framework::Uuid& scope,const framework::Uuid& session,const framework::Uuid& secure_session,const std::optional<std::string>& start){
    const auto user=framework::Uuid(account.principal_id);
    if(!dependencies_.grid_user)throw std::runtime_error("Grid-user service unavailable");
    auto info=dependencies_.grid_user(account.principal_id);
    if(!allow_duplicates_&&info&&info->online&&!info->last_region.is_zero()){
        if(dependencies_.recover_duplicate)dependencies_.recover_duplicate(scope,user,*info);
        else{
            if(!dependencies_.grid)throw std::runtime_error("Grid service unavailable during duplicate recovery");
            const auto region=dependencies_.grid->by_id(scope,info->last_region);
            if(region&&!region->server_uri().empty())throw std::runtime_error("Duplicate-session IM recovery connector unavailable");
        }
        if(!dependencies_.logged_out)throw std::runtime_error("Grid-user logout service unavailable");
        dependencies_.logged_out(account.principal_id,scope,info->last_region,info->last_position,info->last_look_at);
        return login_failure("AlreadyLoggedInProblem");
    }
    if(require_inventory_&&!dependencies_.inventory)return login_failure("InventoryProblem");
    if(dependencies_.suitcase_root)dependencies_.suitcase_root(user);
    if(!dependencies_.inventory)throw std::runtime_error("Inventory service unavailable");
    auto skeleton=dependencies_.inventory(user);
    if(require_inventory_&&!skeleton)return login_failure("InventoryProblem");
    // The original condition dereferences a null skeleton when inventory is
    // optional, and rejects an empty skeleton regardless of RequireInventory.
    if(!skeleton)throw std::runtime_error("Null inventory skeleton");
    if(skeleton->empty())return login_failure("InventoryProblem");
    if(dependencies_.presence&&!dependencies_.presence->login(account.principal_id,session,secure_session))return login_failure("GridProblem");
    std::optional<GridRegion> home;
    if(info){if(!info->home_region.is_zero()&&dependencies_.grid){home=dependencies_.grid->by_id(scope,info->home_region);if(!home&&!start)throw std::invalid_argument("Null start while reporting missing home");}}
    else{info=GridUserInfo{};info->last_position=info->home_position={128,128,30};}
    LoginDestinationSelector selector(dependencies_.grid,fallback_to_any_);
    auto destination=selector.find(scope,info,start,home);
    if(!destination.region){abort(session);return login_failure("GridProblem");}
    if(account.user_level>=200)destination.flags|=512; // TeleportFlags.Godlike
    std::shared_ptr<framework::AvatarAppearance> appearance;if(dependencies_.appearance)appearance=dependencies_.appearance(user);
    PreparedLogin result;result.account=account;result.scope=scope;result.session=session;result.secure_session=secure_session;result.grid_user=*info;result.home=std::move(home);result.skeleton=std::move(*skeleton);result.destination=std::move(destination);result.appearance=std::move(appearance);return result;
}
void LoginOrchestrator::abort(const framework::Uuid& session){if(!dependencies_.presence)throw std::runtime_error("Presence service unavailable during login rollback");dependencies_.presence->logout(session);}
void LoginOrchestrator::cleanup(const framework::Uuid& session){if(dependencies_.presence)dependencies_.presence->logout(session);}
}
