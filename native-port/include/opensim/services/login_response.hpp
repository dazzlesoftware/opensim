// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/host_resolver.hpp"
#include "opensim/framework/time_zone.hpp"
#include "opensim/services/user_account.hpp"
#include "opensim/services/grid_user_service.hpp"
#include "opensim/services/grid_region.hpp"
#include "opensim/services/library_service.hpp"
#include <functional>
#include <optional>
namespace opensim::services {
struct LoginUserInfo {
    std::optional<std::string> first_name, last_name;
    std::uint64_t home_region_handle = 0;
    framework::Vector3 home_position, home_look_at;
};
struct LoginInventoryFolder {
    std::optional<std::string> name = std::string{};
    framework::Uuid id, parent;
    std::uint16_t version = 0;
    std::int16_t type = 0;
    LoginInventoryFolder() = default;
    explicit LoginInventoryFolder(const InventoryFolder& folder)
        : name(folder.name), id(folder.id), parent(folder.parent),
          version(static_cast<std::uint16_t>(folder.version)), type(static_cast<std::int16_t>(folder.type)) {}
};
struct LoginFriendInfo {
    std::optional<std::string> friend_id;
    std::int32_t my_flags = 0, their_flags = 0;
};
struct LoginResponseLibrary {
    using Folders = std::vector<std::shared_ptr<LoginInventoryFolder>>;
    std::function<std::shared_ptr<LoginInventoryFolder>()> root;
    std::function<std::optional<Folders>()> folders;
    static std::shared_ptr<LoginResponseLibrary> bind(const LibraryService& service);
};
struct LoginResponseInput {
    using Text = std::optional<std::string>;
    const UserAccount* account = nullptr;
    const framework::AgentCircuitData* circuit = nullptr;
    const GridUserInfo* grid_user = nullptr;
    const GridRegion *destination = nullptr, *home = nullptr;
    std::optional<LoginResponseLibrary::Folders> skeleton;
    std::optional<std::vector<std::shared_ptr<InventoryItem>>> gestures;
    std::optional<std::vector<std::shared_ptr<LoginFriendInfo>>> friends;
    std::shared_ptr<LoginResponseLibrary> library;
    Text where, start_location, message, map_url, profile_url, openid_url, search_url, currency, dst_zone,
        destinations_url, avatars_url, classified_fee;
    framework::Vector3 position, look_at;
    std::optional<RegionEndpoint> client_endpoint;
    framework::Uuid real_id;
    std::int32_t max_agent_groups = 0;
};
struct LoginResponseEnvironment {
    using Daylight = std::function<bool()>;
    std::function<std::optional<RegionEndpoint>(const GridRegion&)> external_endpoint;
    std::function<std::optional<std::string>(const GridRegion&)> server_uri;
    Daylight local_daylight;
    std::function<Daylight(const std::string&)> find_timezone;
};
void bind_login_response_network(LoginResponseEnvironment& environment,
                                 std::shared_ptr<framework::HostResolver> resolver = {});
void bind_login_response_timezones(LoginResponseEnvironment& environment,
                                   std::shared_ptr<framework::TimeZoneDatabase> database = {},
                                   std::function<std::int64_t()> clock = {});
struct LoginBuddyInfo {
    std::optional<std::string> id;
    std::int32_t rights_have = 1, rights_given = 1;
    network::Value to_table() const;
};
struct LoginBuddyList {
    std::vector<std::shared_ptr<LoginBuddyInfo>> buddies;
    void add(const std::shared_ptr<LoginBuddyInfo>& buddy);
    network::Value to_array() const;
};
class LoginResponse {
  public:
    using Text = std::optional<std::string>;
    using Node = std::shared_ptr<network::Value>;
    using Random = std::function<framework::Uuid()>;
    using Clock = std::function<double()>;
    explicit LoginResponse(Random random = {}, Clock clock = {});
    LoginResponse(const LoginResponseInput& input, const LoginResponseEnvironment& environment,
                  Random random = {}, Clock clock = {});
    Text login = "false", dst = "N", stipend_since_login = "N", gendered = "Y", ever_logged_in = "Y";
    Text first_name = "Test", last_name = "User", agent_access = "M", agent_access_max = "A",
         start_location = "last", allow_first_life = "Y";
    Text error_reason = "key",
         error_message = "You have entered an invalid name/password combination.  Check Caps/lock.";
    Text message = "Welcome to OpenSim!", seed_capability = "",
         home = "{'region_handle':[r256000,r256000], 'position':[r0,r0,r0], 'look_at':[r0,r0,r0]}",
         look_at = "[r1,r0,r0]";
    Text map_url = "", profile_url = "", openid_url = "", search_url = "", currency = "",
         classified_fee = "0", destinations_url, avatars_url, sim_address;
    std::uint32_t sim_port = 0, sim_http_port = 0, region_x = 255232, region_y = 254976;
    std::int32_t circuit_code = 0, region_size_x = 0, region_size_y = 0, max_agent_groups = 60;
    framework::Uuid agent, session, secure_session, real_id;
    Node inventory_root, inventory_skeleton, inventory_library, inventory_library_owner,
        inventory_library_root, active_gestures;
    std::shared_ptr<LoginBuddyList> buddy_list;
    network::Value to_xml_rpc();
    network::Value to_llsd() const;
    void add_ui(std::string key, Text value);
    void add_classified(std::int32_t id, Text name);
    void set_event_categories(Text, Text) {} // The source method is a TODO with no state changes.
    static Text texture(std::size_t index);
    static void texture(std::size_t index, Text value);
    static network::Value array_list_to_osd(const Node& list);

  private:
    Clock clock_;
    Node login_flags_, global_textures_, event_categories_, ui_config_, classified_categories_,
        initial_outfit_, ui_config_hash_;
    static Node global_texture_hash();
};
}
