// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/vector3d_text.hpp"
#include <functional>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace opensim::script {
struct LslList;
struct LslRotation {
    double x = 0, y = 0, z = 0, w = 1;
    bool operator==(const LslRotation &) const = default;
};
struct LslRegionEnvironmentEdit {
    std::int32_t transition;
    std::string day_cycle;
    double day_length, day_offset;
    std::array<double, 3> altitudes;
};
using LslValue = std::variant<std::int32_t, std::string, double, framework::Vector3d, LslRotation,
                              std::shared_ptr<const LslList>>;
struct LslList {
    std::vector<LslValue> items;
};
struct LslDiagnostic {
    std::size_t line, column;
    std::string message;
};
class LslProgram;
struct LslCompileResult {
    std::shared_ptr<const LslProgram> program;
    std::vector<LslDiagnostic> diagnostics;
    explicit operator bool() const { return bool(program); }
};
// Supported grammar is intentionally bounded. Unsupported syntax/types/API calls
// fail compilation; no partial program or hardcoded asset-text execution occurs.
LslCompileResult compile_lsl(std::string_view source);
std::string lsl_vector_string(const framework::Vector3d& value);
std::string lsl_date_from_unix(std::int32_t seconds);
struct LslDetector {
    framework::Uuid key;
    std::int32_t touch_face = -1;
    framework::Vector3d touch_pos{}, touch_normal{}, touch_binormal{};
    framework::Vector3d touch_st{-1, -1, 0}, touch_uv{-1, -1, 0};
    std::string name;
    framework::Uuid owner, group;
    framework::Vector3d position, velocity, grab;
    LslRotation rotation{0, 0, 0, 0};
    std::int32_t type = 0, link_number = 0;
};
struct LslEvent {
    std::string name;
    std::vector<LslValue> arguments;
    std::vector<LslDetector> detectors;
};
struct LslContext {
    framework::Uuid object, owner, group, item;
    std::string object_name;
    framework::Vector3 position;
    double water_height = 20.;
    std::string region_name;
    framework::Vector3d region_corner;
    std::string object_description;
    framework::Vector3 object_scale{.5f, .5f, .5f};
    framework::Vector3 velocity, omega, acceleration;
    framework::Uuid creator;
    float time_dilation = 1.f;
    std::string script_name;
    std::int32_t attachment_point = 0;
    framework::Vector3 attachment_offset{};
    LslRotation local_rotation{};
    std::optional<LslRotation> root_rotation;
    std::optional<framework::Vector3> root_position;
    std::string start_string;
    std::int32_t start_parameter = 0;
};
enum class LslChatType { Say, Owner, Debug };
struct LslChat {
    LslChatType type;
    std::int32_t channel;
    std::string text;
    LslContext context;
};
struct LslInventoryMetadata {
    std::string description;
    framework::Uuid creator;
    std::uint32_t creation_date = 0;
};
enum class LslParcelListAction { add, remove, reset };

struct LslAvatarName {
    std::string first, last, server;
    std::string scene_name() const {
        if (server.empty())
            return first + " " + last;
        return first + "." + last + " @" + server;
    }
};

struct LslHost {
    std::function<float(float, float)> ground_height;
    std::function<framework::Vector3(float, float, float, float)> ground_normal;
    std::function<std::int32_t()> unix_time;
    // Time in 100ns ticks. Defaults to a steady clock; tests/hosts can inject time.
    std::function<std::int64_t()> clock_ticks;
    std::function<void(const LslChat &)> chat;
    std::function<void(const LslChat &)> listener;
    std::function<void(const std::string &)> console;
    std::function<void(std::int32_t milliseconds)> request_sleep;
    std::optional<std::int32_t> console_channel;
    float script_delay_factor = 1.f;
    std::function<framework::Uuid(framework::Uuid, framework::Uuid, bool, bool)> request_url;
    std::function<std::string(framework::Uuid, framework::Uuid, std::string_view)> ossl_permission_error;
    bool ossl_enabled = false;
    bool ossl_permission_error_to_owner = false;
    std::function<double()> region_water_height;
    std::function<void(float)> set_region_water_height;
    std::function<void(bool)> set_region_sun_settings;
    std::function<float()> region_day_fraction;
    std::function<std::pair<std::int32_t, std::int32_t>(framework::Vector3, bool)> environment_timing;
    std::function<std::optional<LslRotation>(framework::Vector3, bool, bool)> environment_rotation;
    std::function<std::int32_t(framework::Uuid, const LslRegionEnvironmentEdit&)> replace_region_environment;
    std::function<std::int32_t(framework::Uuid, std::int32_t, const std::string&)> replace_parcel_environment;
    std::function<std::int32_t(framework::Uuid, framework::Uuid, const std::string&, std::int32_t,
                               const std::string&)> replace_agent_environment;
    std::function<void(framework::Uuid, const std::string&)> release_url;
    std::function<std::int32_t()> free_urls;
    std::function<std::string(framework::Uuid, framework::Uuid, const std::string&)> http_header;
    std::function<void(framework::Uuid, framework::Uuid, std::int32_t, std::string)> http_response;
    std::function<void(framework::Uuid, framework::Uuid, framework::Uuid, std::int32_t)> http_content_type;
    float min_timer_interval = 0.5f;
    std::int32_t heap_limit_bytes = 1024 * 1024;
    // Optional monotonic clock in 100ns ticks for elapsed script time.
    std::function<std::int64_t()> monotonic_ticks;
    std::function<void(framework::Uuid, framework::Vector3, bool)> target_omega;
    std::function<bool(framework::Uuid, bool, const std::string &)> set_object_text;
    std::function<void(framework::Uuid, std::string, std::uint32_t)> set_floating_text;
    std::function<void(framework::Uuid, bool, std::string)> set_interaction_label;
    std::function<void(framework::Uuid, const std::optional<std::string> &, double, std::uint8_t)> loop_sound;
    std::function<void(framework::Uuid, float)> adjust_sound_gain;
    std::function<void(framework::Uuid, float)> set_sound_radius;
    std::function<void(framework::Uuid, const std::string &, float)> set_collision_sound;
    std::function<void(framework::Uuid, const std::array<std::int32_t, 5> &)> set_pay_prices;
    std::function<bool(framework::Uuid, const std::string &)> preload_sound;
    using SoundBounds = std::pair<framework::Vector3, framework::Vector3>;
    std::function<void(framework::Uuid, const std::string &, double, std::uint8_t, bool,
                       const std::optional<SoundBounds> &)>
        play_sound;
    std::function<void(framework::Uuid, bool)> set_sound_queueing;
    std::function<std::optional<framework::Vector3>(framework::Uuid, framework::Vector3)> set_object_scale;
    std::function<void(framework::Uuid, std::uint8_t)> set_click_action;
    std::function<void(framework::Uuid, bool)> set_pass_touches;
    std::function<void(framework::Uuid, std::int32_t, framework::Vector3d, LslRotation)> set_sit_target;
    std::function<void(framework::Uuid, std::int32_t, float)> set_sit_active_range;
    std::function<float(framework::Uuid, std::int32_t)> sit_active_range;
    std::function<void(framework::Uuid, std::int32_t, framework::Vector3d)> set_stand_target;
    std::function<framework::Vector3d(framework::Uuid, std::int32_t)> stand_target;
    std::function<void(framework::Uuid, std::int32_t, std::optional<framework::Vector3d>,
                       std::optional<framework::Vector3d>, std::optional<bool>)> set_seat_camera;
    std::function<void(framework::Uuid, bool)> allow_inventory_drop;
    std::function<std::int32_t(framework::Uuid, const std::string &)> inventory_type;
    std::function<std::optional<framework::Uuid>(framework::Uuid, const std::string&, bool)> notecard_asset;
    std::function<std::string(framework::Uuid, std::int32_t)> cached_notecard_line;
    std::function<framework::Uuid(framework::Uuid, framework::Uuid, framework::Uuid,
                                  std::optional<std::int32_t>)> request_notecard;
    std::function<framework::Uuid(framework::Uuid, framework::Uuid, std::string)> request_inventory_data;
    std::function<framework::Uuid(framework::Uuid, framework::Uuid, framework::Uuid, std::int32_t)>
        request_agent_data;
    std::function<framework::Uuid(framework::Uuid, framework::Uuid, std::string, std::int32_t)>
        request_simulator_data;
    std::function<std::optional<LslInventoryMetadata>(framework::Uuid, const std::string &)>
        inventory_metadata;
    std::function<framework::Uuid(framework::Uuid, const std::string &)> inventory_key;
    std::function<std::optional<std::uint32_t>(framework::Uuid, const std::string &, std::int32_t)>
        inventory_permission;
    std::function<std::pair<framework::Uuid, std::int32_t>(framework::Uuid, framework::Uuid)>
        script_permissions;
    std::function<void(framework::Uuid, framework::Uuid, std::string_view, std::int32_t)> request_permissions;
    std::function<void(framework::Uuid)> clear_script_permissions;
    std::function<void(framework::Uuid)> release_script_urls;
    std::function<framework::Uuid(framework::Uuid, const std::string&)> find_script;
    std::function<bool(framework::Uuid)> script_running;
    std::function<void(framework::Uuid, bool)> set_script_running;
    std::function<void(framework::Uuid)> reset_other_script;
    std::function<void(framework::Uuid, framework::Uuid, const std::string &, bool)> script_animation;
    std::function<void(framework::Uuid, framework::Uuid, const std::string&, bool)> ossl_avatar_animation;
    std::function<std::vector<framework::Uuid>(framework::Uuid)> avatar_animations;
    std::function<std::string(framework::Uuid)> avatar_movement_state;
    std::function<std::string(framework::Uuid, std::string_view)> animation_override;
    std::function<void(framework::Uuid, framework::Uuid, std::string_view,
                       const std::optional<std::string> &)>
        change_animation_override;
    std::function<std::vector<std::string>(framework::Uuid, std::int32_t)> inventory_names;
    std::function<std::int32_t()> region_agent_count;
    std::function<std::optional<framework::Uuid>(framework::Uuid)> object_owner;
    std::function<std::optional<framework::Uuid>(framework::Uuid)> object_group;
    std::function<LslList(framework::Uuid)> attached_list;
    std::function<std::int32_t(framework::Uuid)> link_number;
    std::function<framework::Uuid(framework::Uuid, std::int32_t)> link_key;
    std::function<framework::Uuid(framework::Uuid, std::int32_t)> sit_target_avatar;
    std::function<std::int32_t(framework::Uuid, std::int32_t)> sit_flags;
    std::function<void(framework::Uuid, framework::Uuid)> unsit;
    std::function<void(framework::Uuid, framework::Uuid)> force_sit;
    std::function<std::optional<std::uint32_t>(framework::Uuid, std::int32_t)> object_permission;
    std::function<framework::Vector3(framework::Uuid)> geometric_center;
    std::function<float()> region_fps;
    std::function<std::optional<std::string>(framework::Uuid)> object_name;
    std::function<std::optional<std::string>(framework::Uuid)> avatar_name;
    std::function<std::string(framework::Uuid, framework::Uuid)> avatar_ip_address;
    using NameFormatter = std::string (*)(std::string_view);
    std::function<std::pair<framework::Uuid, bool>(framework::Uuid, framework::Uuid, framework::Uuid, NameFormatter)>
        request_identity_name;
    std::function<std::pair<framework::Uuid, bool>(framework::Uuid, framework::Uuid, LslAvatarName)>
        request_identity_key;
    std::function<framework::Uuid(std::string_view)> avatar_key_by_name;
    std::function<std::string(framework::Uuid, std::string_view, std::string_view)> linkset_data_read;
    std::function<std::int32_t(framework::Uuid, bool)> linkset_data_size;
    std::function<std::int32_t(framework::Uuid, std::string, std::string, std::string, bool)>
        linkset_data_write;
    std::function<void(framework::Uuid)> linkset_data_reset;
    std::function<std::vector<std::string>(framework::Uuid)> linkset_data_keys;
    std::function<LslList(framework::Uuid, const std::vector<std::string> &, std::string_view)>
        linkset_data_delete_keys;
    std::function<std::int32_t(framework::Uuid)> face_count;
    std::function<std::int32_t(framework::Uuid, bool)> prim_count;
    std::function<framework::Vector3(framework::Uuid)> agent_size;
    std::function<std::string(framework::Uuid)> agent_language;
    std::function<std::int32_t()> region_flags;
    std::function<std::int32_t(framework::Vector3)> parcel_flags;
    std::function<std::optional<framework::Uuid>(framework::Vector3)> parcel_owner;
    std::function<std::optional<framework::Vector3>(framework::Uuid)> entity_position;
    std::function<bool(framework::Uuid, LslParcelListAction, bool, framework::Uuid, double)>
        edit_parcel_list;
    std::function<bool(framework::Vector3, framework::Uuid)> script_danger;
    std::function<LslList(framework::Vector3, framework::Uuid, const LslList &)> parcel_details;
    std::function<std::vector<framework::Uuid>(std::int32_t, framework::Vector3)> region_agents;
    std::function<std::vector<std::string>()> region_agent_names;
};
enum class LslRunStatus { Idle, BudgetExhausted, Sleeping, Faulted };
struct LslRunResult {
    LslRunStatus status;
    std::size_t instructions = 0;
    std::string error;
    std::size_t events_completed = 0;
};
class LslInstance {
  public:
    LslInstance(std::shared_ptr<const LslProgram>, LslContext, const LslHost&);
    LslInstance(std::shared_ptr<const LslProgram>, LslContext, const LslHost&, bool start_running);
    ~LslInstance();
    LslInstance(LslInstance &&) noexcept;
    LslInstance &operator=(LslInstance &&) noexcept;
    LslInstance(const LslInstance &) = delete;
    void reset();
    bool running() const;
    void running(bool);
    bool post_event(LslEvent);
    LslRunResult run(std::size_t instruction_budget);
    std::size_t queued_events() const;
    std::int32_t heap_bytes_used() const;
    bool handles_event(std::string_view name) const;
    std::uint64_t event_mask() const;
    // Valid during host callbacks and while an event is budget-suspended.
    const LslEvent *current_event() const;
    void set_context(LslContext);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
