// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_circuit.hpp"
#include <chrono>
#include <mutex>
#include <unordered_map>
#include "opensim/scene/profile_packets.hpp"
#include "opensim/scene/advanced_profile.hpp"
#include "opensim/framework/user_profile_models.hpp"
#include "opensim/framework/profile_cache_entry.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/framework/vector3d_text.hpp"
#include <any>
namespace opensim::services {
class AccountStore;
class GridService;
class IPresenceService;
}
namespace opensim::server {
class GridIdentity;
}
namespace opensim::scene {
struct AvatarClassifiedInfo;
struct AvatarPickInfo;
class ProfileUserDirectory : public ProfileUsers {
  public:
    virtual void web_failed(framework::Uuid) = 0;
    virtual bool is_local(framework::Uuid) = 0;
    virtual ProfileText server_url(framework::Uuid, std::u16string_view, bool& failed) = 0;
    virtual ProfileText server_url(framework::Uuid, std::u16string_view) = 0;
};
class NativeProfileUserDirectory final : public ProfileUserDirectory {
  public:
    using ServiceUrls = framework::AgentCircuitData::ServiceUrls;
    using UrlLookup = std::function<std::shared_ptr<const ServiceUrls>(framework::Uuid)>;
    NativeProfileUserDirectory(services::AccountStore& accounts, framework::Uuid scope, UrlLookup urls);
    void web_failed(framework::Uuid) override;
    bool is_local(framework::Uuid) override;
    ProfileText server_url(framework::Uuid, std::u16string_view, bool& failed) override;
    ProfileText server_url(framework::Uuid, std::u16string_view) override;

  private:
    ProfileText lookup(framework::Uuid, std::u16string_view, bool& failed, bool include_failed);
    services::AccountStore& accounts_;
    framework::Uuid scope_;
    UrlLookup urls_;
    std::mutex mutex_;
    std::unordered_map<framework::Uuid, std::chrono::steady_clock::time_point, framework::UuidHash> failed_;
};
struct ProfileHomeGrid : ProfileGridInfo {
    bool has_hg = false;
    virtual ProfileText gatekeeper_url() = 0;
    virtual int is_local_grid(const ProfileText& gatekeeper, bool resolve) = 0;
};
class NativeProfileHomeGrid final : public ProfileHomeGrid {
  public:
    explicit NativeProfileHomeGrid(const server::GridIdentity& grid);
    ProfileText gatekeeper_url() override;
    int is_local_grid(const ProfileText& gatekeeper, bool resolve) override;

  private:
    const server::GridIdentity& grid_;
};
struct ProfileRequestGridRegion {
    std::int32_t x = 0, y = 0;
    std::uint64_t handle() const {
        return (std::uint64_t{static_cast<std::uint32_t>(x)} << 32) | static_cast<std::uint32_t>(y);
    }
};
class ProfileRequestGridService {
  public:
    virtual ~ProfileRequestGridService() = default;
    virtual std::shared_ptr<ProfileRequestGridRegion> region_by_name(framework::Uuid scope,
                                                                     const ProfileText&) = 0;
};
class NativeProfileGridService final : public ProfileRequestGridService {
  public:
    explicit NativeProfileGridService(services::GridService& grid) : grid_(grid) {
    }
    std::shared_ptr<ProfileRequestGridRegion> region_by_name(framework::Uuid scope,
                                                             const ProfileText&) override;

  private:
    services::GridService& grid_;
};
struct ProfileAccountData {
    std::int32_t flags = 0, created = 0;
    ProfileText title;
};
class ProfileAccountService {
  public:
    virtual ~ProfileAccountService() = default;
    virtual std::shared_ptr<ProfileAccountData> get(framework::Uuid scope, framework::Uuid user) = 0;
};
class NativeProfileAccounts final : public ProfileAccountService {
  public:
    explicit NativeProfileAccounts(services::AccountStore& accounts) : accounts_(accounts) {
    }
    std::shared_ptr<ProfileAccountData> get(framework::Uuid scope, framework::Uuid user) override;

  private:
    services::AccountStore& accounts_;
};
using ProfileUserInfo = framework::OrderedDictionary<std::string, std::any>;
class ProfileUserAgent {
  public:
    virtual ~ProfileUserAgent() = default;
    virtual std::shared_ptr<ProfileUserInfo> get_info(framework::Uuid) = 0;
};
class ProfileFriends {
  public:
    virtual ~ProfileFriends() = default;
    virtual bool online(framework::Uuid client, framework::Uuid avatar) = 0;
};
struct ProfileAgents {
    std::size_t count = 0;
};
class ProfilePresenceService {
  public:
    virtual ~ProfilePresenceService() = default;
    virtual std::shared_ptr<ProfileAgents> agents(const std::vector<std::string>&) = 0;
};
class NativeProfilePresenceService final : public ProfilePresenceService {
  public:
    explicit NativeProfilePresenceService(services::IPresenceService& presence) : presence_(presence) {
    }
    std::shared_ptr<ProfileAgents> agents(const std::vector<std::string>& users) override;

  private:
    services::IPresenceService& presence_;
};
class ProfileNpcClient {
  public:
    virtual ~ProfileNpcClient() = default;
    virtual ProfileText about() = 0;
    virtual ProfileText born() = 0;
    virtual framework::Uuid image() = 0;
};
using ProfileMemberships = std::vector<std::shared_ptr<framework::GroupMembershipData>>;
class ProfileRequestGroups : public ProfileGroups {
  public:
    virtual std::shared_ptr<ProfileMemberships> memberships(framework::Uuid) = 0;
};
class ProfileRequestScene;
struct ProfileRequestPresence {
    virtual ~ProfileRequestPresence() = default;
    bool child = false, npc = false, deleted = false;
    framework::Uuid current_parcel;
    virtual std::shared_ptr<ProfileNpcClient> npc_client() = 0;
    virtual bool viewer_ui_god() = 0;
    virtual framework::Vector3 absolute_position() = 0;
    virtual std::shared_ptr<ProfileRequestScene> scene() = 0;
};
class ProfileRequestLand {
  public:
    virtual ~ProfileRequestLand() = default;
    virtual ProfileText name() = 0;
};
class ProfileRequestMoney {
  public:
    virtual ~ProfileRequestMoney() = default;
    virtual bool amount_covered(framework::Uuid, std::int32_t) = 0;
    virtual void apply_charge(framework::Uuid, std::int32_t, std::int32_t transaction_type) = 0;
};
class ProfileRequestScene {
  public:
    virtual ~ProfileRequestScene() = default;
    virtual std::shared_ptr<ProfileRequestLand> land(float x, float y) = 0;
    virtual std::shared_ptr<ProfileRequestMoney> money() = 0;
    virtual ProfileText region_name() = 0;
    virtual std::uint32_t world_x() = 0;
    virtual std::uint32_t world_y() = 0;
    virtual std::uint64_t region_handle() = 0;
    virtual ProfileText scene_name() = 0;
};
struct ProfilePickUpdate {
    framework::Uuid pick, creator, snapshot;
    bool top_pick = false, enabled = false;
    ProfileText name, description;
    std::int32_t sort_order = 0;
};
struct ProfileClassifiedUpdate {
    framework::Uuid classified, parcel, snapshot;
    std::uint32_t category = 0, parent_estate = 0;
    ProfileText name, description;
    framework::Vector3 global_position;
    std::uint8_t flags = 0;
    std::int32_t price = 0;
};
class ProfileRequestClient : public AdvancedProfileClient {
  public:
    virtual bool is_active() = 0;
    virtual std::shared_ptr<ProfileRequestPresence> scene_presence() = 0;
    virtual void send_properties(const AvatarProfileProperties&) = 0;
    virtual void send_interests(const AvatarProfileInterests&) = 0;
    virtual void send_groups(framework::Uuid, std::shared_ptr<ProfileMemberships>) = 0;
    virtual framework::Uuid agent_id() = 0;
    virtual std::shared_ptr<ProfileRequestScene> scene() = 0;
    virtual framework::Vector3 scene_agent_position() = 0;
    virtual void send_notes(framework::Uuid, const ProfileText&) = 0;
    virtual void send_preferences(bool email, bool visible, const ProfileText& address) = 0;
    virtual void alert(std::u16string_view, bool modal) = 0;
    virtual void send_picks(framework::Uuid, std::shared_ptr<framework::UserProfileNameList>) = 0;
    virtual void send_classifieds(framework::Uuid, std::shared_ptr<framework::UserProfileNameList>) = 0;
    virtual void send_pick_info(const AvatarPickInfo&) = 0;
    virtual void send_classified_info(const AvatarClassifiedInfo&) = 0;
};
// Viewer replies use a circuit-bound sender and a session-bound event queue.
class ProfileViewerReplies : public ProfileRequestClient {
  public:
    void send_properties(const AvatarProfileProperties&) override;
    void send_interests(const AvatarProfileInterests&) override;
    void send_groups(framework::Uuid, std::shared_ptr<ProfileMemberships>) override;
    void send_notes(framework::Uuid, const ProfileText&) override;
    void send_preferences(bool email, bool visible, const ProfileText& address) override;
    void send_picks(framework::Uuid, std::shared_ptr<framework::UserProfileNameList>) override;
    void send_classifieds(framework::Uuid, std::shared_ptr<framework::UserProfileNameList>) override;
    void send_pick_info(const AvatarPickInfo&) override;
    void send_classified_info(const AvatarClassifiedInfo&) override;

  protected:
    virtual void send_profile_packet(network::lludp::Packet) = 0;
    virtual std::shared_ptr<ProfileEvents> profile_event_queue() = 0;
};

class ProfileRequestContext {
  public:
    virtual ~ProfileRequestContext() = default;
    virtual std::shared_ptr<ProfileUserDirectory> users() = 0;
    virtual std::shared_ptr<ProfileHomeGrid> grid() = 0;
    virtual ProfileText local_profile_uri() = 0;
    virtual std::shared_ptr<ProfileRequestPresence> presence(framework::Uuid) = 0;
    virtual void info(std::u16string_view) = 0;
    virtual void get_foreign_image(framework::Uuid, const ProfileText& uri, bool local) = 0;
    virtual std::shared_ptr<ProfileRequestGridService> grid_service() = 0;
    virtual framework::Uuid region_scope() = 0;
    virtual void warn_pick_parcel(float x, float y, const ProfileText& scene) = 0;
    virtual bool allow_web_urls() = 0;
    virtual ProfileText system_agent_born() = 0;
    virtual std::shared_ptr<ProfileAccountService> account_service() = 0;
    virtual std::shared_ptr<ProfileUserAgent> user_agent(const ProfileText&) = 0;
    virtual std::shared_ptr<ProfileFriends> friends() = 0;
    virtual std::shared_ptr<ProfilePresenceService> presence_service() = 0;
    virtual std::shared_ptr<ProfileRequestGroups> groups() = 0;
    virtual bool legacy_profile(std::shared_ptr<framework::UserProfileProperties>&, const ProfileText&) = 0;
    virtual void debug(const ProfileText&, const std::optional<std::u16string>& error = {}) = 0;
    virtual void process_error(const std::exception&) = 0;
    virtual void schedule(std::function<void()>) = 0;
    virtual ProfileText new_rpc_id();
    virtual bool request_object(std::shared_ptr<server::JsonRpcMembers>&, const ProfileText& method,
                                const ProfileText& uri, const ProfileText& id);
    virtual bool request(std::shared_ptr<network::Value>&, const ProfileText& method, const ProfileText& uri,
                         const ProfileText& id);

  private:
    server::JsonRpcRequestManager rpc_;
};
// Concrete original request workflows, shared by the full scene module and its
// admitted-client adapter; routing always goes through the owner's home grid.
class UserProfileRequests : public std::enable_shared_from_this<UserProfileRequests> {
  public:
    struct AsyncRequest {
        std::shared_ptr<ProfileRequestClient> client;
        std::shared_ptr<ProfileRequestPresence> presence;
        framework::Uuid agent;
        int type = 0;
    };
    virtual ~UserProfileRequests() = default;
    using Cache =
        framework::ExpiringCacheOS<framework::Uuid, std::shared_ptr<framework::UserProfileCacheEntry>,
                                   framework::UuidHash>;
    explicit UserProfileRequests(
        std::shared_ptr<ProfileRequestContext>,
        framework::ExpiringCacheBackend = framework::native_expiring_cache_backend());
    // Called after the circuit has authenticated the viewer packet.
    bool dispatch_viewer_packet(const network::lludp::Packet&, const std::shared_ptr<ProfileRequestClient>&);
    void request_avatar_properties(std::shared_ptr<ProfileRequestClient>, framework::Uuid);
    void avatar_interests_update(std::shared_ptr<ProfileRequestClient>, std::uint32_t, const ProfileText&,
                                 std::uint32_t, const ProfileText&, const ProfileText&);
    void avatar_properties_update(std::shared_ptr<ProfileRequestClient>,
                                  std::shared_ptr<framework::UserProfileProperties>);
    bool get_profile_data(std::shared_ptr<framework::UserProfileProperties>&, bool foreign,
                          const ProfileText&, ProfileText& message);
    bool get_user_account_data(framework::Uuid, std::shared_ptr<ProfileAccountData>&);
    virtual bool is_friend_online(std::shared_ptr<ProfileRequestClient>, framework::Uuid);
    void process_request(AsyncRequest);
    void process_local_requests();
    void process_hg_requests();
    std::size_t queue_count(bool hg) const;
    bool profile_server_uri(framework::Uuid, ProfileText&);
    std::shared_ptr<ProfileRequestPresence> find_presence(framework::Uuid);
    void notes_request(std::shared_ptr<AdvancedProfileClient> sender, const std::vector<ProfileText>* args);
    void notes_update(std::shared_ptr<ProfileRequestClient>, framework::Uuid target, const ProfileText&);
    void preferences_request(std::shared_ptr<ProfileRequestClient>);
    void preferences_update(bool email, bool visible, std::shared_ptr<ProfileRequestClient>);
    void picks_request(std::shared_ptr<AdvancedProfileClient>, const std::vector<ProfileText>* args);
    std::shared_ptr<framework::UserProfileNameList> get_picks(framework::Uuid);
    void classifieds_request(std::shared_ptr<AdvancedProfileClient>, const std::vector<ProfileText>* args);
    void classified_info_request(framework::Uuid, std::shared_ptr<ProfileRequestClient>);
    void pick_info_request(std::shared_ptr<AdvancedProfileClient>, const std::vector<ProfileText>* args);
    std::shared_ptr<framework::UserProfilePick> get_pick(framework::Uuid target, framework::Uuid pick);
    void pick_info_update(std::shared_ptr<ProfileRequestClient>, const ProfilePickUpdate&);
    void pick_delete(std::shared_ptr<ProfileRequestClient>, framework::Uuid);
    void classified_info_update(const ProfileClassifiedUpdate&, std::shared_ptr<ProfileRequestClient>);
    void classified_delete(framework::Uuid, std::shared_ptr<ProfileRequestClient>);
    void cache_foreign_image(framework::Uuid agent, framework::Uuid image);
    Cache& profile_cache() {
        return profiles_;
    }
    std::recursive_mutex& profile_cache_mutex() {
        return profile_mutex_;
    }
    auto& classified_owners() {
        return classified_owners_;
    }
    auto& classified_interests() {
        return classified_interests_;
    }
    std::recursive_mutex& classified_cache_mutex() {
        return classified_mutex_;
    }

  private:
    friend struct UserProfileAsyncProbe;
    struct WorkQueue {
        mutable std::recursive_mutex gate;
        mutable std::mutex stack;
        std::vector<AsyncRequest> items;
        bool running = false;
    };
    WorkQueue local_, hg_;
    void process_queue(WorkQueue&);
    void map_pick_proxy(framework::Uuid, std::shared_ptr<framework::UserProfilePick>, framework::Vector3d&,
                        bool fetch_valid_parcel_image);
    void classified_interest(framework::Uuid id, framework::Uuid owner);
    std::shared_ptr<framework::UserProfileNameList>
        fetch_picks(framework::Uuid, std::shared_ptr<framework::UserProfileNameList>);
    std::shared_ptr<ProfileRequestContext> context_;
    Cache profiles_;
    std::recursive_mutex profile_mutex_;
    framework::OrderedDictionary<framework::Uuid, framework::Uuid, framework::UuidHash> classified_owners_;
    framework::OrderedDictionary<framework::Uuid, std::int32_t, framework::UuidHash> classified_interests_;
    std::recursive_mutex classified_mutex_;
};
}
