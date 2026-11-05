// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/viewer_region.hpp"
#include "opensim/scene/basic_profile.hpp"
#include "opensim/scene/profile_requests.hpp"
#include "opensim/scene/default_animations.hpp"
#include "opensim/scene/legacy_inventory_requests.hpp"
#include "opensim/scene/task_inventory_file.hpp"
#include "opensim/scene/part_script_events.hpp"
#include "opensim/scene/inventory_new.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/scene/texture_layers.hpp"
#include "opensim/framework/task_inventory_item.hpp"
#include "opensim/services/agent_launch.hpp"
#include "opensim/services/simulation_agent_updates.hpp"
#include "opensim/services/presence_service.hpp"
#include "opensim/services/user_account.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/http_route_registry.hpp"
namespace opensim::scene {
// Host policies remain explicit while estate/parcel modules are being ported.
// Both login admission and viewer traffic use the same circuit/presence state.
struct RegionInstantMessage;
struct EstateRegionSettings {
    std::uint32_t flags;
    std::int32_t agent_limit, maturity;
    float object_bonus;
};
struct ViewerGroupTitle {
    framework::Uuid role;
    std::string name;
    bool selected = false;
};
struct ParcelAccessEntry {
    framework::Uuid agent;
    std::uint32_t flags = 0;
    std::int32_t expires = 0;
};
struct EnvironmentDay {
    std::int32_t length = 14400;
    std::int32_t offset = 57600;
    std::shared_ptr<const network::Value> settings;
    void apply(network::Value update, bool normalize_cycle = true);
    static EnvironmentDay from_windlight(const network::Value& legacy);
};
struct ScriptParcel {
    framework::Uuid id, owner;
    std::uint32_t flags = 0;
    std::int32_t area = 0;
    framework::Uuid group;
    std::string name = "Your Parcel", description;
    std::int32_t claim_date = 0, landing_type = 0;
    framework::Vector3d landing_point, landing_look_at, center;
    bool see_avatars = true;
    std::int32_t dwell = 0;
    bool any_avatar_sounds = true, group_avatar_sounds = true;
    std::vector<ParcelAccessEntry> access_entries;
    std::vector<std::uint8_t> land_bitmap;
    std::int32_t local_id = 1;
    bool group_owned = false;
    std::optional<EnvironmentDay> environment_day;
    std::uint32_t auction_id = 0;
    framework::Uuid authorized_buyer, snapshot_id, media_id;
    std::int32_t category = 0;
    std::int32_t claim_price = 0;
    std::int32_t sale_price = 0;
    std::int32_t status = 0;
    std::int32_t other_clean_time = 0;
    std::int32_t pass_price = 0;
    float pass_hours = 0;
    std::string media_url, music_url;
    std::string media_type = "none/none";
    std::string media_description;
    std::int32_t media_width = 0, media_height = 0;
    bool media_auto_scale = false;
    bool media_loop = false;
    bool obscure_media = false;
};
struct AgentPreferences {
    std::string access = "M", language = "en-us";
    float hover = 0.f;
    bool language_public = true;
    std::int32_t everyone = 0, group = 0, next_owner = 0;
};
struct RegionSpawnPoint {
    float yaw = 0, pitch = 0, distance = 0;
};
struct RegionPolicies {
    bool environment_enabled = false;
    std::function<void(std::optional<framework::Uuid>, std::optional<std::string>)> persist_environment;
    network::CircuitTransportOptions circuit_transport;
    int udp_receive_buffer_bytes = 0;
    bool disable_facelights = false;
    int texture_send_limit = 20;
    int minimum_asset_upload_level = 0;
    std::function<int(framework::Uuid)> asset_upload_user_level;
    std::function<std::optional<std::vector<std::uint8_t>>(framework::Uuid, std::uint32_t)> request_mute_list;
    std::function<void(framework::Uuid, framework::Uuid, const std::string&, int, std::int32_t)> store_mute;
    std::function<void(framework::Uuid, framework::Uuid, const std::string&)> remove_mute;
    bool bypass_object_permissions = false;
    std::function<AgentPreferences(framework::Uuid)> load_preferences;
    std::function<std::string(framework::Uuid)> load_preference_language;
    std::function<void(framework::Uuid, const AgentPreferences&)> save_preferences;
    std::uint32_t object_capacity = 15000;
    float statistics_fps_factor = 1.f;
    double target_poll_ms = 50.;
    std::function<std::shared_ptr<framework::IAssetCache>()> baked_texture_cache;
    std::function<void(framework::Uuid, const framework::WearableCacheItems&)> store_baked_appearance;
    std::function<framework::WearableCacheItems(framework::Uuid)> fetch_baked_appearance;
    std::function<void(std::function<void()>)> schedule_baked_fetch;
    std::optional<float> sound_max_distance = 100.f;
    std::optional<network::lludp::Packet> sample_economy_data;
    bool sample_economy_sell_enabled = true;
    std::function<std::optional<services::GridRegion>(std::uint64_t)> teleport_destination;
    std::function<bool(const framework::Uuid&, const framework::Uuid&)> teleport_arrived;
    std::function<void(services::GridRegion&, framework::Uuid, framework::Uuid)> cancel_teleport_destination;
    bool disable_teleport_cancellation = false;
    std::function<void(const std::string&)> release_teleport_source;
    std::function<std::string(services::GridRegion&, const ViewerAgent&, framework::Vector3,
                              services::LoginTransferContext&)>
        teleport_access;
    std::function<std::optional<network::lludp::Packet>(
        const services::GridRegion&, services::GridRegion&, services::GridRegion&, const ViewerAgent&,
        framework::Vector3, framework::Vector3, services::LoginTransferContext&, const framework::Uuid&,
        std::uint32_t, std::string&)>
        teleport_create;
    std::function<std::string(const ViewerAgent&, framework::Uuid, network::lludp::Packet&)> resolve_landmark;
    std::function<void(const RegionInstantMessage&)> forward_instant_message;
    std::function<void(const RegionInstantMessage&)> offer_friendship;
    std::function<void(framework::Uuid, framework::Uuid, bool)> answer_friendship;
    std::function<bool(framework::Uuid, framework::Uuid)> terminate_friendship;
    std::function<std::optional<std::int32_t>(framework::Uuid, framework::Uuid, std::int32_t)>
        grant_friend_rights;
    std::function<std::vector<RegionInstantMessage>(framework::Uuid)> pending_friendships;
    std::function<std::vector<framework::Uuid>(framework::Uuid)> online_friends;
    std::function<void(framework::Uuid, framework::Uuid, bool)> friend_status_changed;
    std::function<std::vector<RegionInstantMessage>(framework::Uuid)> retrieve_offline_messages;
    std::function<bool(framework::Uuid sender, framework::Uuid recipient)> can_instant_message;
    std::shared_ptr<UserProfileRequests> advanced_profiles;
    std::function<std::shared_ptr<ProfileRequestLand>(float, float)> advanced_profile_land;
    std::shared_ptr<ProfileRequestMoney> advanced_profile_money;
    std::function<framework::Uuid(const ViewerAgent&)> advanced_profile_parcel;
    std::function<std::shared_ptr<ProfileRequestPresence>(framework::Uuid)> advanced_profile_presence;
    std::shared_ptr<BasicProfileModule> basic_profile;
    std::shared_ptr<BasicProfileAccounts> basic_profile_accounts;
    std::function<bool(framework::Uuid, const framework::Vector3&, std::string&)> access;
    std::function<bool(const framework::AgentCircuitData&, std::string_view)> verify_foreign_client;
    std::function<bool(const std::shared_ptr<framework::AgentCircuitData>&, std::string&)> authorize;
    std::function<bool(framework::Uuid, const framework::Vector3&, std::string&)> can_rez;
    std::function<bool(framework::Uuid, const SceneObject&)> can_edit;
    std::function<bool(framework::Uuid, const SceneObject&)> can_move;
    std::function<std::shared_ptr<const ScriptParcel>(framework::Vector3)> script_parcel_at;
    std::function<std::vector<std::shared_ptr<const ScriptParcel>>()> list_parcels;
    std::function<void(std::shared_ptr<const ScriptParcel>, bool persist)> update_parcel_access;
    std::function<bool(framework::Uuid)> is_local_grid_user;
    std::function<std::optional<services::UserAccount>(framework::Uuid)> script_account;
    std::function<std::optional<services::GridRegion>(std::string_view, bool)> script_grid_region;
    std::function<framework::Uuid(std::string_view, std::string_view, std::string_view)> script_account_key;
    std::function<bool(framework::Uuid, framework::Uuid, framework::Vector3, framework::Vector3)> set_home;
    std::function<std::int32_t(std::int32_t)> script_parcel_capacity;
    std::function<void(const Terrain&)> persist_baked_terrain;
    std::function<void(std::size_t, float, float)> persist_terrain_texture_heights;
    std::function<void(bool, std::size_t, framework::Uuid)> persist_terrain_texture_asset;
    std::function<void(float, float, float)> persist_region_terrain_settings;
    std::function<void(const EstateRegionSettings&)> persist_estate_region_settings;
    std::function<void(bool)> persist_region_sun_settings;
    std::function<bool(const SceneObject&, framework::Vector3)> can_object_entry;
    std::function<bool(framework::Uuid, framework::Vector3)> group_move;
    float nonphysical_prim_min = 0.001f;
    float nonphysical_prim_max = 256.f;
    float physical_prim_min = 0.01f;
    float physical_prim_max = 10.f;
    std::function<void()> parcel_prim_count_tainted;
    std::function<std::uint32_t(framework::Uuid, const SceneObject&)> client_flags;
    std::function<bool(framework::Uuid, const SceneObject&)> can_edit_inventory;
    std::function<bool(framework::Uuid)> viewer_ui_god;
    std::function<int(framework::Uuid)> god_rights_level;
    bool automatic_viewer_gods = false;
    bool implicit_gods = false;
    std::function<bool(framework::Uuid)> estate_manager;
    bool estate_manager_is_god = false;
    float estate_billable_factor = 0.f;
    std::string estate_name = "My Estate";
    std::string estate_abuse_email;
    std::uint32_t estate_detail_only_flags = 0x04020000u;
    std::vector<framework::Uuid> estate_managers, estate_users, estate_groups, estate_bans;
    std::function<void(framework::Uuid, framework::Uuid, std::optional<bool>, std::optional<bool>,
                       std::optional<bool>, std::optional<bool>)>
        persist_estate_access;
    std::function<void(framework::Uuid, std::uint32_t)> persist_estate_covenant;
    std::function<void(framework::Uuid, const std::vector<RegionSpawnPoint>&)> persist_telehub;
    std::function<void(framework::Uuid)> forward_estate_covenant;
    std::function<void(framework::Uuid, std::string, std::string)> forward_estate_message;
    std::function<void(std::optional<framework::Uuid>)> forward_estate_teleport_home;
    bool estate_communications = false;
    bool bypass_estate_permissions = false;
    bool strict_estate_access = true;
    std::uint32_t estate_menu_flags = 0x10000006u;
    std::uint32_t estate_handshake_only_flags = 0x00108020u;
    bool enforce_estate_age = false;
    bool enforce_estate_identity = false;
    std::function<std::uint32_t(framework::Uuid)> account_profile_flags;
    bool telehub_allow_landmarks = false;
    std::string spawn_point_routing = "closest";
    bool use_sl_landing_behavior = false;
    std::uint32_t estate_id = 0, parent_estate_id = 1;
    std::int32_t estate_price_per_meter = 1, estate_redirect_x = 0, estate_redirect_y = 0;
    std::uint32_t region_agent_capacity = 100, region_object_capacity = 15000;
    std::string region_type;
    std::function<bool(framework::Uuid, const SceneObject&, const TaskInventoryFileItem&)> can_edit_script;
    std::function<void(std::shared_ptr<framework::AssetBase>)> store_asset;
    std::function<std::optional<std::string>(std::shared_ptr<framework::AssetBase>)> store_inventory_asset;
    std::function<bool(framework::Uuid, framework::Uuid, framework::Uuid)> can_edit_inventory_notecard;
    std::function<bool(framework::Uuid, framework::Uuid)> can_edit_user_inventory_script;
    std::function<std::shared_ptr<framework::AssetBase>(framework::Uuid)> load_asset;
    std::function<std::shared_ptr<framework::AssetBase>(const framework::AgentCircuitData&, framework::Uuid)>
        load_foreign_asset;
    std::function<void(std::function<void()>)> schedule_asset_fetch;
    CachedTextureLayerDecoder decode_texture_layers;
    std::function<void(framework::Uuid, framework::Uuid, framework::Uuid, bool, framework::Uuid)>
        script_updated;

    // Source Constants.DefaultScriptText; hosts may supply the default asset's text.
    std::string default_script =
        "default\n{\n    state_entry()\n    {\n        llSay(0, \"Script running\");\n    }\n}";
    std::function<framework::Uuid(framework::Uuid, const services::InventoryItem&, const std::string&)>
        create_script_asset;
    std::filesystem::path
        physics_library; // Empty selects no physics; explicit ubODE profile supports one avatar.
    std::shared_ptr<const DefaultAnimations> animations;
    std::optional<LegacyInventoryBackend> inventory;
    // Real avatar inventory storage remains alive until the region stops.
    services::InventoryService* inventory_service = nullptr;
    std::function<bool(framework::Uuid, const framework::AvatarAppearance&)> save_appearance;
    int appearance_save_delay_seconds = 5;
    int appearance_send_delay_seconds = 2;
    std::function<bool(framework::Uuid, framework::Uuid, framework::Uuid)> can_copy_task_inventory;
    std::function<bool(framework::Uuid)> inventory_is_god;
    std::function<void(const services::InventoryItem&, int)> inventory_upload_complete;
    bool bypass_inventory_permissions = false;
    bool bypass_sale_permissions = false;
    std::function<bool(framework::Uuid, const SceneObject&, const services::InventoryItem&)>
        can_drop_inventory;
    std::function<bool(framework::Uuid, framework::Uuid)> inventory_group_member;
    std::function<std::vector<framework::Uuid>(framework::Uuid)> estate_group_memberships;
    std::function<std::vector<std::shared_ptr<services::EventGroupMembership>>(framework::Uuid)>
        viewer_group_memberships;
    std::function<std::optional<ActiveViewerGroup>(framework::Uuid, std::optional<framework::Uuid>)>
        viewer_active_group;
    std::function<std::vector<ViewerGroupTitle>(framework::Uuid, framework::Uuid)> viewer_group_titles;
    std::function<void(framework::Uuid, framework::Uuid, framework::Uuid)> select_viewer_group_title;
    std::function<void(framework::Uuid, framework::Uuid, bool, bool)> set_viewer_group_preferences;
    std::function<std::uint64_t(framework::Uuid, framework::Uuid)> inventory_group_powers;
    std::function<void(framework::Uuid, framework::Uuid, framework::Uuid,
                       std::shared_ptr<framework::TaskInventoryItem>)>
        task_inventory_transaction;
    std::function<void(framework::Uuid, const services::InventoryItem&)> fetch_remote_inventory_assets;
    std::function<void(framework::Uuid, framework::Uuid, std::shared_ptr<services::InventoryItem>)>
        user_inventory_transaction;
    std::function<std::optional<services::InventoryItem>(framework::Uuid)> library_inventory_item;
    std::function<std::optional<framework::Uuid>()> library_inventory_owner;
    std::function<bool(framework::Uuid, framework::Uuid)> can_copy_user_inventory;
    std::function<bool(std::int8_t, framework::Uuid)> can_create_user_inventory;
    std::function<bool(const InventoryNewRequest&)> inventory_creation_transaction;
    std::function<framework::Uuid(std::uint8_t)> environment_default_asset;
    std::function<InventoryNewLandmark(framework::Uuid)> inventory_landmark;
    bool propagate_permissions = true;
    std::function<bool(const framework::TaskInventoryItem&, const SceneObject&)> can_run_script;
    bool scripts_disabled = false;
    float min_timer_interval = 0.5f;
    bool legacy_sit_offsets = true;
    std::int32_t script_heap_limit_bytes = 1024 * 1024;
    float script_delay_factor = 1.f;
    std::optional<std::string> script_url_host;
    std::string script_http_shard = "OpenSim";
    std::string script_http_user_agent;
    std::int32_t script_url_limit = 15000;
    bool script_ossl_enabled = true;
    bool script_ossl_permission_error_to_owner = false;
    bool script_ossl_grid_gods = false;
    std::string script_ossl_threat_level = "VeryLow";
    struct OsslRule {
        std::string owners;
        std::string creators;
    };
    std::map<std::string, OsslRule> script_ossl_rules;
    std::int32_t notecard_line_chars = 255;
    bool automatic_link_permission = false;
    std::int32_t linkset_data_limit = 32 * 1024;
    std::function<void(framework::Uuid)> remove_script_targets;
    std::function<std::optional<PartEmailEvents>()> script_email_events;
    std::function<void(framework::Uuid, std::uint64_t, std::uint32_t)> script_events_changed;

    std::function<void(framework::Uuid, framework::Uuid, framework::Uuid)> new_script;

    std::function<void(const ViewerAgent&)> entered, logged_out;
    std::function<void(const std::string&)> log;
};
struct RegionInstantMessage {
    framework::Uuid sender, recipient, session, region;
    framework::Vector3 position;
    std::uint32_t estate = 0, timestamp = 0;
    std::uint8_t offline = 0, dialog = 0;
    bool from_group = false;
    std::u16string sender_name, message;
    std::vector<std::uint8_t> bucket;
};
class RegionRuntime final : public services::ISimulationLogin, public services::ISimulationAgentUpdates {
  public:
    bool kick_viewer(framework::Uuid, std::u16string_view reason);
    bool receive_grid_moderation(const RegionInstantMessage&);
    bool alert_viewer(framework::Uuid, const std::string& message);
    bool deliver_instant_message(const RegionInstantMessage&);
    bool notify_friend_status(framework::Uuid recipient, framework::Uuid friend_id, bool online,
                              std::optional<framework::Uuid> expected_session = {});
    bool notify_friend_termination(framework::Uuid recipient, framework::Uuid former_friend);
    bool notify_friend_rights(framework::Uuid recipient, framework::Uuid principal, std::int32_t previous,
                              std::int32_t granted);
    std::shared_ptr<const framework::AgentCircuitData::ServiceUrls>
        profile_service_urls(framework::Uuid) const;
    std::shared_ptr<ProfileRequestPresence> profile_presence(framework::Uuid) const;
    RegionRuntime(services::GridRegion region, services::IPresenceService& presence, RegionPolicies policies);
    ~RegionRuntime();
    network::UdpEndpoint start(std::shared_ptr<server::RegistryCapabilityListener> listener,
                               std::shared_ptr<server::RegistryCapabilityListener> secure_script_listener = {},
                               std::shared_ptr<server::RegistryCapabilityListener> plain_script_listener = {});
    void poll(double host_sleep_ms = 0.);
    void stop(const std::function<void()>& save_scene = {});
    bool can_edit_task_inventory(framework::Uuid object, framework::Uuid agent) const;
    bool can_edit_task_script(framework::Uuid item, framework::Uuid object, framework::Uuid agent) const;
    // HTTP callers queue the real transaction to the scene owner thread. Quiesce
    // pending script updates before stopping HTTP so no handler waits on a stopped
    // owner. The scene and asset dependencies must remain alive until it returns.
    std::vector<std::string> update_task_script(framework::Uuid agent, framework::Uuid item,
                                                framework::Uuid object, bool running,
                                                std::span<const std::uint8_t>);
    framework::Uuid update_inventory_asset(framework::Uuid agent, framework::Uuid item,
                                           framework::Uuid object, std::span<const std::uint8_t>);
    // Both queues are quiesced together before stopping HTTP.
    void quiesce_script_updates();
    std::int32_t active_script_count() const;

    services::GridRegion descriptor() const;
    World& world();
    ViewerRegion& viewers();
    void load_terrain_baseline(int format, std::string_view blob);
    void bake_terrain();
    void revert_terrain();
    void flip_terrain(bool x_axis);
    void adjust_terrain(const std::function<float(float)>& adjust);
    void rescale_terrain(float minimum, float maximum);
    void set_terrain_limits(float lower, float raise);
    void load_region_sun_settings(bool use_estate_sun);
    bool use_estate_sun() const;
    void load_environment_day(EnvironmentDay day);
    void set_scripts_enabled(bool enabled);
    void restore_task_inventory(framework::Uuid part,
                                const std::vector<std::shared_ptr<framework::TaskInventoryItem>>& items);
    std::optional<std::vector<framework::TaskInventoryItem>>
    snapshot_task_inventory(framework::Uuid part) const;
    void set_region_setting_flags(std::uint32_t flags);
    void set_region_maturity(std::int32_t maturity);
    void set_region_telehub(framework::Uuid object, std::vector<RegionSpawnPoint> points = {});
    void set_region_menu_limits(int agent_limit, float object_bonus);
    void set_estate_covenant(framework::Uuid covenant, std::uint32_t timestamp);
    void receive_estate_covenant(std::uint32_t estate, framework::Uuid covenant);
    void receive_estate_message(std::uint32_t estate, const std::string& message);
    void receive_estate_teleport_home(std::uint32_t estate, std::optional<framework::Uuid> target);
    void set_terrain_handshake(bool blocked, float water_height);
    void set_terrain_texture_heights(ViewerRegionOptions::TerrainTextureHeights heights);
    void set_terrain_texture_assets(std::array<framework::Uuid, 4> textures,
                                    std::array<framework::Uuid, 4> materials);
    void load_terrain_file(std::string_view filename, std::span<const std::uint8_t> data);
    services::SceneCapabilities* capabilities();
    bool query_access(services::GridRegion&, const framework::Uuid&, const std::optional<std::string>&, bool,
                      const framework::Vector3&, std::vector<framework::Uuid>&,
                      services::LoginTransferContext&, std::string&) override;
    std::shared_ptr<framework::AgentTransferScene> get_scene(const framework::Uuid&) override {
        return {};
    }
    bool confirm_teleport_arrival(framework::Uuid agent, framework::Uuid token);
    bool close_agent_session(framework::Uuid agent, std::string_view session);
    bool update_agent(services::GridRegion&, const std::shared_ptr<framework::AgentData>&,
                      services::LoginTransferContext&) override;
    bool update_agent(services::GridRegion&, const std::shared_ptr<framework::AgentPosition>&) override {
        return false;
    }
    bool viewer_god_mode(framework::Uuid agent, framework::Uuid session) const;
    bool agent_has_god_permissions(framework::Uuid agent) const;
    bool query_gatekeeper_access(const services::GridRegion&, const framework::AgentCircuitData&,
                                 std::string&);
    bool create_agent(const services::GridRegion*, services::GridRegion&,
                      const std::shared_ptr<framework::AgentCircuitData>&, std::uint32_t,
                      services::LoginTransferContext&, std::string&) override;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
}
