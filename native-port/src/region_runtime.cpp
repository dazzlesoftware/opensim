// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/region_runtime.hpp"
#include "opensim/server/poll_http_service.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/host_resolver.hpp"
#include "opensim/framework/animation_set.hpp"
#include "opensim/services/user_account.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/permissions.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include "opensim/framework/culture_sort.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/framework/integer_text.hpp"
#include <set>
#include "opensim/services/scene_client_verification.hpp"
#include "opensim/scene/avatar_update.hpp"
#include "opensim/scene/avatar_packets.hpp"
#include "opensim/scene/basic_profile_runtime.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/framework/avatar_appearance_constants.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/network/sdk_osd_stream.hpp"
#include "opensim/framework/quaternion_text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/service_protocol.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/field_string.hpp"
#include "opensim/scene/object_add.hpp"
#include "opensim/scene/object_update.hpp"
#include "opensim/scene/rez_location.hpp"
#include "opensim/scene/chat_packet.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/scene/viewer_physics.hpp"
#include "opensim/physics/ubode.hpp"
#include "opensim/scene/root_position.hpp"
#include "opensim/scene/task_inventory_file.hpp"
#include "opensim/scene/xfer_download.hpp"
#include "opensim/scene/script_running.hpp"
#include "opensim/scene/script_task_update.hpp"
#include "opensim/scene/script_control.hpp"
#include "opensim/scene/task_inventory_registry.hpp"
#include "opensim/scene/inventory_script_rez.hpp"
#include "opensim/scene/script_instance_create.hpp"
#include "opensim/scene/task_inventory_move.hpp"
#include "opensim/scene/task_inventory_remove.hpp"
#include "opensim/scene/task_inventory_update.hpp"
#include "opensim/scene/inventory_drop_permission.hpp"
#include "opensim/scene/user_inventory_operations.hpp"
#include "opensim/scene/inventory_copy.hpp"
#include "opensim/scene/inventory_asset_update.hpp"
#include "opensim/scene/scene_inventory_asset_update.hpp"
#include "opensim/scene/agent_inventory_add.hpp"
#include "opensim/scene/inventory_notifications.hpp"
#include "opensim/scene/inventory_item_create.hpp"
#include "opensim/script/asset_text.hpp"
#include "opensim/scene/object_properties.hpp"
#include <future>
#include <bit>
#include <bitset>
#include <cmath>
#include "opensim/framework/culture_prefix.hpp"
#include <numbers>
#include <algorithm>
#include <charconv>
#include <iterator>
#include <thread>
#include <deque>
#include <random>
namespace opensim::scene {
using namespace framework;
using namespace services;
using namespace network;
namespace {
Value environment_real(double number) {
    Value value;
    value.kind = Value::Kind::Real;
    value.real = static_cast<float>(number);
    return value;
}
float environment_day_fraction(const EnvironmentDay& day, double unix_seconds) {
    return static_cast<float>(std::clamp(std::fmod(unix_seconds + day.offset, day.length) / day.length, 0., 1.));
}
std::optional<script::LslRotation> sample_environment_rotation(const Value& cycle, const Value* settings,
                                                               float altitude, float fraction, bool moon) {
    const auto& tracks = cycle.get("tracks").array;
    if (tracks.size() < 2)
        return std::nullopt;
    std::array altitudes{1000.f, 2000.f, 3000.f};
    std::array<std::size_t, 3> altitude_tracks{2, 3, 4};
    if (settings) {
        const auto& saved = settings->get("track_altitudes").array;
        for (std::size_t i = 0; i < std::min(saved.size(), altitudes.size()); ++i)
            altitudes[i] = static_cast<float>(osd_real(saved[i]));
        for (std::size_t i = 0; i < altitudes.size(); ++i)
            for (auto j = i + 1; j < altitudes.size(); ++j)
                if (altitudes[i] > altitudes[j]) {
                    std::swap(altitudes[i], altitudes[j]);
                    std::swap(altitude_tracks[i], altitude_tracks[j]);
                }
    }
    std::size_t track_index = 1;
    if (!(altitude < altitudes.front())) {
        std::size_t index = 1;
        while (index < altitudes.size() && !(altitudes[index] > altitude))
            ++index;
        do {
            track_index = altitude_tracks[--index];
        } while (index > 0 && (track_index >= tracks.size() || tracks[track_index].array.empty()));
    }
    if (track_index >= tracks.size())
        return std::nullopt;
    struct Key {
        float time;
        const Value* name;
    };
    std::vector<Key> keys;
    for (const auto& key : tracks[track_index].array)
        if (key.contains("key_keyframe") && key.contains("key_name"))
            keys.push_back({static_cast<float>(osd_real(key.get("key_keyframe"))), &key.get("key_name")});
    if (keys.empty())
        return std::nullopt;
    std::stable_sort(keys.begin(), keys.end(), [](const Key& left, const Key& right) {
        return std::isnan(left.time) ? !std::isnan(right.time) : left.time < right.time;
    });
    using Rotation = std::array<float, 4>;
    const auto rotation = [&](const Key& key) -> std::optional<Rotation> {
        const auto& frame = cycle.get("frames").get(osd_string(*key.name));
        if (osd_string(frame.get("type")) != "sky")
            return {};
        const auto& values = frame.get(moon ? "moon_rotation" : "sun_rotation").array;
        if (values.size() != 4)
            return Rotation{0, 0, 0, 1};
        return Rotation{static_cast<float>(osd_real(values[0])), static_cast<float>(osd_real(values[1])),
                        static_cast<float>(osd_real(values[2])), static_cast<float>(osd_real(values[3]))};
    };
    const auto result = [](const std::optional<Rotation>& q) -> std::optional<script::LslRotation> {
        if (!q)
            return {};
        return script::LslRotation{(*q)[0], (*q)[1], (*q)[2], (*q)[3]};
    };
    if (keys.size() == 1 || keys.front().time < 0)
        return result(rotation(keys.front()));
    const auto next = std::ranges::find_if(keys, [fraction](const Key& key) { return key.time > fraction; });
    const bool wraps = next == keys.begin() || next == keys.end();
    const auto& left = wraps ? keys.back() : *std::prev(next);
    const auto& right = wraps ? keys.front() : *next;
    const auto right_time = right.time + (wraps ? 1.f : 0.f);
    auto a = rotation(left);
    const auto b = rotation(right);
    if (!a || left.time < 0)
        return right_time < 0 ? std::nullopt : result(b);
    if (!b || right_time < 0 || right_time == left.time)
        return result(a);
    const auto amount = std::clamp((fraction - left.time) / (right_time - left.time), 0.f, 1.f);
    float dot = 0;
    for (std::size_t i = 0; i < a->size(); ++i)
        dot += (*a)[i] * (*b)[i];
    if (dot < 0) {
        dot = -dot;
        for (auto& component : *a)
            component = -component;
    }
    auto weight_a = 1.f - amount;
    auto weight_b = amount;
    if (1.f - dot >= .0005f) {
        const auto theta = std::acos(dot);
        const auto inverse = 1.f / std::sin(theta);
        weight_a = std::sin(theta * weight_a) * inverse;
        weight_b = std::sin(theta * amount) * inverse;
    }
    for (std::size_t i = 0; i < a->size(); ++i)
        (*a)[i] = (*a)[i] * weight_a + (*b)[i] * weight_b;
    return result(a);
}
Value environment_vector(const Value& input, std::span<const float> defaults, bool present, bool quaternion = false) {
    Value value;
    value.kind = Value::Kind::Array;
    std::array<float, 4> components{};
    if (quaternion)
        components[3] = 1;
    if (!present) {
        std::ranges::copy(defaults, components.begin());
    } else if (input.kind == Value::Kind::Array && input.array.size() == defaults.size()) {
        for (std::size_t i = 0; i < defaults.size(); ++i)
            components[i] = static_cast<float>(osd_real(input.array[i]));
    } else if (input.kind == Value::Kind::String) {
        auto text = framework::trim_utf8(osd_string(input));
        if (quaternion) {
            framework::Quaternion parsed;
            if (!framework::parse_quaternion(text, parsed))
                throw std::invalid_argument("Invalid environment rotation");
            components = {parsed.x, parsed.y, parsed.z, parsed.w};
        } else {
            if (defaults.size() == 2) {
                if (std::ranges::count(text, ',') != 1)
                    throw std::invalid_argument("Invalid environment vector");
                const auto end = text.ends_with('>') ? text.size() - 1 : text.size();
                text.insert(end, ",0");
            }
            Vector3 parsed;
            if (!framework::try_parse_sdk_vector3(framework::utf16(text), parsed))
                throw std::invalid_argument("Invalid environment vector");
            components = {parsed.x, parsed.y, parsed.z};
        }
    }
    for (std::size_t i = 0; i < defaults.size(); ++i)
        value.array.push_back(environment_real(components[i]));
    return value;
}
Value normalize_water_frame(const Value& input) {
    auto frame = Value::object({{"type", Value("water")}});
    constexpr std::pair<const char*, float> scalars[] = {
        {"blur_multiplier", .04f}, {"fresnel_offset", .5f}, {"fresnel_scale", .4f},
        {"scale_above", .03f}, {"scale_below", .2f}, {"underwater_fog_mod", .25f},
        {"water_fog_density", 10.f},
    };
    for (const auto& [name, fallback] : scalars)
        frame.set(name, environment_real(input.contains(name) ? osd_real(input.get(name)) : fallback));
    constexpr std::pair<const char*, const char*> textures[] = {
        {"normal_map", "822ded49-9a6c-f61c-cb89-6df54f42cdf4"},
        {"transparent_texture", "2bfd3884-7e27-69b9-ba3a-3e673f680004"},
    };
    for (const auto& [name, fallback] : textures) {
        Value texture(input.contains(name) ? osd_uuid(input.get(name)).str() : fallback);
        texture.kind = Value::Kind::Uuid;
        frame.set(name, std::move(texture));
    }
    constexpr std::array normal_scale{2.f, 2.f, 2.f};
    constexpr std::array fog_color{.0156f, .149f, .2509f};
    constexpr std::array wave1{1.05f, -.42f};
    constexpr std::array wave2{1.11f, -1.16f};
    for (const auto& [name, defaults] :
         {std::pair<const char*, std::span<const float>>{"normal_scale", normal_scale},
          {"water_fog_color", fog_color}, {"wave1_direction", wave1}, {"wave2_direction", wave2}})
        frame.set(name, environment_vector(input.get(name), defaults, input.contains(name)));
    return frame;
}
Value normalize_sky_frame(const Value& input) {
    auto frame = Value::object({{"type", Value("sky")}});
    constexpr std::pair<const char*, float> scalars[] = {
        {"cloud_scale", .42f}, {"cloud_shadow", .27f}, {"cloud_variance", 0.f},
        {"dome_offset", .96f}, {"dome_radius", 15000.f}, {"droplet_radius", 800.f},
        {"gamma", 1.f}, {"ice_level", 0.f}, {"max_y", 1605.f}, {"moisture_level", 0.f},
        {"moon_brightness", .5f}, {"moon_scale", 1.f}, {"planet_radius", 6360.f},
        {"sky_bottom_radius", 6360.f}, {"sky_top_radius", 6420.f}, {"star_brightness", 0.f},
        {"sun_arc_radians", .00045f}, {"sun_scale", 1.f},
    };
    for (const auto& [name, fallback] : scalars)
        frame.set(name, environment_real(input.contains(name) ? osd_real(input.get(name)) : fallback));
    if (input.contains("reflection_probe_ambiance"))
        frame.set("reflection_probe_ambiance", environment_real(osd_real(input.get("reflection_probe_ambiance"))));
    constexpr std::pair<const char*, const char*> textures[] = {
        {"bloom_id", "3c59f7fe-9dc8-47f9-8aaf-a9dd1fbc3bef"},
        {"cloud_id", "1dc1368f-e8fe-f02d-a08d-9d9f11c1af6b"},
        {"halo_id", "12149143-f599-91a7-77ac-b52a3c0f59cd"},
        {"moon_id", "ec4b9f0b-d008-45c6-96a4-01dd947ac621"},
        {"rainbow_id", "11b4c57c-56b3-04ed-1f82-2004363882e4"},
        {"sun_id", "00000000-0000-0000-0000-000000000000"},
    };
    for (const auto& [name, fallback] : textures) {
        Value texture(input.contains(name) ? osd_uuid(input.get(name)).str() : fallback);
        texture.kind = Value::Kind::Uuid;
        frame.set(name, std::move(texture));
    }
    const auto vector = [&](const char* name, std::initializer_list<float> defaults, bool quaternion = false) {
        frame.set(name, environment_vector(input.get(name), defaults, input.contains(name), quaternion));
    };
    vector("cloud_color", {.41f, .41f, .41f});
    vector("cloud_pos_density1", {1.f, .5260f, 1.f});
    vector("cloud_pos_density2", {1.f, .5260f, .12f});
    vector("cloud_scroll_rate", {.2f, .011f});
    vector("glow", {5.f, .001f, -.48f});
    vector("moon_rotation", {0.f, .9239557f, 0.f, .3824995f}, true);
    vector("sun_rotation", {0.f, -.3824995f, 0.f, .9239557f}, true);
    const auto& sunlight = input.get("sunlight_color");
    constexpr std::array default_sunlight{.7342f, .7815f, .9f, .3f};
    Value sunlight_color;
    if (sunlight.kind == Value::Kind::Array && sunlight.array.size() != 4) {
        sunlight_color = environment_vector(sunlight, std::span(default_sunlight).first<3>(), true);
        sunlight_color.array.push_back(environment_real(0));
    } else {
        sunlight_color = environment_vector(sunlight, default_sunlight, sunlight.kind == Value::Kind::Array);
    }
    frame.set("sunlight_color", std::move(sunlight_color));

    const auto& haze = input.get("legacy_haze");
    auto legacy = Value::object({});
    const auto haze_vector = [&](const char* name, std::initializer_list<float> defaults) {
        legacy.set(name, environment_vector(haze.get(name), defaults, haze.contains(name)));
    };
    haze_vector("ambient", {1.047f, 1.047f, 1.047f});
    haze_vector("blue_density", {.2447f, .4487f, .76f});
    haze_vector("blue_horizon", {.4954f, .4954f, .64f});
    constexpr std::pair<const char*, float> haze_scalars[] = {
        {"density_multiplier", .00018f}, {"distance_multiplier", .8f},
        {"haze_density", .7f}, {"haze_horizon", .19f},
    };
    for (const auto& [name, fallback] : haze_scalars)
        legacy.set(name, environment_real(haze.contains(name) ? osd_real(haze.get(name)) : fallback));
    frame.set("legacy_haze", std::move(legacy));

    const auto coefficients = [](const Value* saved, std::array<float, 5> defaults,
                                 std::optional<float> anisotropy = {}) {
        if (saved && saved->kind != Value::Kind::Map)
            throw std::invalid_argument("Invalid sky coefficients");
        auto result = Value::object({});
        constexpr std::array names{"constant_term", "exp_scale", "exp_term", "linear_term", "width"};
        for (std::size_t i = 0; i < names.size(); ++i)
            result.set(names[i], environment_real(saved ? osd_real(saved->get(names[i])) : defaults[i]));
        if (anisotropy)
            result.set("anisotropy", environment_real(saved ? osd_real(saved->get("anisotropy")) : *anisotropy));
        return result;
    };
    const auto entries = [&](const char* name) -> std::span<const Value> {
        const auto& value = input.get(name);
        return value.kind == Value::Kind::Array ? std::span<const Value>(value.array) : std::span<const Value>{};
    };
    const auto set_coefficients = [&](const char* name, std::initializer_list<Value> values) {
        Value array;
        array.kind = Value::Kind::Array;
        array.array = values;
        frame.set(name, std::move(array));
    };
    const auto absorption = entries("absorption_config");
    // The managed reader assigns both entries to the first coefficient.
    auto first_absorption = coefficients(absorption.empty() ? nullptr : &absorption.front(), {0, 0, 0, 0, 25000});
    if (absorption.size() > 1)
        first_absorption = coefficients(&absorption[1], {});
    set_coefficients("absorption_config", {first_absorption, coefficients(nullptr, {1, 0, 0, -6.6666667e-5f, 0})});
    const auto mie = entries("mie_config");
    set_coefficients("mie_config", {coefficients(mie.empty() ? nullptr : &mie.front(), {0, -8.333333e-4f, 1, 0, 0}, .8f)});
    const auto rayleigh = entries("rayleigh_config");
    set_coefficients("rayleigh_config", {coefficients(rayleigh.empty() ? nullptr : &rayleigh.front(), {0, -1.25e-4f, 1, 0, 0})});
    return frame;
}
Value normalize_environment_cycle(Value cycle) {
    auto frames = cycle.get("frames");
    if (frames.kind == Value::Kind::Map) {
        for (auto& [name, frame] : frames.map) {
            const auto type = osd_string(frame.get("type"));
            if (type == "water")
                frame = normalize_water_frame(frame);
            else if (type == "sky")
                frame = normalize_sky_frame(frame);
        }
        cycle.set("frames", std::move(frames));
    }
    return cycle;
}
Value environment_array(std::vector<Value> items = {}) {
    Value result;
    result.kind = Value::Kind::Array;
    result.array = std::move(items);
    return result;
}
Value windlight_vector4(const Value& value) {
    constexpr std::array<float, 3> zero{};
    auto result = environment_vector(value, zero, true);
    result.array.push_back(environment_real(1));
    return result;
}
Value windlight_scalar(float value) {
    return environment_array({environment_real(value), environment_real(0), environment_real(0), environment_real(1)});
}
Value export_windlight_sky(std::string_view name, const Value& sky) {
    auto result = Value::object({{"name", Value(std::string(name))}});
    const auto& haze = sky.get("legacy_haze");
    for (const auto* field : {"ambient", "blue_density", "blue_horizon"})
        result.set(field, windlight_vector4(haze.get(field)));
    for (const auto* field : {"density_multiplier", "distance_multiplier", "haze_density", "haze_horizon"})
        result.set(field, windlight_scalar(static_cast<float>(osd_real(haze.get(field)))));
    for (const auto* field : {"cloud_color", "cloud_pos_density1", "cloud_pos_density2", "glow"})
        result.set(field, windlight_vector4(sky.get(field)));
    for (const auto* field : {"cloud_scale", "cloud_shadow", "gamma", "max_y"})
        result.set(field, windlight_scalar(static_cast<float>(osd_real(sky.get(field)))));
    constexpr std::array<float, 2> zero{};
    auto scroll = environment_vector(sky.get("cloud_scroll_rate"), zero, true);
    auto enabled = environment_array();
    for (auto& component : scroll.array) {
        enabled.array.emplace_back(component.real != 0);
        component = environment_real(static_cast<float>(component.real) + 10.f);
    }
    result.set("cloud_scroll_rate", std::move(scroll));
    result.set("enable_cloud_scroll", std::move(enabled));
    result.set("star_brightness", environment_real(static_cast<float>(osd_real(sky.get("star_brightness"))) / 250.f));
    result.set("sunlight_color", sky.get("sunlight_color"));

    constexpr std::array<float, 4> identity{0, 0, 0, 1};
    const auto rotation = environment_vector(sky.get("sun_rotation"), identity, true, true);
    const auto x = static_cast<float>(rotation.array[0].real);
    const auto y = static_cast<float>(rotation.array[1].real);
    const auto z = static_cast<float>(rotation.array[2].real);
    const auto w = static_cast<float>(rotation.array[3].real);
    const auto norm = x*x + y*y + z*z + w*w;
    const auto scale = norm > 0 ? 2.f / norm : 0.f;
    auto vx = scale * (x*x + w*w) - 1.f;
    auto vy = scale * (x*y + z*w);
    auto vz = scale * (x*z - y*w);
    const auto length = std::sqrt(vx*vx + vy*vy + vz*vz);
    if (length > 0) {
        vx /= length;
        vy /= length;
        vz /= length;
    }
    const auto angle = [](float radians) {
        if (std::abs(radians) < 1e-6f)
            return 0.f;
        return radians < 0 ? radians + 2.f * std::numbers::pi_v<float> : radians;
    };
    result.set("sun_angle", environment_real(angle(std::asin(std::clamp(vz, -1.f, 1.f)))));
    result.set("east_angle", environment_real(angle(-std::atan2(vy, vx))));
    std::vector<Value> light;
    if (vz >= 0)
        light = {environment_real(vy), environment_real(vz), environment_real(vx)};
    else if (vz > -.12f) {
        const auto factor = 1.f / std::sqrt(vy*vy + vz*vz);
        light = {environment_real(vy * factor), environment_real(0), environment_real(vx * factor)};
    } else
        light = {environment_real(-vy), environment_real(-vz), environment_real(-vx)};
    light.push_back(environment_real(1));
    result.set("lightnorm", environment_array(std::move(light)));
    return result;
}
Value export_windlight_water(const Value& water) {
    auto result = Value::object({});
    if (osd_string(water.get("type")) != "water")
        return result;
    constexpr std::pair<const char*, const char*> fields[] = {
        {"blurMultiplier", "blur_multiplier"}, {"fresnelOffset", "fresnel_offset"},
        {"fresnelScale", "fresnel_scale"}, {"normScale", "normal_scale"},
        {"normalMap", "normal_map"}, {"scaleAbove", "scale_above"}, {"scaleBelow", "scale_below"},
        {"underWaterFogMod", "underwater_fog_mod"}, {"waterFogDensity", "water_fog_density"},
        {"wave1Dir", "wave1_direction"}, {"wave2Dir", "wave2_direction"},
    };
    for (const auto& [legacy, modern] : fields)
        result.set(legacy, water.get(modern));
    result.set("waterFogColor", windlight_vector4(water.get("water_fog_color")));
    return result;
}
std::vector<std::uint8_t> lightshare_payload(const Value& cycle) {
    const auto& tracks = cycle.get("tracks").array;
    const auto& frames = cycle.get("frames");
    const auto frame = [&](std::size_t track) -> const Value& {
        if (track < tracks.size() && !tracks[track].array.empty())
            return frames.get(osd_string(tracks[track].array.front().get("key_name")));
        static const Value empty;
        return empty;
    };
    const auto& water = frame(0);
    const auto& sky = frame(1);
    const bool has_water = osd_string(water.get("type")) == "water";
    const bool has_sky = osd_string(sky.get("type")) == "sky";
    const auto& haze = sky.get("legacy_haze");
    const auto angles = has_sky ? export_windlight_sky("", sky) : Value{};
    const auto number = [](const Value& map, const char* key) {
        return static_cast<float>(osd_real(map.get(key)));
    };
    const auto component = [](const Value& vector, std::size_t index) {
        return index < vector.array.size() ? static_cast<float>(osd_real(vector.array[index])) : 0.f;
    };
    std::vector<std::uint8_t> bytes;
    bytes.reserve(249);
    const auto real = [&](float value) {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::uint8_t>(bits >> shift));
    };
    const auto vector = [&](bool present, const Value& value, std::initializer_list<float> defaults,
                            float factor = 1.f, bool opaque = false) {
        std::size_t i = 0;
        for (const auto fallback : defaults) {
            real(!present ? fallback : opaque && i == 3 ? 1.f : component(value, i) * factor);
            ++i;
        }
    };
    const auto scalar = [&](bool present, const Value& map, const char* key, float fallback, float factor = 1.f) {
        real(present ? number(map, key) * factor : fallback);
    };
    vector(has_water, water.get("water_fog_color"), {4, 38, 64}, 256.f);
    real(has_water ? std::sqrt(number(water, "water_fog_density")) : 4.f);
    scalar(has_water, water, "underwater_fog_mod", .25f);
    vector(has_water, water.get("normal_scale"), {2, 2, 2});
    scalar(has_water, water, "fresnel_scale", .4f);
    scalar(has_water, water, "fresnel_offset", .5f);
    scalar(has_water, water, "scale_above", .03f);
    scalar(has_water, water, "scale_below", .2f);
    scalar(has_water, water, "blur_multiplier", .04f);
    vector(has_water, water.get("wave2_direction"), {1.05f, -.42f});
    vector(has_water, water.get("wave1_direction"), {1.11f, -1.16f});
    const auto normal = has_water ? osd_uuid(water.get("normal_map"))
                                  : Uuid("822ded49-9a6c-f61c-cb89-6df54f42cdf4");
    bytes.insert(bytes.end(), normal.bytes().begin(), normal.bytes().end());
    vector(has_sky, haze.get("blue_horizon"), {.25f, .25f, .32f, .32f}, .5f, true);
    scalar(has_sky, haze, "haze_horizon", .19f);
    vector(has_sky, haze.get("blue_density"), {.12f, .22f, .38f, .38f}, .5f, true);
    scalar(has_sky, haze, "haze_density", .70f);
    scalar(has_sky, haze, "density_multiplier", .18f, 1000.f);
    scalar(has_sky, haze, "distance_multiplier", .8f);
    vector(has_sky, sky.get("sunlight_color"), {.24f, .26f, .30f, .30f}, 1.f / 3.f);
    constexpr auto angle_scale = .5f / std::numbers::pi_v<float>;
    scalar(has_sky, angles, "sun_angle", .317f, angle_scale);
    vector(has_sky, haze.get("ambient"), {.35f, .35f, .35f, .35f}, 1.f / 3.f, true);
    scalar(has_sky, angles, "east_angle", 0.f, angle_scale);
    real(has_sky ? -component(sky.get("glow"), 2) / 5.f : .10f);
    real(has_sky ? (2.f - component(sky.get("glow"), 0)) / 20.f : 1.75f);
    scalar(has_sky, sky, "gamma", 1.f);
    scalar(has_sky, sky, "star_brightness", 0.f, 1.f / 250.f);
    vector(has_sky, sky.get("cloud_color"), {.41f, .41f, .41f, .41f}, 1.f, true);
    vector(has_sky, sky.get("cloud_pos_density1"), {1.f, .53f, 1.f});
    scalar(has_sky, sky, "cloud_shadow", .27f);
    scalar(has_sky, sky, "cloud_scale", .42f);
    vector(has_sky, sky.get("cloud_pos_density2"), {1.f, .53f, .12f});
    const auto scroll_x = has_sky ? component(sky.get("cloud_scroll_rate"), 0) : .20f;
    const auto scroll_y = has_sky ? component(sky.get("cloud_scroll_rate"), 1) : .01f;
    real(scroll_x);
    real(scroll_y);
    const auto altitude = has_sky ? number(sky, "max_y") : 1605.f;
    const auto altitude_bits = std::isfinite(altitude)
        ? static_cast<std::uint16_t>(static_cast<std::int32_t>(std::fmod(altitude, 65536.f))) : 0;
    bytes.push_back(static_cast<std::uint8_t>(altitude_bits));
    bytes.push_back(static_cast<std::uint8_t>(altitude_bits >> 8));
    bytes.push_back(scroll_x == 0);
    bytes.push_back(scroll_y == 0);
    bytes.push_back(1);
    return bytes;
}
Value windlight_sky(const Value& legacy) {
    if (legacy.kind != Value::Kind::Map)
        throw std::invalid_argument("Invalid WindLight sky frame");
    auto sky = Value::object({{"type", Value("sky")}});
    auto haze = Value::object({});
    const auto legacy_vector = [&](const char* name, bool drop_alpha) {
        constexpr std::array<float, 4> zero{};
        auto value = environment_vector(legacy.get(name), zero, true);
        if (drop_alpha)
            value.array.pop_back();
        return value;
    };
    for (const auto* name : {"ambient", "blue_density", "blue_horizon"})
        haze.set(name, legacy_vector(name, true));
    for (const auto* name : {"density_multiplier", "distance_multiplier", "haze_density", "haze_horizon"})
        haze.set(name, legacy_vector(name, false).array.front());
    sky.set("legacy_haze", std::move(haze));
    for (const auto* name : {"cloud_color", "cloud_pos_density1", "cloud_pos_density2", "glow"})
        sky.set(name, legacy_vector(name, true));
    for (const auto* name : {"cloud_scale", "cloud_shadow", "gamma", "max_y"})
        sky.set(name, legacy_vector(name, false).array.front());
    constexpr std::array<float, 2> zero{};
    auto scroll = environment_vector(legacy.get("cloud_scroll_rate"), zero, true);
    const auto enabled = environment_vector(legacy.get("enable_cloud_scroll"), zero, true);
    for (std::size_t i = 0; i < scroll.array.size(); ++i)
        scroll.array[i] = environment_real(enabled.array[i].real != 0 ? scroll.array[i].real - 10.f : 0.f);
    sky.set("cloud_scroll_rate", std::move(scroll));
    sky.set("star_brightness", environment_real(static_cast<float>(osd_real(legacy.get("star_brightness"))) * 250.f));
    sky.set("sunlight_color", legacy_vector("sunlight_color", false));

    const auto rotation = [](float azimuth, float altitude) {
        constexpr float tau = 2.f * std::numbers::pi_v<float>;
        const auto nearly_zero = [](float angle) {
            return std::abs(angle) < .001f || std::abs(std::abs(angle) - tau) < .001f;
        };
        const auto array = [](float x, float y, float z, float w) {
            return environment_array({environment_real(x), environment_real(y), environment_real(z),
                                      environment_real(w)});
        };
        const auto az = azimuth * .5f;
        const auto alt = altitude * .5f;
        if (nearly_zero(altitude))
            return array(0, 0, std::sin(az), std::cos(az));
        if (nearly_zero(azimuth))
            return array(0, -std::sin(alt), 0, std::cos(alt));
        const auto sa = std::sin(az), ca = std::cos(az);
        const auto sl = std::sin(alt), cl = std::cos(alt);
        const auto x = sl * sa, y = -sl * ca, z = cl * sa, w = cl * ca;
        const auto norm = std::sqrt(x*x + y*y + z*z + w*w);
        return array(x / norm, y / norm, z / norm, w / norm);
    };
    const auto azimuth = -static_cast<float>(osd_real(legacy.get("east_angle")));
    const auto altitude = static_cast<float>(osd_real(legacy.get("sun_angle")));
    sky.set("sun_rotation", rotation(azimuth, altitude));
    sky.set("moon_rotation", rotation(azimuth, altitude + std::numbers::pi_v<float>));
    return sky;
}
}
EnvironmentDay EnvironmentDay::from_windlight(const Value& legacy) {
    if (legacy.kind != Value::Kind::Array)
        throw std::invalid_argument("Invalid WindLight settings");
    const auto& parts = legacy.array;
    auto frames = Value::object({});
    if (parts.size() > 2 && parts[2].kind == Value::Kind::Map)
        for (const auto& [name, sky] : parts[2].map)
            frames.set(name, windlight_sky(sky));
    auto water = Value::object({{"type", Value("water")}});
    if (parts.size() > 3 && parts[3].kind == Value::Kind::Map) {
        const auto& original = parts[3];
        constexpr std::pair<const char*, const char*> fields[] = {
            {"blurMultiplier", "blur_multiplier"}, {"fresnelOffset", "fresnel_offset"},
            {"fresnelScale", "fresnel_scale"}, {"normScale", "normal_scale"},
            {"normalMap", "normal_map"}, {"scaleAbove", "scale_above"}, {"scaleBelow", "scale_below"},
            {"underWaterFogMod", "underwater_fog_mod"}, {"waterFogDensity", "water_fog_density"},
            {"wave1Dir", "wave1_direction"}, {"wave2Dir", "wave2_direction"},
        };
        for (const auto& [old_name, new_name] : fields)
            water.set(new_name, original.get(old_name));
        constexpr std::array<float, 4> zero{};
        auto color = environment_vector(original.get("waterFogColor"), zero, true);
        color.array.pop_back();
        water.set("water_fog_color", std::move(color));
    }
    if (!frames.contains("WLWater"))
        frames.set("WLWater", std::move(water));

    std::vector<std::pair<float, std::string>> keys;
    if (parts.size() > 1 && parts[1].kind == Value::Kind::Array) {
        for (const auto& key : parts[1].array) {
            if (key.kind != Value::Kind::Array)
                continue;
            if (key.array.size() < 2)
                throw std::invalid_argument("Invalid WindLight keyframe");
            keys.emplace_back(static_cast<float>(osd_real(key.array[0])), osd_string(key.array[1]));
        }
    }
    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
        if (std::isnan(a.first))
            return !std::isnan(b.first);
        return a.first < b.first;
    });
    auto sky_track = environment_array();
    for (const auto& [time, name] : keys)
        sky_track.array.push_back(Value::object({{"key_keyframe", environment_real(time < 0 ? 0 : time)},
                                                {"key_name", Value(name)}}));
    auto water_track = environment_array({
        Value::object({{"key_keyframe", environment_real(0)}, {"key_name", Value("WLWater")}}),
    });
    auto cycle = Value::object({{"type", Value("daycycle")}, {"name", Value("WLDaycycle")},
                                {"frames", std::move(frames)},
                                {"tracks", environment_array({std::move(water_track), std::move(sky_track),
                                    environment_array(), environment_array(), environment_array()})}});
    EnvironmentDay day;
    day.apply(Value::object({{"day_cycle", std::move(cycle)}, {"env_version", Value(std::int32_t{0})}}));
    return day;
}
void EnvironmentDay::apply(Value update, bool normalize_cycle) {
    auto merged = settings && settings->kind == Value::Kind::Map ? *settings : Value::object({});
    for (const auto* name : {"day_cycle", "day_length", "day_offset", "flags", "env_version", "track_altitudes"})
        if (update.contains(name)) {
            auto value = update.get(name);
            if (normalize_cycle && std::string_view(name) == "day_cycle")
                value = normalize_environment_cycle(std::move(value));
            merged.set(name, std::move(value));
        }
    if (!update.contains("env_version")) {
        const auto version = static_cast<std::uint32_t>(osd_integer(merged.get("env_version")));
        merged.set("env_version", Value(std::bit_cast<std::int32_t>(version + 1)));
    }
    if (update.contains("day_length"))
        length = osd_integer(update.get("day_length"));
    if (update.contains("day_offset"))
        offset = osd_integer(update.get("day_offset"));
    settings = std::make_shared<const Value>(std::move(merged));
}
namespace {
std::optional<std::string> simulator_data(const GridRegion& region, std::int32_t selector, bool local) {
    switch (selector) {
    case 5:
        if (local)
            return script::lsl_vector_string({double(static_cast<std::uint32_t>(region.x) / 256),
                                              double(static_cast<std::uint32_t>(region.y) / 256), 0.});
        if (region.flags.value_or(0) & RegionFlags::Hyperlink) {
            std::uint64_t handle = 0;
            if (region.region_secret && !try_parse_unsigned_integer(*region.region_secret, handle))
                return {};
            return script::lsl_vector_string({double(handle >> 32),
                                              double(static_cast<std::uint32_t>(handle)), 0.});
        }
        return script::lsl_vector_string({double(region.x), double(region.y), 0.});
    case 6:
        return "up";
    case 7:
        switch (region.maturity) {
        case 0: return "PG";
        case 1: return "MATURE";
        case 2: return "ADULT";
        default: return "UNKNOWN";
        }
    case 128:
        return "OpenSim";
    default:
        return local ? std::nullopt : std::optional<std::string>("unknown");
    }
}

using NotecardLines = std::shared_ptr<const std::vector<std::string>>;

NotecardLines cached_notecard(Uuid id, NotecardLines loaded = {}) {
    using Clock = std::chrono::steady_clock;
    struct Entry {
        NotecardLines lines;
        Clock::time_point expires;
    };
    static std::mutex mutex;
    static std::map<Uuid, Entry> cache;
    std::lock_guard lock(mutex);
    const auto now = Clock::now();
    if (auto found = cache.find(id); found != cache.end() && found->second.expires > now) {
        found->second.expires = now + std::chrono::seconds(30);
        return found->second.lines;
    }
    if (loaded) {
        std::erase_if(cache, [now](const auto& entry) { return entry.second.expires <= now; });
        cache.emplace(id, Entry{loaded, now + std::chrono::seconds(30)});
    }
    return loaded;
}

std::string notecard_reply(const std::vector<std::string>& lines, std::optional<std::int32_t> line,
                           std::int32_t max_chars) {
    if (!line)
        return std::to_string(lines.size());
    if (max_chars < 0)
        throw std::out_of_range("NotecardLineReadCharsMax");
    if (*line < 0)
        return {};
    const auto index = static_cast<std::size_t>(*line);
    return truncate_utf16(index < lines.size() ? std::string_view(lines[index]) : "\n\n\n", max_chars);
}

float terrain_noise(int px, int py, std::uint32_t width, std::uint32_t height) {
    const auto noise = [](int x, int y) {
        auto n = static_cast<std::uint32_t>(x + y * 749);
        n = (n << 13) ^ n;
        return 1.0 - double((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu) / 1073741824.0;
    };
    const auto smooth = [&](int x, int y) {
        const double corners =
            (noise(x - 1, y - 1) + noise(x + 1, y - 1) + noise(x - 1, y + 1) + noise(x + 1, y + 1)) / 16;
        const double sides = (noise(x - 1, y) + noise(x + 1, y) + noise(x, y - 1) + noise(x, y + 1)) / 8;
        return corners + sides + noise(x, y) / 4;
    };
    const auto interpolate = [](double a, double b, double blend) { return a * (1.0 - blend) + b * blend; };
    double total = 0;
    for (int frequency = 1; frequency <= 128; frequency *= 2) {
        const double x = (double(px) / width) * frequency, y = (double(py) / height) * frequency;
        const int ix = static_cast<int>(x), iy = static_cast<int>(y);
        const auto bottom = interpolate(smooth(ix, iy), smooth(ix + 1, iy), x - ix);
        const auto top = interpolate(smooth(ix, iy + 1), smooth(ix + 1, iy + 1), x - ix);
        total += interpolate(bottom, top, y - iy);
    }
    return static_cast<float>(total);
}
class Actor final : public AdmissionPresence {
  public:
    Uuid id;
    std::atomic<bool> root = false;
    std::atomic<AdmissionPresenceState> status = AdmissionPresenceState::running;
    explicit Actor(Uuid value) : id(value) {}
    Uuid agent_id() const override {
        return id;
    }
    bool deleted() const override {
        return status == AdmissionPresenceState::removed;
    }
    bool child() const override {
        return !root;
    }
    AdmissionPresenceState state() const override {
        return status;
    }
    void state(AdmissionPresenceState value) override {
        status = value;
    }
    void do_not_close_after_teleport(bool) override {}
    bool has_client() const override {
        return true;
    }
    void teleport_flags(std::uint32_t) override {}
    void adjust_known_seeds() override {}
};
}
struct RegionRuntime::State : SceneAdmissionBackend,
                              SceneCapabilityBackend,
                              SceneClientVerificationBackend,
                              std::enable_shared_from_this<RegionRuntime::State> {
    using SceneClientVerificationBackend::remove_circuit;
    GridRegion descriptor;
    IPresenceService& sessions;
    RegionPolicies policies;
    AccessRegion access_region;
    World world;
    mutable AgentCircuitManager circuits;
    std::unique_ptr<UdpSocket> socket;
    std::optional<Terrain> terrain_baseline;
    std::mutex terrain_edit_mutex;
    std::mutex terrain_texture_mutex;
    ViewerRegionOptions::TerrainTextureHeights terrain_texture_heights;
    std::array<Uuid, 4> terrain_texture_assets = ViewerRegionOptions{}.terrain_details;
    std::array<Uuid, 4> terrain_pbr_materials;
    float terrain_lower_limit = -100.f, terrain_raise_limit = 100.f;
    std::atomic<int> region_agent_limit{40};
    std::atomic<float> region_object_bonus{1.f};
    std::atomic<std::int32_t> region_maturity{descriptor.maturity};
    std::atomic<std::uint8_t> region_access{descriptor.access};
    std::mutex estate_region_settings_mutex;
    Uuid estate_covenant;
    std::uint32_t estate_covenant_timestamp = 0;
    std::mutex estate_covenant_mutex;
    std::optional<std::chrono::steady_clock::time_point> estate_region_change_due;
    mutable std::mutex estate_access_mutex;
    std::atomic<bool> block_terraform{false};
    std::atomic<std::uint32_t> region_setting_flags{0x4000000};
    bool use_estate_sun = true;
    EnvironmentDay environment_day;
    std::atomic<bool> environment_time_refresh{true};
    std::chrono::steady_clock::time_point next_environment_time;
    mutable std::mutex environment_mutex;
    std::map<Uuid, std::optional<EnvironmentDay>> parcel_environment_updates;
    std::map<Uuid, std::pair<Vector3d, Vector3d>> initial_parcel_bounds;
    std::map<Uuid, EnvironmentDay> agent_environment_overrides;
    Value default_environment_cycle;
    Uuid region_telehub;
    std::vector<RegionSpawnPoint> region_spawn_points;
    std::mutex telehub_mutex;
    std::size_t next_spawn_point = 0;
    std::uint32_t region_flags() const {
        return region_setting_flags.load() | (block_terraform ? 0x40u : 0u);
    }
    void activate_physics() {
        if (viewer_physics.load() || policies.physics_library.empty())
            return;
        physics_activation_attempted = true;
        const auto weak = weak_from_this();
        auto physics = std::make_shared<ViewerPhysics>(
            world, *viewers, policies.physics_library, policies.animations,
            [weak](const ViewerAgent& a) {
                if (auto v = weak.lock())
                    for (auto& recipient : v->viewers->agents())
                        if (recipient.root)
                            v->send_avatar(a, recipient);
            },
            [weak](const std::string& error) {
                if (auto v = weak.lock())
                    v->error(error);
            },
            water_height,
            [weak](const ViewerAgent& avatar, const ViewerAgent& recipient) {
                const auto state = weak.lock();
                return state && state->can_see_avatar(avatar, recipient);
            });
        viewer_physics.store(physics);
        for (const auto& avatar : viewers->agents())
            if (avatar.root)
                physics->entered(physics_agent(avatar));
    }
    void update_maturity(std::int32_t maturity) {
        region_maturity = maturity;
        region_access = maturity == 1 ? 21 : maturity == 2 ? 42 : 13;
    }
    bool scripts_enabled() const {
        return (region_flags() & 0x2000u) == 0;
    }
    ViewerAgent physics_agent(ViewerAgent agent) const {
        constexpr std::uint32_t block_fly = 0x80000u, control_fly = 0x2000u;
        if (region_flags() & block_fly) {
            agent.controls &= ~control_fly;
            agent.flying = false;
        }
        return agent;
    }
    std::atomic<float> water_height{20.f};
    struct TerrainUpload {
        Uuid session;
        std::uint64_t id;
        std::string filename;
        std::vector<std::uint8_t> data;
        std::uint32_t size = 0, next = 0;
    };
    std::mutex terrain_upload_mutex;
    std::optional<TerrainUpload> terrain_upload;
    struct TerrainUploadReceipt {
        Uuid session;
        std::uint64_t id, packet;
    };
    std::optional<TerrainUploadReceipt> terrain_upload_receipt;
    std::atomic<std::uint64_t> next_upload_xfer = 0;
    void send_upload_packet(const ViewerAgent& a, lludp::Packet packet) {
        packet.zerocoded = true;
        viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id, std::move(packet),
                                                ThrottleOutPacketType::Asset);
    }
    void start_terrain_upload(const ViewerAgent& a, std::string filename) {
        std::lock_guard lock(terrain_upload_mutex);
        if (terrain_upload) {
            alert(a.circuit->agent_id, "Another Terrain Upload is in progress.  Please wait your turn!");
            return;
        }
        const auto id = ++next_upload_xfer;
        terrain_upload = TerrainUpload{a.circuit->session_id, id, filename, {}};
        auto packet = lludp::make_packet("RequestXfer");
        auto& fields = packet.block("XferID");
        fields["ID"] = id;
        std::vector<std::uint8_t> name(filename.begin(), filename.end());
        if (!name.empty())
            name.push_back(0);
        fields["Filename"] = std::move(name);
        send_upload_packet(a, std::move(packet));
    }
    void confirm_upload_packet(const ViewerAgent& a, std::uint64_t id, std::uint64_t number) {
        auto ack = lludp::make_packet("ConfirmXferPacket");
        ack.block("XferID")["ID"] = id;
        ack.block("XferID")["Packet"] = number;
        send_upload_packet(a, std::move(ack));
    }
    bool receive_terrain_upload(const ViewerAgent& a, const lludp::Packet& packet) {
        std::unique_lock lock(terrain_upload_mutex);
        const auto& fields = packet.block("XferID");
        const auto id = std::get<std::uint64_t>(fields.at("ID"));
        if (packet.spec->name == "SendXferPacket" && terrain_upload_receipt &&
            terrain_upload_receipt->id == id && terrain_upload_receipt->session == a.circuit->session_id &&
            terrain_upload_receipt->packet == std::get<std::uint64_t>(fields.at("Packet"))) {
            confirm_upload_packet(a, id, terrain_upload_receipt->packet);
            return true;
        }
        if (!terrain_upload || terrain_upload->id != id || terrain_upload->session != a.circuit->session_id)
            return false;
        if (packet.spec->name == "AbortXfer") {
            terrain_upload.reset();
            alert(a.circuit->agent_id, "Terrain Upload aborted by the client");
            return true;
        }
        auto& upload = *terrain_upload;
        const auto number = std::get<std::uint64_t>(fields.at("Packet"));
        const auto index = number & 0x7fffffffu;
        if (index > upload.next)
            return true;
        if (index < upload.next) {
            confirm_upload_packet(a, id, index);
            return true;
        }
        const bool last = (number & 0x80000000u) != 0;
        if (index == upload.next) {
            const auto& bytes = std::get<std::vector<std::uint8_t>>(packet.block("DataPacket").at("Data"));
            const std::size_t prefix = index == 0 ? 4 : 0;
            if (bytes.size() < prefix) {
                terrain_upload.reset();
                return true;
            }
            if (prefix)
                for (unsigned i = 0; i < 4; ++i)
                    upload.size |= std::uint32_t(bytes[i]) << (8 * i);
            const auto received = upload.data.size() + bytes.size() - prefix;
            const auto& terrain = world.terrain();
            constexpr std::size_t base_upload_limit = 64u * 1024 * 1024;
            constexpr std::size_t llraw_bytes_per_sample = 13;
            const auto region_llraw_size =
                std::size_t(terrain.width()) * terrain.height() * llraw_bytes_per_sample;
            const auto upload_limit = upload.filename.ends_with(".raw")
                ? std::max(base_upload_limit, region_llraw_size) : base_upload_limit;
            if (upload.size > upload_limit || received > upload.size ||
                (last && received != upload.size)) {
                terrain_upload.reset();
                return true;
            }
            upload.data.insert(upload.data.end(), bytes.begin() + prefix, bytes.end());
            ++upload.next;
        }
        confirm_upload_packet(a, id, number);
        if (!last || upload.data.size() != upload.size)
            return true;
        auto complete = std::move(upload);
        terrain_upload_receipt = TerrainUploadReceipt{a.circuit->session_id, id, number};
        terrain_upload.reset();
        lock.unlock();
        alert(a.circuit->agent_id, "Terrain Upload Complete. Loading....");
        try {
            load_terrain_file(complete.filename, complete.data, false);
            const auto extension = complete.filename.substr(complete.filename.rfind('.'));
            alert(a.circuit->agent_id, "Your terrain was loaded as a " + extension +
                                           " file. It may take a few moments to appear.");
        } catch (const std::ios_base::failure& error) {
            if (policies.log)
                policies.log(error.what());
            alert(a.circuit->agent_id,
                  "There was an IO Exception loading your terrain.  Please check free space.");
        } catch (const std::exception& error) {
            if (policies.log)
                policies.log(error.what());
            alert(
                a.circuit->agent_id,
                "There was a general error loading your terrain.  Please fix the terrain file and try again");
        }
        return true;
    }
    void bake_terrain() {
        std::lock_guard lock(terrain_edit_mutex);
        update_baked_terrain();
    }
    void load_terrain_file(std::string_view filename, std::span<const std::uint8_t> data, bool exact_size) {
        std::lock_guard edit(terrain_edit_mutex);
        auto& terrain = world.terrain();
        Terrain imported(terrain);
        if (filename.ends_with(".ter"))
            imported.merge_terragen(data, exact_size);
        else if (filename.ends_with(".bmp"))
            imported.merge_bitmap(data, exact_size);
        else if (filename.ends_with(".png"))
            imported.merge_png(data, exact_size);
        else if (filename.ends_with(".r32") || filename.ends_with(".f32"))
            imported.merge_raw(data, true, exact_size);
        else if (filename.ends_with(".raw"))
            imported.merge_raw(data, false, exact_size);
        else
            throw std::runtime_error("Unsupported terrain file format");
        if (policies.persist_baked_terrain)
            policies.persist_baked_terrain(imported);
        terrain_baseline.emplace(imported);
        terrain.replace_heights(imported);
    }
    void update_baked_terrain() {
        const Terrain snapshot(world.terrain());
        if (policies.persist_baked_terrain)
            policies.persist_baked_terrain(snapshot);
        terrain_baseline.emplace(snapshot);
    }
    std::unique_ptr<ViewerRegion> viewers;
    std::unique_ptr<SceneCapabilities> capability_runtime;
    std::atomic<std::shared_ptr<ViewerPhysics>> viewer_physics;
    bool physics_activation_attempted = false;
    std::unique_ptr<BasicProfileRuntime> basic_profiles;
    std::unique_ptr<LegacyInventoryRequests> legacy_inventory;
    struct InventoryCommand {
        ViewerAgent agent;
        std::vector<LegacyItemRequest> items;
        LegacyFolderRequest folder;
    };
    std::mutex inventory_mutex;
    std::deque<InventoryCommand> inventory_items, inventory_folders;
    bool inventory_items_running = false, inventory_folders_running = false;
    std::chrono::steady_clock::time_point next_inventory_folder{};
    std::shared_ptr<server::RegistryCapabilityListener> registry;
    std::shared_ptr<server::RegistryCapabilityListener> secure_script_registry;
    std::shared_ptr<server::RegistryCapabilityListener> plain_script_registry;
    SceneAdmission admission;
    SceneClientVerification verification;
    std::recursive_mutex remove_mutex, new_mutex;
    std::mutex actors_mutex;
    std::map<Uuid, std::shared_ptr<Actor>> actors;
    std::atomic<bool> running = false;
    struct ObjectState {
        std::uint32_t crc = 0;
        bool selected = false;
    };
    std::mutex objects_mutex;
    std::map<Uuid, ObjectState> object_state;
    struct TextureSequenceState {
        std::chrono::steady_clock::time_point last_packet;
        std::map<std::uint32_t, std::uint32_t> by_object;
    };
    std::mutex texture_sequences_mutex;
    std::map<Uuid, TextureSequenceState> texture_sequences;
    struct TextureDownload {
        Uuid image;
        AssetBytes bytes;
        std::uint32_t sequence;
        std::uint32_t next_packet;
        float priority;
        std::int64_t discard_level;
        bool sent_header = false;
        bool ready = false;
        std::uint64_t fetch_id = 0;
        std::vector<std::int32_t> layer_ends;
    };
    std::mutex texture_downloads_mutex;
    std::map<Uuid, std::vector<TextureDownload>> texture_downloads;
    std::uint64_t next_texture_fetch = 0;
    struct ScriptCommand {
        Uuid actor, object;
        std::optional<services::InventoryItem> item;
        std::string event;
        script::LslDetector detector;
    };
    struct PermissionQuestion {
        Uuid agent, session;
        std::int32_t mask;
    };
    struct RunningScript {
        services::InventoryItem item;
        std::unique_ptr<script::LslInstance> instance;
        std::shared_ptr<framework::TaskInventoryItem> task;
        bool deferred_default_rez = false;
        std::optional<PermissionQuestion> question;
        double cpu_ms = 0.;
    };
    struct LandStatsRequest {
        Uuid agent, session;
        std::uint32_t type, flags;
        std::int32_t parcel;
        std::string filter;
    };
    std::vector<std::pair<Uuid, Uuid>> restored_scripts;
    std::mutex commands_mutex;
    std::vector<ScriptCommand> script_commands;
    std::vector<LandStatsRequest> land_stats_requests;
    struct PendingTeleport {
        Uuid agent, session;
        GridRegion destination;
        Uuid token;
        std::chrono::steady_clock::time_point expires;
        bool confirmed = false;
        bool requires_callback = false;
        bool estate_ejection = false;
    };
    std::mutex teleport_mutex;
    std::chrono::steady_clock::time_point next_terrain_edit{};
    std::vector<PendingTeleport> pending_teleports;
    std::set<Uuid> departing_foreign_sessions;
    std::vector<std::pair<std::shared_ptr<AgentCircuitData>, std::chrono::steady_clock::time_point>>
        incoming_teleports;
    struct ArrivalCallback {
        std::shared_ptr<AgentCircuitData> circuit;
        std::string prefix, uri;
    };
    std::map<Uuid, ArrivalCallback> arrival_callbacks;
    // Instances and task-script metadata are owned by the host poll thread.
    std::map<Uuid, RunningScript> scripts;
    struct DataserverRequest {
        Uuid part, item, target, query;
        script::LslHost::NameFormatter format;
        std::optional<script::LslAvatarName> avatar_name;
        std::function<std::optional<std::string>()> deferred_lookup;
        std::optional<std::string> landmark_name;
    };
    std::deque<DataserverRequest> dataserver_requests;
    struct DataserverLookup {
        DataserverRequest request;
        std::future<std::optional<std::string>> result;
    };
    std::optional<DataserverLookup> dataserver_lookup;
    struct TaskPart {
        std::unique_ptr<TaskInventoryRegistry> inventory;
        std::unique_ptr<TaskInventoryFile> file;
        std::unique_ptr<PartScriptEvents> events;
        bool scripted = false;
    };
    struct TaskCommand {
        ViewerAgent actor;
        lludp::Packet packet;
        std::int32_t creation_date = 0;
    };
    mutable std::recursive_mutex task_data_mutex;
    std::mutex appearance_mutex;
    std::mutex preferences_mutex;
    std::map<std::pair<Uuid, Uuid>, std::chrono::steady_clock::time_point> rebake_deadlines;
    struct ScriptUpdateJob {
        Uuid agent, item, object;
        bool running;
        AssetBytes data;
        std::promise<std::vector<std::string>> result;
    };
    struct InventoryAssetJob {
        Uuid agent, item, object;
        AssetBytes data;
        std::promise<Uuid> result;
    };
    std::deque<std::shared_ptr<InventoryAssetJob>> inventory_asset_jobs;
    std::atomic<std::int32_t> active_scripts{0};
    struct ScriptUrl {
        Uuid item;
        Uuid group;
        Uuid part;
        std::string address;
        bool secure;
        bool allow_xss;
    };
    struct ScriptHttpRequest {
        Uuid url;
        Uuid part;
        std::map<std::string, std::string> headers;
        std::optional<std::chrono::steady_clock::time_point> deadline;
        std::optional<int> status;
        std::string response;
        std::string content_type = "text/plain";
        bool secure = false;
        bool allow_xss = false;
    };
    std::mutex script_http_mutex;
    std::map<Uuid, ScriptUrl> script_urls;
    std::map<Uuid, ScriptHttpRequest> script_http_requests;
    std::deque<std::pair<Uuid, script::LslEvent>> script_http_events;
    std::string script_url_error;
    static constexpr auto script_http_timeout = std::chrono::seconds(25);

    void revoke_script_urls(Uuid item, std::optional<std::string_view> address = {},
                            bool discard_pending_events = true) {
        if (address)
            while (address->ends_with('/'))
                address->remove_suffix(1);
        std::lock_guard lock(script_http_mutex);
        std::erase_if(script_urls, [&](const auto& entry) {
            if (address ? entry.second.address != *address : entry.second.item != item)
                return false;
            script_url_listener(entry.second.secure)->routes.remove_poll(
                script_url_prefix(entry.second.secure) + entry.first.str());
            return true;
        });
        for (auto request = script_http_requests.begin(); request != script_http_requests.end();) {
            if (request->second.deadline && !script_urls.contains(request->second.url))
                request = script_http_requests.erase(request);
            else
                ++request;
        }
        if (!address && discard_pending_events)
            std::erase_if(script_http_events, [&](const auto& event) { return event.first == item; });
    }

    Uuid request_script_url(Uuid part, Uuid item, bool secure, bool allow_xss) {
        const auto id = Uuid::random();
        const auto object = world.object(part);
        const auto root = object && object->parent_id && !object->is_attachment
                              ? world.object(object->parent_id) : object;
        std::lock_guard lock(script_http_mutex);
        const auto listener = script_url_listener(secure);
        std::optional<std::string> reason;
        if (!script_url_error.empty())
            reason = script_url_error;
        else if (!listener)
            reason = secure ? "" : "HTTP server not available";
        else if (!root || std::ssize(script_urls) >= policies.script_url_limit)
            reason = "Too many URLs already open";
        if (reason) {
            script_http_events.emplace_back(item, script::LslEvent{
                "http_request", {id.str(), std::string("URL_REQUEST_DENIED"), std::move(*reason)}, {}});
            return id;
        }
        const auto& host = *policies.script_url_host;
        const auto address = std::string(secure ? "https://" : "http://") +
                             (host.find(':') == host.npos || host.starts_with('[') ? host : "[" + host + "]") +
                             ":" + std::to_string(listener->port()) +
                             script_url_prefix(secure) + id.str();
        script_urls.emplace(id, ScriptUrl{item, root->id, part, address, secure, allow_xss});
        register_script_http_route(*listener, id);
        script_http_events.emplace_back(item, script::LslEvent{
            "http_request", {id.str(), std::string("URL_REQUEST_GRANTED"), address + "/"}, {}});
        return id;
    }

    server::RegistryCapabilityListener* script_url_listener(bool secure) const {
        if (secure && secure_script_registry)
            return secure_script_registry.get();
        if (!secure && plain_script_registry)
            return plain_script_registry.get();
        if (secure && plain_script_registry)
            return nullptr;
        return registry && registry->use_ssl() == secure ? registry.get() : nullptr;
    }

    static std::string script_url_prefix(bool secure) {
        return secure ? "/lslhttps/" : "/lslhttp/";
    }

    void register_script_http_route(server::RegistryCapabilityListener& listener, Uuid url_id) {
        const bool secure = listener.use_ssl();
        const auto prefix = script_url_prefix(secure);
        const auto weak = weak_from_this();
        auto handler = std::make_shared<services::CapabilityPollHandler>(
            [weak, prefix, secure](Uuid request_id, const network::HttpRequest& incoming) -> server::PollResponsePtr {
                const auto state = weak.lock();
                if (!state)
                    return {};
                const auto error_response = [&](int status) {
                    auto response = server::PollHttpService::response(incoming);
                    response->status_code(status);
                    return response;
                };
                const auto& target = incoming.raw_target.empty() ? incoming.path : incoming.raw_target;
                // UrlModule uses Uri.AbsolutePath for RawUrl and parses Uri.Query separately.
                const auto uri = network::canonical_service_request_target(target);
                if (!uri)
                    return error_response(400);
                const auto& [path, query] = *uri;
                const auto url_end = prefix.size() + 36;
                Uuid requested_url;
                if (path.size() < url_end ||
                    !framework::try_parse_sdk_uuid(
                        std::string_view(path).substr(prefix.size(), 36), requested_url))
                    return error_response(400);
                std::lock_guard lock(state->script_http_mutex);
                const auto url = state->script_urls.find(requested_url);
                if (!path.starts_with(prefix) || url == state->script_urls.end() ||
                    (path.size() > url_end && path[url_end] != '/'))
                    return error_response(404);

                auto& request = state->script_http_requests[request_id];
                request.url = requested_url;
                request.part = url->second.part;
                request.allow_xss = url->second.allow_xss;
                request.secure = secure;
                request.deadline = std::chrono::steady_clock::now() + script_http_timeout;
                request.headers = incoming.headers;
                // The .NET HTTP parser consumes these fields without adding
                // them to the collection exposed by llGetHTTPHeader.
                for (const auto* name : {"host", "connection", "content-length", "accept",
                                         "http_x_requested_with", "x-requested-with", "remote_addr", "forwarded"})
                    request.headers.erase(name);
                request.headers["x-script-url"] = url->second.address;
                request.headers["x-remote-ip"] = incoming.remote_ip;
                request.headers["x-path-info"] = path.substr(url_end);
                request.headers["x-query-string"] = query.empty() ? std::string{}
                    : network::service_query_header(std::string_view(query).substr(1));
                auto method = incoming.method;
                for (auto& character : method)
                    if (character >= 'a' && character <= 'z')
                        character -= 'a' - 'A';
                state->script_http_events.emplace_back(url->second.item, script::LslEvent{
                    "http_request", {request_id.str(), std::move(method), network::read_service_body(incoming.body)}, {}});
                return {};
            }, prefix + url_id.str(),
            [weak](Uuid id, Uuid) {
                if (const auto state = weak.lock()) {
                    std::lock_guard lock(state->script_http_mutex);
                    const auto request = state->script_http_requests.find(id);
                    return request != state->script_http_requests.end() &&
                           (request->second.status ||
                            (request->second.deadline && std::chrono::steady_clock::now() > *request->second.deadline));
                }
                return false;
            },
            [weak](Uuid id, Uuid) { return finish_script_http(weak, id, false); },
            [weak](Uuid id, Uuid) { return finish_script_http(weak, id, true); },
            [weak](Uuid id, Uuid) {
                if (const auto state = weak.lock()) {
                    std::lock_guard lock(state->script_http_mutex);
                    state->script_http_requests.erase(id);
                }
            }, Uuid{}, static_cast<std::int32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(script_http_timeout).count()));
        listener.routes.add_poll_variable(std::move(handler));
    }

    static server::PollTablePtr finish_script_http(std::weak_ptr<State> weak, Uuid id, bool timed_out) {
        auto result = std::make_shared<server::PollTable>();
        if (const auto state = weak.lock()) {
            auto request = [&] {
                std::lock_guard lock(state->script_http_mutex);
                return state->script_http_requests.extract(id);
            }();
            if (!request.empty()) {
                auto& data = request.mapped();
                const auto now = std::chrono::steady_clock::now();
                const bool expired = data.deadline &&
                    (timed_out ? now >= *data.deadline : now > *data.deadline);
                if (expired) {
                    result->set("int_response_code", 500);
                    result->set("str_response_string", std::string("Script timeout"));
                    result->set("content_type", std::string("text/plain"));
                    result->set("keepalive", false);
                    return result;
                }
                if (timed_out || !data.status)
                    return result;
                result->set("int_response_code", *data.status);
                result->set("str_response_string", std::move(data.response));
                result->set("content_type", std::move(data.content_type));
                result->set("keepalive", false);
                result->set("headers", state->script_http_response_headers(data.part, data.secure));
                if (data.allow_xss)
                    result->set("access_control_allow_origin", std::string("*"));
            }
        }
        return result;
    }

    static std::string script_http_coordinates(std::initializer_list<float> components) {
        std::string result = "(";
        for (const float component : components) {
            if (result.size() > 1)
                result += ", ";
            char digits[64];
            const auto end = std::to_chars(digits, digits + sizeof digits, component == 0 ? 0.f : component,
                                           std::chars_format::fixed, 6);
            result.append(digits, end.ptr);
        }
        return result + ")";
    }

    static std::string script_http_legacy_html(std::string_view body) {
        const auto units = framework::utf16_replacing(body);
        std::u16string html = u"<html>";
        for (std::size_t i = 0; i < units.size(); ++i) {
            std::uint32_t code = units[i];
            switch (code) {
            case '<': html += u"&lt;"; continue;
            case '>': html += u"&gt;"; continue;
            case '&': html += u"&amp;"; continue;
            case '"': html += u"&quot;"; continue;
            case '\'': html += u"&#39;"; continue;
            }
            if (code >= 0xd800 && code <= 0xdbff && i + 1 < units.size() &&
                units[i + 1] >= 0xdc00 && units[i + 1] <= 0xdfff)
                code = 0x10000 + ((code - 0xd800) << 10) + (units[++i] - 0xdc00);
            if ((code >= 160 && code <= 255) || code > 0xffff) {
                html += u"&#";
                const auto digits = std::to_string(code);
                html.append(digits.begin(), digits.end());
                html += u';';
            } else {
                html += static_cast<char16_t>(code);
            }
        }
        html += u"</html>";
        return framework::utf8(html);
    }

    server::PollTablePtr script_http_response_headers(Uuid part, bool secure) const {
        auto headers = std::make_shared<server::PollTable>();
        if (const auto object = world.object(part)) {
            const auto position = script_world_position(*object);
            const auto velocity = object->velocity;
            auto rotation = object->rotation;
            if (!object->is_attachment && object->parent_id)
                if (const auto root = world.object(object->parent_id))
                    rotation = framework::sdk_multiply_quaternion(root->rotation, rotation);
            headers->set("X-SecondLife-Object-Name", object->name);
            headers->set("X-SecondLife-Object-Key", object->id.str());
            headers->set("X-SecondLife-Owner-Key", object->owner.str());
            headers->set("X-SecondLife-Region", descriptor.name.value_or("") + " (" +
                         std::to_string(descriptor.x) + ", " + std::to_string(descriptor.y) + ")");
            headers->set("X-SecondLife-Local-Position",
                         script_http_coordinates({position.x, position.y, position.z}));
            headers->set("X-SecondLife-Local-Velocity",
                         script_http_coordinates({velocity.x, velocity.y, velocity.z}));
            headers->set("X-SecondLife-Local-Rotation",
                         script_http_coordinates({rotation.x, rotation.y, rotation.z, rotation.w}));
        }
        if (!framework::trim_utf8(policies.script_http_shard).empty())
            headers->set("X-SecondLife-Shard", policies.script_http_shard);
        if (!framework::trim_utf8(policies.script_http_user_agent).empty())
            headers->set("User-Agent", policies.script_http_user_agent);
        if (secure)
            headers->set("Accept-CH", std::string("UA"));
        return headers;
    }

    void deliver_script_http_events() {
        std::deque<std::pair<Uuid, script::LslEvent>> events;
        {
            std::lock_guard lock(script_http_mutex);
            events.swap(script_http_events);
        }
        std::lock_guard lock(task_data_mutex);
        for (auto& [item, event] : events) {
            const auto script = scripts.find(item);
            if (script != scripts.end() && script->second.instance)
                script->second.instance->post_event(std::move(event));
        }
    }

    std::string script_ossl_permission(Uuid part, Uuid item_id, std::string_view function,
                                       bool check_global_switch = true) {
        if (check_global_switch && !policies.script_ossl_enabled)
            return "All unsafe OSSL funtions disabled";
        const std::string name(function);
        const auto configured = policies.script_ossl_rules.find(name);
        const auto owners = configured == policies.script_ossl_rules.end()
            ? std::string{} : framework::trim_utf8(configured->second.owners);
        const auto creators = configured == policies.script_ossl_rules.end()
            ? std::string{} : framework::trim_utf8(configured->second.creators);
        if (owners.empty() && creators.empty()) {
            constexpr std::array<std::string_view, 8> levels{
                "NoAccess", "None", "VeryLow", "Low", "Moderate", "High", "VeryHigh", "Severe",
            };
            auto maximum = std::find(levels.begin(), levels.end(), policies.script_ossl_threat_level);
            if (maximum == levels.end())
                maximum = levels.begin() + 2;
            std::string_view function_level = "Moderate";
            if (function == "osForceOtherSit" || function == "osAvatarPlayAnimation" ||
                function == "osAvatarStopAnimation")
                function_level = "VeryHigh";
            else if (function == "osGetAgents" || function == "osSunGetParam")
                function_level = "None";
            else if (function == "osGetAgentIP")
                function_level = "Severe";
            else if (function == "osSetRegionWaterHeight" || function == "osSetRegionSunSettings")
                function_level = "High";
            const auto required = std::find(levels.begin(), levels.end(), function_level);
            if (maximum >= required)
                return {};
            return name + " permission denied.  Allowed threat level is " + std::string(*maximum) +
                   " but function threat level is " + std::string(*required);
        }
        const auto normalized = framework::utf8(framework::invariant_lower_utf16(framework::utf16(owners)));
        if (normalized == "true")
            return {};
        const auto disabled = name + " disabled in region configuration";
        if (normalized == "false")
            return disabled;
        const auto object = world.object(part);
        const auto script = scripts.find(item_id);
        if (!object || script == scripts.end())
            return name + " permission denied";
        const auto item = script->second.task ? service_item(*script->second.task) : script->second.item;
        const auto matches = [](std::string_view entries, const auto& predicate) {
            while (!entries.empty()) {
                const auto comma = entries.find(',');
                if (predicate(framework::trim_utf8(entries.substr(0, comma))))
                    return true;
                if (comma == entries.npos)
                    break;
                entries.remove_prefix(comma + 1);
            }
            return false;
        };
        bool valid_rule = false;
        if (matches(normalized, [&](const auto& entry) {
            Uuid owner;
            if (framework::try_parse_sdk_uuid(entry, owner) && !owner.is_zero()) {
                valid_rule = true;
                return owner == object->owner;
            }
            if (entry == "parcel_owner" || entry == "parcel_group_member") {
                valid_rule = true;
                const auto parcel = parcel_at_truncated_position(script_world_position(*object));
                return parcel && (entry == "parcel_owner" ? parcel->owner == item.owner
                    : !parcel->group.is_zero() && parcel->group == item.group_id);
            }
            if (entry == "estate_owner" || entry == "estate_manager") {
                valid_rule = true;
                return entry == "estate_owner" ? item.owner == descriptor.estate_owner
                                               : is_estate_manager(item.owner);
            }
            if (entry == "active_god") {
                valid_rule = true;
                const auto viewer = viewers->agent(item.owner);
                return viewer && viewer_is_god(item.owner, viewer->circuit->session_id);
            }
            if (entry == "god") {
                valid_rule = true;
                return god_rights_level(item.owner) >= 200;
            }
            if (entry == "grid_god") {
                valid_rule = true;
                const auto account = policies.script_ossl_grid_gods && policies.script_account
                    ? policies.script_account(item.owner) : std::nullopt;
                return account && account->user_level >= 200;
            }
            return false;
        }))
            return {};
        Uuid creator;
        framework::try_parse_sdk_uuid(item.creator_id, creator);
        bool valid_creator = false;
        if (matches(creators, [&](const auto& entry) {
            Uuid allowed;
            if (!framework::try_parse_sdk_uuid(entry, allowed) || allowed.is_zero())
                return false;
            valid_creator = true;
            return creator == allowed;
        })) {
            if (creator == item.owner ||
                !(static_cast<std::uint32_t>(item.current_permissions) &
                  static_cast<std::uint32_t>(framework::PermissionMask::Modify)))
                return {};
            return name + " permission denied. Script creator is not prim owner";
        }
        if (valid_creator)
            return name + " permission denied. Script creator is not in the list of users allowed to execute "
                          "this function and prim owner also has no permission";
        return valid_rule ? name + " permission denied" : disabled;
    }

    void replace_script_instance(RunningScript& entry, std::unique_ptr<script::LslInstance> instance = {}) {
        entry.question.reset();
        entry.cpu_ms = 0.;
        cancel_dataserver_requests(entry.item.id);
        revoke_script_urls(entry.item.id);
        const auto delta = static_cast<int>(bool(instance)) - static_cast<int>(bool(entry.instance));
        entry.instance = std::move(instance);
        if (delta)
            active_scripts.fetch_add(delta, std::memory_order_relaxed);
    }

    void cancel_dataserver_requests(Uuid item) {
        std::erase_if(dataserver_requests, [item](const auto& request) { return request.item == item; });
        if (dataserver_lookup && dataserver_lookup->request.item == item)
            dataserver_lookup->request.item = {};
    }

    std::optional<std::string> script_avatar_name(Uuid id) const {
        if (viewers)
            if (const auto avatar = viewers->agent(id))
                return avatar->circuit->name();
        return {};
    }

    Uuid script_avatar_key(std::string_view name) const {
        if (viewers)
            for (const auto& avatar : viewers->agents())
                if (avatar.root && framework::invariant_equals_ignore_case(name, avatar.circuit->name()))
                    return avatar.circuit->agent_id;
        return {};
    }

    void post_dataserver_reply(const DataserverRequest& request, std::string name) {
        if (request.item.is_zero() || !world.object(request.part))
            return;
        if (request.format)
            name = request.format(name);
        const auto query = request.query.str();
        for (auto& [id, entry] : scripts)
            if (entry.item.parent == request.part && entry.instance)
                entry.instance->post_event({"dataserver", {query, name}, {}});
    }

    TaskInventoryRegistry::Item script_inventory_item(Uuid id, const std::string& name,
                                                      std::optional<std::int32_t> type = {}) const {
        const auto part = task_parts.find(id);
        if (part != task_parts.end() && part->second.inventory)
            for (const auto& item : part->second.inventory->items())
                if (item->name == name && (!type || item->type == *type))
                    return item;
        return {};
    }

    void process_dataserver_requests() {
        if (dataserver_lookup) {
            if (dataserver_lookup->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            auto completed = std::exchange(dataserver_lookup, {});
            try {
                if (auto reply = completed->result.get())
                    post_dataserver_reply(completed->request, std::move(*reply));
            } catch (const std::exception&) {
                // Source dataserver actions drop the request if its lookup fails.
            }
        }
        while (!dataserver_requests.empty()) {
            auto request = std::move(dataserver_requests.front());
            dataserver_requests.pop_front();
            std::function<std::optional<std::string>()> lookup;
            if (request.deferred_lookup) {
                lookup = std::move(request.deferred_lookup);
            } else if (request.landmark_name) {
                const auto item = script_inventory_item(request.part, *request.landmark_name,
                                                        static_cast<std::int32_t>(AssetType::Landmark));
                if (item && policies.load_asset) {
                    lookup = [load = policies.load_asset, asset = item->asset_id,
                              x = descriptor.x, y = descriptor.y] {
                        const auto landmark = load(asset);
                        if (!landmark)
                            return std::string{};
                        return script::lsl_vector_string(script::landmark_position_relative_to(
                            landmark->data, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)));
                    };
                } else {
                    post_dataserver_reply(request, {});
                }
            } else if (request.avatar_name) {
                const auto id = script_avatar_key(request.avatar_name->scene_name());
                if (!id.is_zero() || !policies.script_account_key) {
                    post_dataserver_reply(request, id.str());
                } else {
                    lookup = [resolve = policies.script_account_key, name = *request.avatar_name] {
                        return resolve(name.first, name.last, name.server).str();
                    };
                }
            } else if (const auto avatar = script_avatar_name(request.target)) {
                post_dataserver_reply(request, *avatar);
            } else if (const auto object = world.object(request.target)) {
                post_dataserver_reply(request, object->name);
            } else if (policies.script_account) {
                lookup = [resolve = policies.script_account, target = request.target] {
                    const auto account = resolve(target);
                    return account ? account->name() : std::string{};
                };
            } else {
                post_dataserver_reply(request, {});
            }
            if (lookup) {
                try {
                    dataserver_lookup.emplace(DataserverLookup{
                        std::move(request), std::async(std::launch::async, std::move(lookup))});
                    break;
                } catch (const std::system_error& e) {
                    error(e.what());
                }
            }
        }
    }

    bool script_updates_open = false;
    std::deque<std::shared_ptr<ScriptUpdateJob>> script_update_jobs;
    std::map<Uuid, TaskPart> task_parts;
    std::vector<TaskCommand> task_commands;
    std::vector<std::pair<ViewerAgent, WearableCacheItems>> pending_bake_restores;
    struct WearablesReply {
        std::chrono::steady_clock::time_point due;
        Uuid agent, session;
    };
    std::deque<WearablesReply> pending_wearables;
    std::map<Uuid, WearablesReply> pending_appearance_saves;
    std::map<Uuid, WearablesReply> pending_appearance_sends;
    struct InventoryScriptCommand {
        ViewerAgent actor;
        Uuid item;
        std::uint32_t local;
    };
    std::vector<InventoryScriptCommand> inventory_script_commands;
    std::optional<ViewerAgent> task_recipient;
    std::unique_ptr<XferFiles> xfer;
    static std::int32_t xfer_tick() {
        return std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count()));
    }

    State(GridRegion r, IPresenceService& s, RegionPolicies p)
        : descriptor(std::move(r)), sessions(s), policies(std::move(p)),
          world(descriptor.id, descriptor.size_x, descriptor.size_y), admission(*this), verification(*this) {
        if (!policies.access || !policies.authorize)
            throw std::invalid_argument("Region access and authorization policies are required");
        if (!policies.script_url_host)
            script_url_error = "[Network] configuration missing, HTTP listener for LSL disabled";
        else if (framework::trim_utf8(*policies.script_url_host).empty())
            script_url_error = "ExternalHostNameForLSL not defined in configuration, HTTP listener for LSL disabled";
        else if (!framework::host_resolver().host_from_dns(policies.script_url_host))
            script_url_error = "Could not resolve ExternalHostNameForLSL, HTTP listener for LSL disabled";
        if (!script_url_error.empty())
            warn("[URL MODULE]: " + script_url_error);
        access_region.id = descriptor.id;
        access_region.scope = descriptor.scope;
        access_region.name = descriptor.name;
        access_region.size_x = descriptor.size_x;
        access_region.size_y = descriptor.size_y;
        world.terrain().clear_land();
        xfer = std::make_unique<XferFiles>(xfer_tick);
        if (policies.inventory)
            legacy_inventory = std::make_unique<LegacyInventoryRequests>(*policies.inventory);
    }
    AccessRegion& region() override {
        return access_region;
    }
    std::recursive_mutex& remove_client_mutex() override {
        return remove_mutex;
    }
    std::recursive_mutex& new_user_mutex() override {
        return new_mutex;
    }
    void log_incoming(const std::shared_ptr<AgentCircuitData>& c, std::uint32_t, const GridRegion*,
                      const std::string&) override {
        debug("Incoming agent " + c->agent_id.str());
    }
    bool logins_enabled() const override {
        return running;
    }
    std::vector<std::optional<std::string>> allowed_viewers() const override {
        return {};
    }
    std::vector<std::optional<std::string>> banned_viewers() const override {
        return {};
    }
    std::shared_ptr<AdmissionPresence> presence(const Uuid& a) override {
        std::lock_guard lock(actors_mutex);
        auto i = actors.find(a);
        return i == actors.end() ? nullptr : i->second;
    }
    bool in_transit(const Uuid&) override {
        return false;
    }
    void sleep(int ms) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }
    bool telehub_allow_landmarks() const override {
        return policies.telehub_allow_landmarks;
    }
    bool check_land_access(const Uuid& a, bool not_crossing, bool check_hub, bool,
                           const Vector3& p, std::string& reason) override {
        const auto viewer = viewers->agent(a);
        const bool foreign = viewer && viewer->circuit->verified_foreign_login;
        return check_arrival_access(a, foreign, not_crossing, check_hub, p, reason);
    }
    bool check_arrival_access(Uuid agent, bool foreign, bool not_crossing, bool check_hub,
                              const Vector3& requested, std::string& reason) {
        if (!std::isfinite(requested.x) || !std::isfinite(requested.y) || !std::isfinite(requested.z)) {
            reason = "Invalid agent position";
            return false;
        }
        if (!check_estate_access(agent, foreign, reason))
            return false;
        if (not_crossing && check_hub && !allows_direct_teleport() &&
            (foreign || god_rights_level(agent) < 200) && !is_estate_manager(agent)) {
            std::optional<SceneObject> hub;
            std::vector<RegionSpawnPoint> points;
            {
                std::lock_guard lock(telehub_mutex);
                hub = live_telehub();
                if (hub)
                    points = region_spawn_points;
            }
            if (hub) {
                bool valid_spawn_parcel = false;
                for (const auto& point : points) {
                    const auto position = spawn_location(*hub, point);
                    const auto parcel_access = spawn_parcel_access(agent, foreign, position);
                    if (!parcel_access)
                        continue;
                    valid_spawn_parcel = true;
                    if (*parcel_access && policies.access(agent, position, reason))
                        return true;
                }
                if (valid_spawn_parcel) {
                    reason = "No suitable landing point found";
                    return false;
                }
                if (points.empty() && spawn_parcel_access(agent, foreign, hub->position).value_or(false) &&
                    policies.access(agent, hub->position, reason))
                    return true;
            }
        }
        auto position = requested;
        if (not_crossing && !adjust_parcel_access(agent, foreign, position)) {
            reason = "You dont have access to the region parcels";
            return false;
        }
        return policies.access(agent, position, reason);
    }
    bool check_circuit_land_access(const AgentCircuitData& circuit, bool check_hub,
                                   std::string& reason) override {
        return check_arrival_access(circuit.agent_id, circuit.verified_foreign_login, true, check_hub,
                                    circuit.start_position, reason);
    }
    void remove_circuit(const std::shared_ptr<AgentCircuitData>& c) override {
        circuits.remove_current_circuit(c);
    }
    void close_agent(const Uuid& a, bool) override {
        viewers->disconnect(a);
    }
    void remove_cached_account(const Uuid&) override {}
    void add_circuit(const std::shared_ptr<AgentCircuitData>& c) override {
        circuits.add_new_circuit(c);
    }
    bool has_presence_service() const override {
        return true;
    }
    std::optional<AdmissionGridPresence> grid_presence(const Uuid& s) override {
        auto p = sessions.agent(s);
        return p ? std::optional(AdmissionGridPresence{p->user_id}) : std::nullopt;
    }
    bool see_into_region() const override {
        return true;
    }
    bool authorize_user(const std::shared_ptr<AgentCircuitData>& c, bool bypass,
                        std::string& reason) override {
        return (bypass || check_estate_access(c->agent_id, c->verified_foreign_login, reason)) &&
               policies.authorize(c, reason);
    }
    bool deny_estate_ban(Uuid id, std::string& reason) const {
        std::lock_guard lock(estate_access_mutex);
        if (std::find(policies.estate_bans.begin(), policies.estate_bans.end(), id) ==
            policies.estate_bans.end())
            return false;
        reason = "Denied access to region " + descriptor.name.value_or("") +
                 ": You have been banned from that region.";
        return true;
    }
    bool check_region_access(Uuid id, const Vector3& position, std::string& reason) const {
        const auto viewer = viewers->agent(id);
        return check_estate_access(id, viewer && viewer->circuit->verified_foreign_login, reason) &&
               policies.access(id, position, reason);
    }
    static constexpr std::uint32_t estate_deny_minors = 0x40000000u;
    static constexpr std::uint32_t estate_deny_anonymous = 0x800000u;
    std::uint32_t effective_estate_handshake_flags() const {
        auto flags = policies.estate_handshake_only_flags;
        if (!policies.enforce_estate_age)
            flags &= ~estate_deny_minors;
        if (!policies.enforce_estate_identity)
            flags &= ~estate_deny_anonymous;
        return flags;
    }
    bool profile_allows_arrival(Uuid agent, bool foreign, std::uint32_t parcel_flags = 0) const {
        constexpr std::uint32_t parcel_deny_minors = 0x80000000u, parcel_deny_anonymous = 0x400000u;
        const auto estate = policies.estate_handshake_only_flags;
        const bool require_age = policies.enforce_estate_age &&
            ((estate & estate_deny_minors) || (parcel_flags & parcel_deny_minors));
        const bool require_identity = policies.enforce_estate_identity &&
            ((estate & estate_deny_anonymous) || (parcel_flags & parcel_deny_anonymous));
        if (!require_age && !require_identity)
            return true;
        constexpr std::uint32_t identified = 4, age_verified = 32;
        const auto profile = !foreign && policies.account_profile_flags ? policies.account_profile_flags(agent) : 0u;
        return (!require_age || (profile & age_verified)) && (!require_identity || (profile & identified));
    }
    bool check_estate_access(Uuid id, bool foreign, std::string& reason) const {
        if (!policies.strict_estate_access || (!foreign && god_rights_level(id) >= 200))
            return true;
        std::vector<Uuid> groups;
        bool public_estate = false;
        {
            std::lock_guard lock(estate_access_mutex);
            const auto contains = [&](const auto& list) {
                return std::find(list.begin(), list.end(), id) != list.end();
            };
            if (contains(policies.estate_bans)) {
                reason = "Denied access to region " + descriptor.name.value_or("") +
                         ": You have been banned from that region.";
                return false;
            }
            constexpr std::uint32_t public_access = 0x8000;
            if (id == descriptor.estate_owner ||
                contains(policies.estate_managers) || contains(policies.estate_users))
                return true;
            public_estate = (policies.estate_handshake_only_flags & public_access) != 0;
            if (!public_estate)
                groups = policies.estate_groups;
        }
        if (policies.estate_manager && policies.estate_manager(id))
            return true;
        if (!profile_allows_arrival(id, foreign)) {
            reason = "Denied access to region " + descriptor.name.value_or("") +
                     ": You have been banned from that region.";
            return false;
        }
        if (public_estate)
            return true;
        if (!groups.empty() && policies.estate_group_memberships) {
            const auto memberships = policies.estate_group_memberships(id);
            if (std::any_of(groups.begin(), groups.end(), [&](Uuid group) {
                    return std::find(memberships.begin(), memberships.end(), group) != memberships.end();
                }))
                return true;
        }
        if (!policies.estate_group_memberships && policies.inventory_group_member &&
            std::any_of(groups.begin(), groups.end(),
                        [&](Uuid group) { return policies.inventory_group_member(id, group); }))
            return true;
        reason = "Denied access to private region " + descriptor.name.value_or("") +
                 ": You do not have access to that region.";
        return false;
    }
    std::shared_ptr<AdmissionCaps> caps() override {
        return capability_runtime
                   ? std::shared_ptr<AdmissionCaps>(&capability_runtime->capabilities(), [](auto*) {})
                   : nullptr;
    }
    void cache_user_name(const std::shared_ptr<AgentCircuitData>&) override {}
    std::string scene_name() const override {
        return descriptor.name.value_or("");
    }
    std::optional<std::string> region_name() const override {
        return descriptor.name;
    }
    std::optional<std::string> external_host() const override {
        return descriptor.external_host;
    }
    std::shared_ptr<CapabilityRuntimeListener> listener() const override {
        return registry;
    }
    std::uint32_t region_size_x() const override {
        return descriptor.size_x;
    }
    std::uint32_t region_size_y() const override {
        return descriptor.size_y;
    }
    AgentCircuitManager* authenticate_handler() const override {
        return &circuits;
    }
    std::shared_ptr<UserAgentVerificationModule> user_verification_module() override {
        return {};
    }
    std::shared_ptr<VerifiedScenePresence> scene_presence(const Uuid&) override {
        return {};
    }
    IPresenceService* presence_service() override {
        return &sessions;
    }
    bool check_client(Uuid a, const HttpRequest& r) override {
        auto ip = IpAddress::parse(r.remote_ip);
        if (!ip)
            return false;
        IpEndpoint endpoint{std::make_shared<IpAddress>(*ip), static_cast<std::uint16_t>(r.remote_port)};
        return verification.check_client(a, &endpoint);
    }
    void debug(const std::string& s) override {
        if (policies.log)
            policies.log(s);
    }
    void warn(const std::string& s) override {
        debug(s);
    }
    void error(const std::string& s) override {
        debug(s);
    }
    void console(const std::string& s) override {
        debug(s);
    }
    void cleanup_exception(const std::exception& e) override {
        error(e.what());
    }
    void add_command(CapabilityCommand) override {}
    void send_avatar(const ViewerAgent& a, const ViewerAgent& recipient) {
        if (!can_see_avatar(a, recipient))
            return;
        const auto started = std::chrono::steady_clock::now();
        AvatarUpdate update;
        update.id = a.circuit->agent_id;
        update.local_id = a.local_id;
        update.position = a.position;
        update.velocity = a.velocity;
        update.rotation = a.rotation;
        update.flying = a.flying;
        update.sitting = a.ground_sitting;
        if (a.seat) {
            update.parent_id = a.seat->parent_local;
            update.position = a.seat->offset;
            update.rotation = a.seat->rotation;
            update.sitting = true;
        }
        update.first_name = a.circuit->first_name.value_or("");
        update.last_name = a.circuit->last_name.value_or("");
        if (const auto appearance = a.appearance())
            update.size = appearance->avatar_size();
        if (a.active_group_override)
            update.group_title = a.active_group_override->title;
        auto packet = avatar_object_update(update, descriptor.handle(), time_dilation);
        const bool queued = viewers->transport().enqueue_if_session(
            recipient.circuit->agent_id, recipient.circuit->session_id, std::move(packet),
            ThrottleOutPacketType::Task);
        if (queued)
            stats_agent_updates.fetch_add(1, std::memory_order_relaxed);
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started);
        stats_agent_ns.fetch_add(elapsed.count(), std::memory_order_relaxed);
    }
    struct AvatarParcelView {
        Uuid session, parcel;
        bool hidden = false, god = false;
        bool operator==(const AvatarParcelView&) const = default;
        bool visible_to(const AvatarParcelView& recipient) const {
            return !hidden || parcel == recipient.parcel || recipient.god;
        }
    };
    struct ViewerGodMode {
        Uuid session;
        int rights = 0, level = 0;
        bool frozen = false;
    };
    mutable std::mutex viewer_god_mutex;
    std::map<Uuid, ViewerGodMode> viewer_god_modes;
    bool viewer_is_god(Uuid agent, Uuid session) const {
        if (policies.viewer_ui_god && policies.viewer_ui_god(agent))
            return true;
        std::lock_guard lock(viewer_god_mutex);
        const auto state = viewer_god_modes.find(agent);
        return state != viewer_god_modes.end() && state->second.session == session &&
               state->second.level >= 200;
    }
    int agent_god_level(Uuid agent, Uuid session) const {
        std::lock_guard lock(viewer_god_mutex);
        const auto state = viewer_god_modes.find(agent);
        if (state == viewer_god_modes.end() || state->second.session != session)
            return 0;
        return policies.implicit_gods ? state->second.rights : state->second.level;
    }
    bool movement_frozen(const ViewerAgent& viewer) const {
        std::lock_guard lock(viewer_god_mutex);
        const auto state = viewer_god_modes.find(viewer.circuit->agent_id);
        return state != viewer_god_modes.end() && state->second.session == viewer.circuit->session_id &&
               state->second.frozen;
    }
    struct PendingGodKick {
        Uuid agent, session;
        std::chrono::steady_clock::time_point due;
    };
    std::vector<PendingGodKick> pending_god_kicks;
    bool send_kick(const ViewerAgent& viewer, std::u16string_view reason) {
        auto packet = lludp::make_packet("KickUser");
        auto& user = packet.block("UserInfo");
        user["AgentID"] = viewer.circuit->agent_id;
        user["SessionID"] = viewer.circuit->session_id;
        user["Reason"] = profile_string_bytes(reason, 256);
        return viewers->transport().enqueue_if_session(viewer.circuit->agent_id, viewer.circuit->session_id,
                                                       std::move(packet), ThrottleOutPacketType::Task);
    }
    void queue_kick(const ViewerAgent& viewer, std::u16string_view reason) {
        if (!send_kick(viewer, reason))
            return;
        std::lock_guard lock(viewer_god_mutex);
        pending_god_kicks.push_back({viewer.circuit->agent_id, viewer.circuit->session_id,
                                     std::chrono::steady_clock::now() + std::chrono::milliseconds(500)});
    }
    void god_kick_user(const ViewerAgent& god, const lludp::Packet& packet) {
        const auto& data = packet.block("UserInfo");
        const auto god_id = god.circuit->agent_id, session = god.circuit->session_id;
        if (!god.root || std::get<Uuid>(data.at("GodID")) != god_id ||
            std::get<Uuid>(data.at("GodSessionID")) != session)
            return;
        const auto mode = std::get<std::uint64_t>(data.at("KickFlags"));
        if (mode > 2)
            return;
        const auto level = moderation_level(god_id, &god);
        moderate_user(
            god_id, &god, level, std::get<Uuid>(data.at("AgentID")), static_cast<std::uint8_t>(mode),
            framework::sdk_bytes_to_string(std::get<std::vector<std::uint8_t>>(data.at("Reason"))), true);
    }
    int moderation_level(Uuid id, const ViewerAgent* viewer) const {
        if (viewer && viewer->circuit->verified_foreign_login)
            return 0;
        if (god_rights_level(id) < 200)
            return 0;
        return viewer ? std::max(200, agent_god_level(id, viewer->circuit->session_id)) : 200;
    }
    bool is_estate_manager(Uuid id) const {
        if (id.is_zero())
            return false;
        if (id == descriptor.estate_owner || (policies.estate_manager && policies.estate_manager(id)))
            return true;
        std::lock_guard lock(estate_access_mutex);
        return std::find(policies.estate_managers.begin(), policies.estate_managers.end(), id) !=
               policies.estate_managers.end();
    }
    int god_rights_level(Uuid id) const {
        const auto level = policies.god_rights_level ? policies.god_rights_level(id) : 0;
        return policies.estate_manager_is_god && is_estate_manager(id) ? std::max(level, 210) : level;
    }
    bool moderate_user(Uuid god_id, const ViewerAgent* god, int level, Uuid target, std::uint8_t mode,
                       const std::u16string& reason, bool forward) {
        if (level < 200 || mode > 2)
            return false;
        static const Uuid all_agents("44e87126-e794-4ded-05b3-7c42da3d5cdb");
        const auto target_viewer =
            target == all_agents ? std::optional<ViewerAgent>{} : viewers->agent(target);
        if (target != all_agents && (!target_viewer || !target_viewer->root)) {
            if (!forward || !policies.forward_instant_message)
                return false;
            RegionInstantMessage message;
            message.sender = god_id;
            message.recipient = target;
            message.region = descriptor.id;
            message.dialog = 250;
            message.offline = 1;
            message.sender_name = u"God";
            message.message = reason;
            message.bucket = {mode};
            message.timestamp =
                static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count());
            policies.forward_instant_message(message);
            return true;
        }
        const auto notify = [&](const ViewerAgent& viewer, const std::string& message) {
            const auto id = viewer.circuit->agent_id;
            viewers->transport().enqueue_if_session(
                id, viewer.circuit->session_id, agent_alert(id, message, false), ThrottleOutPacketType::Task);
        };
        for (const auto& viewer : viewers->agents()) {
            const auto id = viewer.circuit->agent_id, target_session = viewer.circuit->session_id;
            if (!viewer.root || id == god_id || (target != all_agents && target != id))
                continue;
            if (level <= agent_god_level(id, target_session)) {
                notify(viewer,
                       "Kick from " + god_id.str() + " ignored, kick reason: " + framework::utf8(reason));
                continue;
            }
            if (mode == 0) {
                queue_kick(viewer, reason);
                continue;
            }
            {
                std::lock_guard lock(viewer_god_mutex);
                const auto state = viewer_god_modes.find(id);
                if (state == viewer_god_modes.end() || state->second.session != target_session)
                    continue;
                state->second.frozen = mode == 1;
            }
            notify(viewer, framework::utf8(reason));
            if (god)
                notify(*god, mode == 1 ? "User Frozen" : "User Unfrozen");
        }
        return true;
    }
    void close_kicked_viewers() {
        std::vector<PendingGodKick> due;
        {
            std::lock_guard lock(viewer_god_mutex);
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(pending_god_kicks, [&](const auto& kick) {
                if (kick.due > now)
                    return false;
                due.push_back(kick);
                return true;
            });
        }
        for (const auto& kick : due)
            viewers->transport().remove_client_if_session(kick.agent, kick.session);
    }
    void send_god_level(const ViewerAgent& viewer, int level) {
        const auto& circuit = *viewer.circuit;
        auto response = lludp::make_packet("GrantGodlikePowers");
        response.block("AgentData")["AgentID"] = circuit.agent_id;
        response.block("AgentData")["SessionID"] = circuit.session_id;
        response.block("GrantData")["GodLevel"] = std::uint64_t(static_cast<std::uint8_t>(level));
        response.block("GrantData")["Token"] = Uuid{};
        viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id, std::move(response),
                                                ThrottleOutPacketType::Task);
    }
    void request_god_mode(const ViewerAgent& viewer, bool enable) {
        const auto& circuit = *viewer.circuit;
        std::lock_guard lock(viewer_god_mutex);
        const auto state = viewer_god_modes.find(circuit.agent_id);
        if (state == viewer_god_modes.end() || state->second.session != circuit.session_id)
            return;
        const auto level = enable ? state->second.rights : 0;
        if (level != state->second.level) {
            state->second.level = level;
            send_god_level(viewer, level);
        }
        if (enable && level < 200) {
            auto reply = lludp::make_packet("AlertMessage");
            constexpr std::string_view message = "Request for god powers denied";
            auto bytes = std::vector<std::uint8_t>(message.begin(), message.end());
            bytes.push_back(0);
            reply.block("AlertData")["Message"] = std::move(bytes);
            viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id, std::move(reply),
                                                    ThrottleOutPacketType::Task);
        }
    }
    std::map<Uuid, AvatarParcelView> avatar_parcel_views;
    void enqueue_windlight_refresh(Uuid agent, bool interpolate) {
        if (!capability_runtime || !capability_runtime->event_queue())
            return;
        EventQueueByteBuffer event;
        event.text = event_queue_inner_xml(Value::object({
            {"message", Value("WindLightRefresh")},
            {"body", Value::object({{"Interpolate", Value(std::int32_t(interpolate))}})},
        }));
        capability_runtime->event_queue()->enqueue(event.to_array(), agent);
    }
    void refresh_avatar_parcel_views() {
        if (!policies.script_parcel_at)
            return;
        const auto agents = viewers->agents();
        std::map<Uuid, AvatarParcelView> current;
        for (const auto& agent : agents)
            if (agent.root) {
                const auto id = agent.circuit->agent_id;
                const auto parcel = policies.script_parcel_at(agent.position);
                current.emplace(id, AvatarParcelView{agent.circuit->session_id, parcel ? parcel->id : Uuid{},
                                                     parcel && !parcel->see_avatars,
                                                     viewer_is_god(id, agent.circuit->session_id)});
            }
        if (current == avatar_parcel_views)
            return;
        for (const auto& avatar : agents)
            if (avatar.root) {
                const auto id = avatar.circuit->agent_id;
                const auto& source = current.at(id);
                const auto previous = avatar_parcel_views.find(id);
                if (previous == avatar_parcel_views.end() || previous->second.session != source.session)
                    continue;
                if (policies.environment_enabled && (policies.estate_menu_flags & 0x200u) &&
                    !avatar.logout_requested && !source.parcel.is_zero() &&
                    source.parcel != previous->second.parcel && !(terrain_cap_flags(*avatar.circuit) & Caps::AdvEnv))
                    if (!agent_environment(avatar.circuit->session_id))
                        enqueue_windlight_refresh(id, true);
                for (const auto& recipient : agents)
                    if (recipient.root && recipient.circuit->agent_id != id) {
                        const auto target_id = recipient.circuit->agent_id;
                        const auto& target = current.at(target_id);
                        const auto old_target = avatar_parcel_views.find(target_id);
                        if (old_target == avatar_parcel_views.end() ||
                            old_target->second.session != target.session)
                            continue;
                        const bool visible = source.visible_to(target);
                        if (visible == previous->second.visible_to(old_target->second))
                            continue;
                        if (visible) {
                            send_avatar(avatar, recipient);
                            send_appearance(avatar, recipient);
                            if (const auto physics = viewer_physics.load();
                                !physics || !physics->broadcast_animations(id, std::span(&recipient, 1))) {
                                std::lock_guard lock(task_data_mutex);
                                const auto snapshot = animation_snapshot(id, active_animations(avatar));
                                viewers->transport().enqueue_if_session(
                                    target_id, target.session, avatar_animations(id, target_id, snapshot),
                                    ThrottleOutPacketType::Task);
                            }
                            for (const auto& object : world.attachments(avatar.local_id))
                                send_object(object, recipient);
                        } else {
                            auto kill = lludp::make_packet("KillObject");
                            for (const auto& object : world.attachments(avatar.local_id)) {
                                auto& rows = kill.blocks["ObjectData"];
                                rows.push_back({{"ID", std::uint64_t(object.local_id)}});
                                if (rows.size() == 200) {
                                    viewers->transport().enqueue_if_session(target_id, target.session,
                                                                            std::move(kill),
                                                                            ThrottleOutPacketType::Task);
                                    kill = lludp::make_packet("KillObject");
                                }
                            }
                            kill.blocks["ObjectData"].push_back({{"ID", std::uint64_t(avatar.local_id)}});
                            viewers->transport().enqueue_if_session(
                                target_id, target.session, std::move(kill), ThrottleOutPacketType::Task);
                        }
                    }
            }
        avatar_parcel_views.swap(current);
    }
    bool can_see_avatar(const ViewerAgent& avatar, const ViewerAgent& recipient) const {
        if (avatar.circuit->agent_id == recipient.circuit->agent_id || !policies.script_parcel_at)
            return true;
        const auto source = policies.script_parcel_at(avatar.position);
        if (!source || source->see_avatars)
            return true;
        if (viewer_is_god(recipient.circuit->agent_id, recipient.circuit->session_id))
            return true;
        const auto destination = policies.script_parcel_at(recipient.position);
        return destination && source->id == destination->id;
    }
    void send_appearance(const ViewerAgent& avatar, const ViewerAgent& recipient) {
        if (!can_see_avatar(avatar, recipient))
            return;
        const auto appearance = avatar.appearance();
        if (!appearance || !appearance->texture || !appearance->visual_params)
            return;
        const auto texture = appearance->texture->bakes_bytes();
        viewers->transport().enqueue_if_session(recipient.circuit->agent_id, recipient.circuit->session_id,
                                                avatar_appearance(avatar.circuit->agent_id,
                                                                  *appearance->visual_params, texture,
                                                                  appearance->hover_z),
                                                ThrottleOutPacketType::Task);
    }
    struct SimulatorMessageRoute final : server::CapabilitySimpleHandler {
        std::weak_ptr<State> owner;
        Uuid agent;
        std::string route = "/C" + Uuid::random().str();
        SimulatorMessageRoute(std::weak_ptr<State> state, Uuid id) : owner(std::move(state)), agent(id) {}
        std::optional<std::string> path() const override {
            return route;
        }
        HttpResponse handle(const HttpRequest& request) override {
            HttpResponse response;
            const auto state = owner.lock();
            if (!state || !state->running || request.method != "POST") {
                response.status = 404;
                return response;
            }
            try {
                const auto input = deserialize_sdk_llsd_xml(request.body);
                if (input.kind != Value::Kind::Map || osd_string(input.get("message")) != "GodKickUser") {
                    response.status = 400;
                    return response;
                }
                const auto& body = input.get("body").view();
                const auto& users = body.get("UserInfo").view();
                if (body.kind != Value::Kind::Map || users.kind != Value::Kind::Array ||
                    users.array.empty() || users.array.front().view().kind != Value::Kind::Map) {
                    response.status = 400;
                    return response;
                }
                const auto& data = users.array.front().view();
                const auto god = osd_uuid(data.get("GodID"));
                const auto session = osd_uuid(data.get("GodSessionID"));
                const auto viewer = state->viewers->agent(god);
                if (god != agent || !viewer || viewer->circuit->session_id != session) {
                    response.status = 401;
                    return response;
                }
                const auto mode = osd_unsigned(data.get("KickFlags"));
                if (mode <= 2)
                    state->moderate_user(god, &*viewer, state->moderation_level(god, &*viewer),
                                         osd_uuid(data.get("AgentID")), static_cast<std::uint8_t>(mode),
                                         framework::utf16(osd_string(data.get("Reason"))), true);
                response.status = 200;
            } catch (const std::exception&) {
                response.status = 400;
            }
            return response;
        }
    };
    struct EstateRegionInfoRoute final : server::CapabilitySimpleHandler {
        std::weak_ptr<State> owner;
        Uuid agent;
        std::string route = "/" + Uuid::random().str();
        EstateRegionInfoRoute(std::weak_ptr<State> state, Uuid id) : owner(std::move(state)), agent(id) {}
        std::optional<std::string> path() const override { return route; }
        HttpResponse handle(const HttpRequest& request) override {
            HttpResponse response;
            const auto state = owner.lock();
            if (!state || !state->running || request.method != "POST") {
                response.status = 404;
                return response;
            }
            Value input;
            try {
                input = deserialize_sdk_osd_stream(request.body);
            } catch (const std::exception&) {
                response.status = 400;
                return response;
            }
            // The original compares the map's implicit boolean value to Count < 3.
            const auto& map = input.view();
            if (map.kind != Value::Kind::Map || map.map.size() == 1 || map.map.size() == 2) {
                response.status = 400;
                return response;
            }
            const auto viewer = state->viewers ? state->viewers->agent(agent) : std::nullopt;
            if (!viewer || !state->can_edit_estate_region(*viewer)) {
                response.status = 401;
                return response;
            }
            try {
                state->edit_estate_region_settings([&](EstateRegionSettings& settings) {
                    const auto flag = [&](const char* key, std::uint32_t mask, bool invert = false) {
                        if (!map.contains(key) || map.get(key).view().kind == Value::Kind::NullReference)
                            return;
                        if (osd_boolean(map.get(key)) != invert)
                            settings.flags |= mask;
                        else
                            settings.flags &= ~mask;
                    };
                    flag("block_terraform", 0x40u);
                    flag("block_fly", 0x80000u);
                    flag("allow_damage", 0x1u);
                    flag("allow_land_resell", 0x80u, true);
                    flag("restrict_pushobject", 0x400000u);
                    flag("allow_parcel_changes", 0x4000000u);
                    flag("block_parcel_search", 0x20000000u);
                    if (map.contains("agent_limit"))
                        settings.agent_limit = state->estate_agent_limit(osd_integer(map.get("agent_limit")));
                    if (map.contains("prim_bonus"))
                        settings.object_bonus = static_cast<float>(osd_real(map.get("prim_bonus")));
                    if (map.contains("sim_access")) {
                        const auto access = osd_integer(map.get("sim_access"));
                        settings.maturity = access <= 13 ? 0 : access <= 21 ? 1 : 2;
                    }
                });
                response.status = 200;
            } catch (const std::exception& failure) {
                state->error(std::string("Estate region settings update failed: ") + failure.what());
                response.status = 500;
            }
            return response;
        }
    };
    std::optional<EnvironmentDay> parcel_environment(const ScriptParcel& parcel) const {
        const auto update = parcel_environment_updates.find(parcel.id);
        return update == parcel_environment_updates.end() ? parcel.environment_day : update->second;
    }
    std::optional<EnvironmentDay> agent_environment(Uuid session) const {
        std::lock_guard lock(environment_mutex);
        const auto found = agent_environment_overrides.find(session);
        return found == agent_environment_overrides.end() ? std::nullopt : std::optional(found->second);
    }
    std::vector<std::uint8_t> parcel_bitmap(const ScriptParcel& parcel) const {
        auto bitmap = parcel.land_bitmap;
        const auto width = descriptor.size_x / 4;
        const auto height = descriptor.size_y / 4;
        if (bitmap.empty() && std::int64_t(parcel.area) == std::int64_t(descriptor.size_x) * descriptor.size_y &&
            width > 0 && height > 0)
            bitmap.resize((static_cast<std::size_t>(width) * height + 7) / 8, 255);
        return bitmap;
    }
    std::pair<Vector3d, Vector3d> parcel_bounds(std::span<const std::uint8_t> bitmap) const {
        const auto width = descriptor.size_x / 4;
        const auto height = descriptor.size_y / 4;
        Vector3d minimum{}, maximum{};
        if (width > 0 && height > 0) {
            minimum = {double(descriptor.size_x), double(descriptor.size_y), 0};
            const auto cells = std::min(bitmap.size() * 8, static_cast<std::size_t>(width) * height);
            for (std::size_t cell = 0; cell < cells; ++cell) {
                if (!(bitmap[cell / 8] & (1u << (cell % 8))))
                    continue;
                const auto x = double((cell % width) * 4);
                const auto y = double((cell / width) * 4);
                minimum.x = std::min(minimum.x, x);
                minimum.y = std::min(minimum.y, y);
                maximum.x = std::max(maximum.x, x + 4);
                maximum.y = std::max(maximum.y, y + 4);
            }
            if (maximum.x == 0) {
                minimum = {};
            } else {
                const auto& terrain = world.terrain();
                minimum.z = terrain.get(static_cast<int>(minimum.x), static_cast<int>(minimum.y));
                maximum.z = terrain.get(static_cast<int>(maximum.x) - 1, static_cast<int>(maximum.y) - 1);
            }
        }
        return {minimum, maximum};
    }
    void load_parcel_bounds() {
        initial_parcel_bounds.clear();
        if (policies.list_parcels) {
            for (const auto& parcel : policies.list_parcels()) {
                if (parcel)
                    initial_parcel_bounds.emplace(parcel->id, parcel_bounds(parcel_bitmap(*parcel)));
            }
        } else if (policies.script_parcel_at) {
            if (const auto parcel = policies.script_parcel_at({0, 0, 0}))
                initial_parcel_bounds.emplace(parcel->id, parcel_bounds(parcel_bitmap(*parcel)));
        }
    }
    void send_parcel_environment_properties(const ViewerAgent& viewer, const ScriptParcel& parcel) {
        if (!capability_runtime || !capability_runtime->event_queue())
            return;
        constexpr std::uint32_t allow_environment_override = 0x200u;
        constexpr std::uint32_t restrict_push_object = 0x400000u;
        constexpr std::uint32_t tax_free = 0x20u;
        const auto estate_flags = effective_estate_handshake_flags();
        const auto parcel_region_flags = region_flags() | policies.estate_menu_flags;
        const bool allow_override = (parcel_region_flags & allow_environment_override) != 0;
        std::int32_t version = -1;
        if (allow_override && !agent_environment(viewer.circuit->session_id)) {
            std::lock_guard lock(environment_mutex);
            if (const auto day = parcel_environment(parcel); day && day->settings)
                version = osd_integer(day->settings->get("env_version"));
        }
        const auto uuid = [](Uuid id) {
            Value value(id.str());
            value.kind = Value::Kind::Uuid;
            return value;
        };
        const auto vector = [](Vector3d value) {
            return environment_array({environment_real(value.x), environment_real(value.y), environment_real(value.z)});
        };
        auto bitmap = parcel_bitmap(parcel);
        const auto cached_bounds = initial_parcel_bounds.find(parcel.id);
        const auto [minimum, maximum] = cached_bounds != initial_parcel_bounds.end()
            ? cached_bounds->second
            : parcel_bounds(bitmap);
        Value binary_bitmap;
        binary_bitmap.kind = Value::Kind::Binary;
        binary_bitmap.binary_data = std::make_shared<std::vector<std::uint8_t>>(std::move(bitmap));
        Value flags;
        flags.kind = Value::Kind::Binary;
        flags.binary_data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{
            static_cast<std::uint8_t>(parcel.flags >> 24), static_cast<std::uint8_t>(parcel.flags >> 16),
            static_cast<std::uint8_t>(parcel.flags >> 8), static_cast<std::uint8_t>(parcel.flags),
        });
        const auto max_prims = parcel.area > 0 && policies.script_parcel_capacity
            ? policies.script_parcel_capacity(parcel.area)
            : std::int32_t{0};
        constexpr std::int64_t unix_epoch_ticks = 621355968000000000LL;
        constexpr std::int64_t ticks_per_second = 10000000;
        Value claim_date;
        claim_date.kind = Value::Kind::Date;
        claim_date.sdk_date = std::make_shared<framework::AssetDateTime>(
            unix_epoch_ticks + parcel.claim_date * ticks_per_second, 1);
        auto data = Value::object({
            {"LocalID", Value(parcel.local_id)},
            {"AABBMax", vector(maximum)},
            {"AABBMin", vector(minimum)},
            {"Area", Value(parcel.area)},
            {"AuctionID", Value(std::bit_cast<std::int32_t>(parcel.auction_id))},
            {"AuthBuyerID", uuid(parcel.authorized_buyer)},
            {"Bitmap", std::move(binary_bitmap)},
            {"Category", Value(parcel.category)},
            {"ClaimDate", std::move(claim_date)},
            {"ClaimPrice", Value(parcel.claim_price)},
            {"Desc", Value(parcel.description)},
            {"ParcelFlags", std::move(flags)},
            {"GroupID", uuid(parcel.group)},
            {"IsGroupOwned", Value(parcel.group_owned)},
            {"LandingType", Value(parcel.landing_type)},
            {"MaxPrims", Value(max_prims)},
            {"MediaID", uuid(parcel.media_id)},
            {"MediaURL", Value(parcel.media_url)},
            {"MediaAutoScale", Value(parcel.media_auto_scale)},
            {"MusicURL", Value(parcel.music_url)},
            {"Name", Value(parcel.name)},
            {"OtherCleanTime", Value(parcel.other_clean_time)},
            {"OtherCount", Value(std::int32_t{0})},
            {"OwnerID", uuid(parcel.owner)},
            {"ParcelPrimBonus", environment_real(region_object_bonus.load())},
            {"PassHours", environment_real(parcel.pass_hours)},
            {"PassPrice", Value(parcel.pass_price)},
            {"PublicCount", Value(std::int32_t{0})},
            {"RegionDenyAnonymous", Value((parcel_region_flags & estate_deny_anonymous) != 0)},
            {"RegionDenyIdentified", Value(false)},
            {"RegionDenyTransacted", Value(false)},
            {"RegionPushOverride", Value((parcel_region_flags & restrict_push_object) != 0)},
            {"RentPrice", Value(std::int32_t{0})},
            {"RequestResult", Value(std::int32_t{0})},
            {"SalePrice", Value(parcel.sale_price)},
            {"SelfCount", Value(std::int32_t{0})},
            {"SequenceID", Value(std::int32_t{0})},
            {"SnapSelection", Value(false)},
            {"SnapshotID", uuid(parcel.snapshot_id)},
            {"Status", Value(parcel.status)},
            {"UserLocation", vector(parcel.landing_point)},
            {"UserLookAt", vector(parcel.landing_look_at)},
            {"SeeAVs", Value(parcel.see_avatars)},
            {"AnyAVSounds", Value(parcel.any_avatar_sounds)},
            {"GroupAVSounds", Value(parcel.group_avatar_sounds)},
        });
        Value extended_flags;
        extended_flags.kind = Value::Kind::Binary;
        extended_flags.binary_data = std::make_shared<std::vector<std::uint8_t>>(
            std::initializer_list<std::uint8_t>{0, 0, 0, static_cast<std::uint8_t>(parcel.obscure_media)});
        const auto body = Value::object({
            {"ParcelData", environment_array({std::move(data)})},
            {"MediaData", environment_array({Value::object({
                {"MediaDesc", Value(parcel.media_description)},
                {"MediaHeight", Value(parcel.media_height)},
                {"MediaWidth", Value(parcel.media_width)},
                {"MediaLoop", Value(parcel.media_loop)},
                {"MediaType", Value(parcel.media_type)},
                {"ObscureMedia", Value(false)},
                {"ObscureMusic", Value(false)},
            })})},
            {"ParcelExtendedFlags", environment_array({Value::object({
                {"Flags", std::move(extended_flags)},
            })})},
            {"AgeVerificationBlock", environment_array({Value::object({
                {"RegionDenyAgeUnverified", Value((parcel_region_flags & estate_deny_minors) != 0)},
            })})},
            {"ParcelEnvironmentBlock", environment_array({Value::object({
                {"ParcelEnvironmentVersion", Value(version)},
                {"RegionAllowEnvironmentOverride", Value(allow_override)},
            })})},
            {"RegionAllowAccessBlock", environment_array({Value::object({
                {"RegionAllowAccessOverride", Value((estate_flags & tax_free) == 0)},
            })})},
        });
        EventQueueByteBuffer event;
        event.text = event_queue_inner_xml(Value::object({{"message", Value("ParcelProperties")}, {"body", body}}));
        capability_runtime->event_queue()->enqueue(event.to_array(), viewer.circuit->agent_id);
    }
    void notify_parcel_environment(const ScriptParcel& parcel) {
        if (!policies.script_parcel_at)
            return;
        for (const auto& viewer : viewers->agents()) {
            if (!viewer.root || viewer.logout_requested)
                continue;
            const auto current = policies.script_parcel_at(viewer.position);
            if (current && current->id == parcel.id)
                send_parcel_environment_properties(viewer, parcel);
        }
    }
    EnvironmentDay environment_at(Vector3 position, bool region) const {
        const auto parcel = !region && policies.script_parcel_at ? policies.script_parcel_at(position) : nullptr;
        std::lock_guard lock(environment_mutex);
        if (parcel)
            if (const auto day = parcel_environment(*parcel))
                return *day;
        return environment_day;
    }
    struct ExtendedEnvironmentRoute final : server::CapabilitySimpleHandler {
        std::weak_ptr<State> owner;
        Uuid agent;
        std::string route = "/" + Uuid::random().str();
        ExtendedEnvironmentRoute(std::weak_ptr<State> state, Uuid id) : owner(std::move(state)), agent(id) {}
        std::optional<std::string> path() const override { return route; }
        static std::optional<std::int32_t> parameter(const Form& form, const char* name) {
            const auto entry = form.find(name);
            if (entry == form.end())
                return -1;
            const auto* value = std::get_if<std::string>(&entry->second);
            if (!value)
                return {};
            const auto trimmed = framework::trim_utf8(*value);
            std::string_view text = trimmed;
            if (text.starts_with('+'))
                text.remove_prefix(1);
            std::int32_t number = 0;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                return {};
            return number;
        }
        static HttpResponse reply(Value value) {
            HttpResponse response;
            response.content_type = "application/llsd+xml";
            response.body = llsd_xml(value);
            return response;
        }
        static HttpResponse rejected(const char* message) {
            return reply(Value::object({{"success", Value(false)}, {"message", Value(message)}}));
        }
        static Value uuid_value(Uuid id) {
            Value value(id.str());
            value.kind = Value::Kind::Uuid;
            return value;
        }
        static Value empty_array() {
            Value array;
            array.kind = Value::Kind::Array;
            return array;
        }
        static Value empty_cycle() {
            auto tracks = empty_array();
            tracks.array.resize(5, empty_array());
            return Value::object({{"frames", Value::object({})}, {"name", Value("")},
                                  {"tracks", std::move(tracks)}, {"type", Value("daycycle")}});
        }
        static std::optional<Value> import_cycle(Value cycle, Value asset, const std::string& requested_name) {
            const auto type = osd_string(asset.get("type"));
            const bool named = !framework::trim_utf8(requested_name).empty();
            if (type == "daycycle") {
                if (named)
                    asset.set("name", Value(requested_name));
                return normalize_environment_cycle(std::move(asset));
            }
            if (type != "sky" && type != "water")
                return {};

            const bool sky = type == "sky";
            try {
                asset = sky ? normalize_sky_frame(asset) : normalize_water_frame(asset);
            } catch (const std::invalid_argument&) {
                return {};
            }
            const auto frame_name = named ? requested_name : (sky ? "Sky" : "Water");
            if (cycle.kind != Value::Kind::Map)
                cycle = empty_cycle();
            auto frames = cycle.get("frames").map;
            std::erase_if(frames, [&](const auto& frame) { return osd_string(frame.second.get("type")) == type; });
            // ViewerEnvironment serializes water first, then sky, when both use the same name.
            if (sky || !frames.contains(frame_name))
                frames.insert_or_assign(frame_name, std::move(asset));
            cycle.set("frames", Value::object(std::move(frames)));

            auto tracks = cycle.get("tracks");
            if (tracks.kind != Value::Kind::Array)
                tracks = empty_array();
            tracks.array.resize(5, empty_array());
            auto replacement = empty_array();
            Value keyframe;
            keyframe.kind = Value::Kind::Real;
            keyframe.real = 0;
            replacement.array.push_back(
                Value::object({{"key_keyframe", keyframe}, {"key_name", Value(frame_name)}}));
            tracks.array[sky ? 1 : 0] = std::move(replacement);
            if (sky)
                for (std::size_t i = 2; i < tracks.array.size(); ++i)
                    tracks.array[i] = empty_array();
            cycle.set("tracks", std::move(tracks));
            if (named)
                cycle.set("name", Value(requested_name));
            return cycle;
        }
        static std::shared_ptr<const ScriptParcel> find_parcel(const State& state, std::int32_t parcel_id) {
            if (parcel_id != -1 && state.policies.list_parcels)
                for (const auto& parcel : state.policies.list_parcels())
                    if (parcel && parcel->local_id == parcel_id)
                        return parcel;
            return {};
        }
        HttpResponse handle(const HttpRequest& request) override {
            HttpResponse response;
            if (request.method != "GET" && request.method != "PUT" &&
                request.method != "POST" && request.method != "DELETE") {
                response.status = 405;
                return response;
            }
            const auto state = owner.lock();
            const auto viewer = state && state->running && state->viewers ? state->viewers->agent(agent) : std::nullopt;
            if (request.method == "GET" && !viewer) {
                response.status = 503;
                response.headers["Retry-After"] = "5";
                return response;
            }
            if (!state || !state->running || (request.method != "GET" && request.method != "DELETE" &&
                                               (!viewer || !viewer->root)))
                return rejected("Could not locate your avatar");
            const auto query = request.raw_target.find('?');
            const auto form = query == std::string::npos ? Form{} :
                parse_service_form(std::string_view(request.raw_target).substr(query + 1));
            const auto parcel_id = parameter(form, "parcelid");
            const auto parcel = find_parcel(*state, parcel_id.value_or(0));
            if (request.method == "GET")
                return get(*state, parcel_id.value_or(0), parcel, viewer->circuit->session_id);
            if (request.method == "DELETE")
                return erase(*state, parcel_id.value_or(0), parcel);
            const auto track = parameter(form, "trackno");
            if (!parcel_id || !track)
                return rejected("Failed to decode request");
            if (*track != -1)
                return rejected("Environment Track not supported");
            if (*parcel_id == -1) {
                if (!state->can_edit_estate_region(*viewer))
                    return rejected("Insufficient estate permissions, settings has not been saved.");
            } else {
                if (!parcel)
                    return rejected("Could not locate requested parcel");
                constexpr std::uint64_t allow_environment = 1ull << 46;
                if (!state->can_edit_parcel_access(*parcel, agent, viewer->circuit->verified_foreign_login,
                                                   allow_environment))
                    return rejected("No permission to change parcel environment");
            }
            return edit(*state, parcel, request.body, state->can_edit_estate_region(*viewer));
        }
        static HttpResponse get(const State& state, std::int32_t parcel_id,
                                const std::shared_ptr<const ScriptParcel>& parcel, Uuid session) {
            const auto day = [&]() -> std::optional<EnvironmentDay> {
                if (const auto override = state.agent_environment(session))
                    return override;
                std::lock_guard lock(state.environment_mutex);
                if (parcel_id == -1)
                    return state.environment_day;
                if (parcel && (state.policies.estate_menu_flags & 0x200u))
                    return state.parcel_environment(*parcel);
                return {};
            }();

            Value altitudes;
            altitudes.kind = Value::Kind::Array;
            for (double altitude : {1000., 2000., 3000.}) {
                Value value;
                value.kind = Value::Kind::Real;
                value.real = altitude;
                altitudes.array.push_back(value);
            }
            auto environment = Value::object({{"region_id", uuid_value(state.descriptor.id)},
                                              {"track_altitudes", altitudes}});
            if (day) {
                const auto& settings = day->settings;
                auto cycle = settings ? settings->get("day_cycle") : state.default_environment_cycle;
                if (cycle.kind != Value::Kind::Map)
                    cycle = empty_cycle();
                environment.set("day_length", Value(day->length));
                environment.set("day_offset", Value(day->offset));
                environment.set("flags", Value(settings ? osd_integer(settings->get("flags")) : 0));
                environment.set("env_version", Value(settings
                    ? (settings->contains("env_version") ? osd_integer(settings->get("env_version")) : 1) : 0));
                if (settings && settings->get("track_altitudes").kind == Value::Kind::Array) {
                    const auto& saved = settings->get("track_altitudes").array;
                    for (std::size_t i = 0; i < std::min(saved.size(), altitudes.array.size()); ++i)
                        altitudes.array[i].real = static_cast<float>(osd_real(saved[i]));
                    auto tracks = cycle.get("tracks");
                    if (tracks.kind == Value::Kind::Array) {
                        while (tracks.array.size() < 5) {
                            Value track;
                            track.kind = Value::Kind::Array;
                            tracks.array.push_back(std::move(track));
                        }
                    }
                    for (std::size_t i = 0; i < altitudes.array.size(); ++i) {
                        for (auto j = i + 1; j < altitudes.array.size(); ++j) {
                            if (altitudes.array[i].real > altitudes.array[j].real) {
                                std::swap(altitudes.array[i], altitudes.array[j]);
                                if (tracks.kind == Value::Kind::Array)
                                    std::swap(tracks.array[i + 2], tracks.array[j + 2]);
                            }
                        }
                    }
                    if (tracks.kind == Value::Kind::Array)
                        cycle.set("tracks", std::move(tracks));
                    environment.set("track_altitudes", std::move(altitudes));
                }
                environment.set("day_cycle", std::move(cycle));
            } else {
                environment.set("is_default", Value(true));
            }
            if (day || parcel_id >= 0)
                environment.set("parcel_id", Value(parcel_id));
            auto result = Value::object({{"environment", std::move(environment)}, {"success", Value(true)}});
            if (day || parcel_id >= 0)
                result.set("parcel_id", Value(parcel_id));
            return reply(std::move(result));
        }
        static void send_lightshare(State& state, const ViewerAgent& viewer,
                                    std::optional<std::vector<std::uint8_t>>& lightshare) {
            if (!lightshare) {
                std::lock_guard lock(state.environment_mutex);
                const auto& settings = state.environment_day.settings;
                lightshare = settings ? lightshare_payload(settings->get("day_cycle")) : std::vector<std::uint8_t>{};
            }
            auto packet = lludp::make_packet("GenericMessage");
            packet.zerocoded = false;
            auto& identity = packet.block("AgentData");
            identity["AgentID"] = viewer.circuit->agent_id;
            identity["SessionID"] = viewer.circuit->session_id;
            auto& method = packet.block("MethodData");
            method["Method"] = profile_string_bytes(lightshare->empty() ? u"WindlightReset" : u"Windlight", 255);
            method["Invoice"] = Uuid::random();
            if (!lightshare->empty()) {
                auto parameter = lludp::make_block(packet.spec->blocks.at(2));
                parameter["Parameter"] = *lightshare;
                packet.blocks["ParamList"].push_back(std::move(parameter));
            }
            state.viewers->transport().enqueue_if_session(viewer.circuit->agent_id, viewer.circuit->session_id,
                                                           std::move(packet), ThrottleOutPacketType::Task);
        }
        static void refresh_agent(State& state, const ViewerAgent& viewer, bool interpolate) {
            const auto flags = state.terrain_cap_flags(*viewer.circuit);
            if (flags & Caps::AdvEnv) {
                const auto parcel = state.policies.script_parcel_at
                    ? state.policies.script_parcel_at(viewer.position) : nullptr;
                if (parcel) {
                    const auto day = [&] {
                        std::lock_guard lock(state.environment_mutex);
                        return state.parcel_environment(*parcel);
                    }();
                    if (day)
                        state.send_parcel_environment_properties(viewer, *parcel);
                }
                state.send_region_info(viewer);
            } else if (flags & Caps::WLEnv)
                state.enqueue_windlight_refresh(viewer.circuit->agent_id, interpolate);
            else {
                std::optional<std::vector<std::uint8_t>> lightshare;
                send_lightshare(state, viewer, lightshare);
            }
        }
        static void refresh(State& state, bool region, bool interpolate = false) {
            std::optional<std::vector<std::uint8_t>> lightshare;
            for (const auto& viewer : state.viewers->agents()) {
                if (!viewer.root || viewer.logout_requested)
                    continue;
                const auto flags = state.terrain_cap_flags(*viewer.circuit);
                if ((flags & Caps::AdvEnv) && region)
                    state.send_region_info(viewer);
                else if (!(flags & Caps::AdvEnv) && (flags & Caps::WLEnv))
                    state.enqueue_windlight_refresh(viewer.circuit->agent_id, interpolate);
                else if (!(flags & (Caps::AdvEnv | Caps::WLEnv)))
                    send_lightshare(state, viewer, lightshare);
            }
        }
        static HttpResponse erase(State& state, std::int32_t parcel_id,
                                  const std::shared_ptr<const ScriptParcel>& parcel) {
            if (parcel_id == -1 || parcel) {
                try {
                    {
                        std::lock_guard lock(state.environment_mutex);
                        if (state.policies.persist_environment)
                            state.policies.persist_environment(parcel ? std::optional(parcel->id) : std::nullopt,
                                                               std::nullopt);
                        if (parcel)
                            state.parcel_environment_updates.insert_or_assign(parcel->id, std::nullopt);
                        else
                            state.environment_day = {};
                    }
                    if (!parcel)
                        state.environment_time_refresh = true;
                    else
                        state.notify_parcel_environment(*parcel);
                    refresh(state, !parcel);
                } catch (const std::exception&) {
                    return rejected("Could not reset environment settings");
                }
            }
            return reply(Value::object({{"success", Value(true)}, {"messageID", uuid_value({})},
                                         {"regionID", uuid_value(state.descriptor.id)}}));
        }
        static void store_windlight(State& state, const Value& request) {
            auto day = EnvironmentDay::from_windlight(request);
            {
                std::lock_guard lock(state.environment_mutex);
                if (state.environment_day.settings) {
                    const auto version = static_cast<std::uint32_t>(
                        osd_integer(state.environment_day.settings->get("env_version")));
                    day.apply(Value::object({
                        {"env_version", Value(std::bit_cast<std::int32_t>(version + 1))},
                    }), false);
                }
                if (state.policies.persist_environment)
                    state.policies.persist_environment(std::nullopt, llsd_xml(*day.settings));
                state.environment_day = std::move(day);
            }
            state.environment_time_refresh = true;
            refresh(state, true);
        }
        static HttpResponse edit(State& state, const std::shared_ptr<const ScriptParcel>& parcel, std::string_view body,
                                 bool can_edit_region) {
            try {
                const auto request = deserialize_sdk_osd_stream(body);
                if (request.kind == Value::Kind::Array) {
                    if (!can_edit_region)
                        return rejected("Insufficient estate permissions, settings has not been saved.");
                    store_windlight(state, request);
                    return reply(Value::object({{"success", Value(true)}, {"messageID", uuid_value({})},
                                                 {"regionID", uuid_value(state.descriptor.id)}}));
                }
                auto update = request.get("environment");
                if (update.kind != Value::Kind::Map)
                    return rejected("Could not process request");
                const bool uses_asset = update.contains("day_asset") && !update.contains("day_cycle");
                Value imported_asset;
                std::string imported_name;
                if (uses_asset) {
                    const auto asset_id = osd_uuid(update.get("day_asset"));
                    const auto asset = state.policies.load_asset ? state.policies.load_asset(asset_id) : nullptr;
                    HttpResponse missing;
                    missing.status = 404;
                    if (!asset || !asset->data || asset->data->empty())
                        return missing;
                    try {
                        imported_asset = deserialize_sdk_osd_bytes(
                            {reinterpret_cast<const char*>(asset->data->data()), asset->data->size()});
                        if (const auto& name = update.get("day_name"); name.kind == Value::Kind::String)
                            imported_name = osd_string(name);
                    } catch (const std::exception&) {
                        return missing;
                    }
                }
                {
                    std::lock_guard lock(state.environment_mutex);
                    auto day = parcel ? state.parcel_environment(*parcel).value_or(EnvironmentDay{})
                                      : state.environment_day;
                    const auto previous_version = day.settings
                        ? static_cast<std::uint32_t>(osd_integer(day.settings->get("env_version")))
                        : std::numeric_limits<std::uint32_t>::max();
                    if (!day.settings) {
                        auto initial = Value::object({{"env_version", Value(std::int32_t{0})}});
                        if (state.default_environment_cycle.kind == Value::Kind::Map)
                            initial.set("day_cycle", state.default_environment_cycle);
                        day.settings = std::make_shared<const Value>(std::move(initial));
                    }
                    if (uses_asset) {
                        update = Value::object({});
                        if (auto cycle = import_cycle(day.settings->get("day_cycle"), std::move(imported_asset),
                                                      imported_name))
                            update.set("day_cycle", std::move(*cycle));
                    }
                    update.set("env_version", Value(std::bit_cast<std::int32_t>(previous_version + 1)));
                    day.apply(std::move(update), !uses_asset);
                    if (state.policies.persist_environment)
                        state.policies.persist_environment(parcel ? std::optional(parcel->id) : std::nullopt,
                                                           llsd_xml(*day.settings));
                    if (parcel)
                        state.parcel_environment_updates.insert_or_assign(parcel->id, std::move(day));
                    else
                        state.environment_day = std::move(day);
                }
                if (!parcel)
                    state.environment_time_refresh = true;
                else
                    state.notify_parcel_environment(*parcel);
                refresh(state, !parcel);
                return reply(Value::object({{"success", Value(true)}}));
            } catch (const std::exception& failure) {
                state.error(std::string("ExtEnvironment settings update failed: ") + failure.what());
                const auto message = "ExtEnvironment Set for region " + state.descriptor.name.value_or("") +
                                     " has failed, settings not saved.";
                return rejected(message.c_str());
            }
        }
    };
    std::int32_t load_script_day_cycle(Uuid part_id, const std::string& name, Value& cycle,
                                       std::int32_t invalid_subtype = -5) {
        Uuid id;
        if (!framework::try_parse_sdk_uuid(name, id)) {
            std::lock_guard lock(task_data_mutex);
            const auto inventory = task_parts.find(part_id);
            if (inventory != task_parts.end() && inventory->second.inventory)
                for (const auto& item : inventory->second.inventory->items())
                    if (item->name == name) {
                        id = item->asset_id;
                        break;
                    }
        }
        try {
            const auto asset = !id.is_zero() && policies.load_asset ? policies.load_asset(id) : nullptr;
            if (!asset || asset->type() != static_cast<std::int8_t>(AssetType::Settings))
                return -4;
            if (!asset->data || asset->data->empty())
                return -5;
            cycle = deserialize_sdk_osd_bytes(
                {reinterpret_cast<const char*>(asset->data->data()), asset->data->size()});
            return cycle.kind == Value::Kind::Map && osd_string(cycle.get("type")) == "daycycle"
                ? 1 : invalid_subtype;
        } catch (const std::exception&) {
            return -5;
        }
    }
    std::int32_t replace_script_agent_environment(Uuid part, Uuid item, const std::string& avatar,
                                                   std::int32_t transition, const std::string& name) {
        if (!script_ossl_permission(part, item, "osReplaceAgentEnvironment", false).empty())
            return -2;
        Uuid id;
        if (!framework::try_parse_sdk_uuid(avatar, id))
            return -4;
        const auto viewer = viewers->agent(id);
        if (!viewer || !viewer->root || viewer->logout_requested)
            return -4;
        const auto session = viewer->circuit->session_id;
        {
            std::lock_guard lock(teleport_mutex);
            if (std::ranges::any_of(pending_teleports, [&](const auto& transfer) {
                    return transfer.agent == id && transfer.session == session;
                }))
                return -4;
        }
        if (!policies.environment_enabled)
            return -5;

        std::optional<EnvironmentDay> replacement;
        auto result = 1;
        if (!name.empty() && name != Uuid{}.str()) {
            Value cycle;
            result = load_script_day_cycle(part, name, cycle, -3);
            if (result == -4 || result == -3)
                return -3;
            if (result == 1) {
                try {
                    auto day = environment_at({}, true);
                    day.apply(Value::object({{"day_cycle", std::move(cycle)},
                                             {"env_version", Value(day.settings
                                                 ? osd_integer(day.settings->get("env_version")) : 0)}}));
                    replacement = std::move(day);
                } catch (const std::exception&) {
                    result = -5;
                }
            }
        }
        {
            std::lock_guard lock(environment_mutex);
            const auto current = viewers->agent(id);
            if (!current || current->circuit->session_id != session || !current->root || current->logout_requested)
                return -4;
            if (replacement) {
                const auto previous = agent_environment_overrides.find(session);
                const auto version = previous == agent_environment_overrides.end() ? 0 :
                    osd_integer(previous->second.settings->get("env_version"));
                std::uint32_t next_version;
                if (version <= 0) {
                    const auto random = Uuid::random().bytes();
                    next_version = 0x07000000u | (std::uint32_t(random[0] & 0x7f) << 24) |
                        (std::uint32_t(random[1]) << 16) | (std::uint32_t(random[2]) << 8) | random[3];
                } else {
                    next_version = static_cast<std::uint32_t>(version) + 1;
                }
                replacement->apply(Value::object({{"env_version", Value(std::bit_cast<std::int32_t>(next_version))}}),
                                   false);
                agent_environment_overrides.insert_or_assign(session, std::move(*replacement));
            } else
                agent_environment_overrides.erase(session);
        }
        ExtendedEnvironmentRoute::refresh_agent(*this, *viewer, transition > 0);
        return result;
    }
    std::int32_t replace_script_parcel_environment(Uuid part_id, std::int32_t transition, const std::string& name) {
        if (!(policies.estate_menu_flags & 0x200u))
            return -1;
        const auto part = world.object(part_id);
        const auto parcel = part && policies.script_parcel_at
            ? policies.script_parcel_at(script_world_position(*part)) : nullptr;
        if (!parcel)
            return -2;
        constexpr std::uint64_t allow_environment = 1ull << 46;
        if (!can_edit_parcel_access(*parcel, part->owner, false, allow_environment))
            return -3;
        if (name.empty() || name == Uuid{}.str())
            return 1;
        if (!policies.environment_enabled)
            return -5;
        Value cycle;
        if (const auto result = load_script_day_cycle(part_id, name, cycle); result != 1)
            return result;
        try {
            {
                std::lock_guard lock(environment_mutex);
                const auto previous = parcel_environment(*parcel);
                auto day = previous.value_or(environment_day);
                const auto version = previous && previous->settings
                    ? static_cast<std::uint32_t>(osd_integer(previous->settings->get("env_version")))
                    : std::numeric_limits<std::uint32_t>::max();
                day.apply(Value::object({{"day_cycle", std::move(cycle)},
                                         {"day_length", Value(day.length)}, {"day_offset", Value(day.offset)},
                                         {"env_version", Value(std::bit_cast<std::int32_t>(version + 1))}}));
                if (policies.persist_environment)
                    policies.persist_environment(parcel->id, llsd_xml(*day.settings));
                parcel_environment_updates.insert_or_assign(parcel->id, std::move(day));
            }
            notify_parcel_environment(*parcel);
            ExtendedEnvironmentRoute::refresh(*this, false, transition > 0);
        } catch (const std::exception& failure) {
            error(std::string("Script parcel environment update failed: ") + failure.what());
            return -5;
        }
        return 1;
    }
    std::int32_t replace_script_region_environment(Uuid part_id, const script::LslRegionEnvironmentEdit& request) {
        const auto part = world.object(part_id);
        if (!part || (!policies.bypass_estate_permissions && part->owner != descriptor.estate_owner &&
                      god_rights_level(part->owner) < 200))
            return -3;
        if (!policies.environment_enabled)
            return -3;

        std::optional<Value> replacement;
        if (!request.day_cycle.empty() && request.day_cycle != Uuid{}.str()) {
            replacement.emplace();
            if (const auto result = load_script_day_cycle(part_id, request.day_cycle, *replacement); result != 1)
                return result;
        }

        try {
            {
                std::lock_guard lock(environment_mutex);
                auto day = environment_day;
                auto update = Value::object({});
                bool changed = replacement.has_value();
                auto length = day.length;
                if (request.day_length >= 4 && request.day_length <= 24 * 7) {
                    length = static_cast<std::int32_t>(request.day_length * 3600);
                    changed |= length != day.length;
                    update.set("day_length", Value(length));
                }
                if (request.day_offset >= -11.5 && request.day_offset <= 11.5) {
                    auto offset = request.day_offset;
                    if (offset <= 0)
                        offset += 24;
                    const auto seconds = static_cast<std::int32_t>(offset * 3600);
                    // The managed setter compares the new offset with the day length.
                    changed |= seconds != length;
                    update.set("day_offset", Value(seconds));
                }

                auto cycle = replacement ? std::move(*replacement) :
                    day.settings ? day.settings->get("day_cycle") : default_environment_cycle;
                std::array<float, 3> altitudes{1000.f, 2000.f, 3000.f};
                if (day.settings) {
                    const auto& saved = day.settings->get("track_altitudes").array;
                    for (std::size_t i = 0; i < std::min(saved.size(), altitudes.size()); ++i)
                        altitudes[i] = static_cast<float>(osd_real(saved[i]));
                }
                bool sort_altitudes = false;
                for (std::size_t i = 0; i < altitudes.size(); ++i) {
                    const auto requested = request.altitudes[i];
                    // The third altitude uses the second argument for the upper bound in .NET.
                    if (requested > 0 && request.altitudes[i == 2 ? 1 : i] < 4000 &&
                        static_cast<float>(requested) != altitudes[i]) {
                        altitudes[i] = static_cast<float>(requested);
                        sort_altitudes = true;
                    }
                }
                if (!changed && !sort_altitudes)
                    return 1;
                if (sort_altitudes) {
                    auto tracks = cycle.get("tracks");
                    if (tracks.kind == Value::Kind::Array)
                        tracks.array.resize(5, environment_array());
                    for (std::size_t i = 0; i < altitudes.size(); ++i)
                        for (auto j = i + 1; j < altitudes.size(); ++j)
                            if (altitudes[i] > altitudes[j]) {
                                std::swap(altitudes[i], altitudes[j]);
                                if (tracks.kind == Value::Kind::Array)
                                    std::swap(tracks.array[i + 2], tracks.array[j + 2]);
                            }
                    if (tracks.kind == Value::Kind::Array)
                        cycle.set("tracks", std::move(tracks));
                    update.set("track_altitudes", environment_array({environment_real(altitudes[0]),
                        environment_real(altitudes[1]), environment_real(altitudes[2])}));
                }
                if (cycle.kind == Value::Kind::Map)
                    update.set("day_cycle", std::move(cycle));
                const auto version = day.settings
                    ? static_cast<std::uint32_t>(osd_integer(day.settings->get("env_version")))
                    : std::numeric_limits<std::uint32_t>::max();
                update.set("env_version", Value(std::bit_cast<std::int32_t>(version + 1)));
                day.apply(std::move(update));
                if (policies.persist_environment)
                    policies.persist_environment(std::nullopt, llsd_xml(*day.settings));
                environment_day = std::move(day);
            }
            environment_time_refresh = true;
            ExtendedEnvironmentRoute::refresh(*this, true, request.transition > 0);
        } catch (const std::exception& failure) {
            error(std::string("Script region environment update failed: ") + failure.what());
            return -5;
        }
        return 1;
    }
    struct LegacyEnvironmentRoute final : server::CapabilitySimpleHandler {
        std::weak_ptr<State> owner;
        Uuid agent;
        std::string route = "/" + Uuid::random().str();
        LegacyEnvironmentRoute(std::weak_ptr<State> state, Uuid id) : owner(std::move(state)), agent(id) {}
        std::optional<std::string> path() const override { return route; }

        static Value identity(const State& state) {
            return Value::object({{"messageID", ExtendedEnvironmentRoute::uuid_value({})},
                                  {"regionID", ExtendedEnvironmentRoute::uuid_value(state.descriptor.id)}});
        }
        static HttpResponse result(const State& state, std::string reason = {}) {
            auto body = identity(state);
            body.set("success", Value(reason.empty()));
            if (!reason.empty())
                body.set("fail_reason", Value(std::move(reason)));
            return ExtendedEnvironmentRoute::reply(std::move(body));
        }
        static HttpResponse read(const State& state, const ViewerAgent& viewer) {
            const auto parcel = (state.policies.estate_menu_flags & 0x200u) && state.policies.script_parcel_at
                ? state.policies.script_parcel_at(viewer.position) : nullptr;
            const auto cycle = [&] {
                if (const auto override = state.agent_environment(viewer.circuit->session_id))
                    return override->settings->get("day_cycle");
                std::lock_guard lock(state.environment_mutex);
                const auto parcel_day = parcel ? state.parcel_environment(*parcel) : std::nullopt;
                const auto& day = parcel_day ? *parcel_day : state.environment_day;
                return day.settings ? day.settings->get("day_cycle") : state.default_environment_cycle;
            }();
            try {
                auto keys = environment_array();
                auto skies = Value::object({});
                auto water = Value::object({});
                const auto& frames = cycle.get("frames");
                const auto& tracks = cycle.get("tracks");
                if (tracks.kind == Value::Kind::Array && tracks.array.size() > 1)
                    for (const auto& entry : tracks.array[1].array)
                        keys.array.push_back(environment_array({entry.get("key_keyframe"), entry.get("key_name")}));
                if (tracks.kind == Value::Kind::Array && !tracks.array.empty() && !tracks.array[0].array.empty())
                    water = export_windlight_water(frames.get(osd_string(tracks.array[0].array[0].get("key_name"))));
                for (const auto& [name, frame] : frames.map)
                    if (osd_string(frame.get("type")) == "sky")
                        skies.set(name, export_windlight_sky(name, frame));
                return ExtendedEnvironmentRoute::reply(environment_array(
                    {identity(state), std::move(keys), std::move(skies), std::move(water)}));
            } catch (const std::exception&) {
                return ExtendedEnvironmentRoute::reply(environment_array({identity(state)}));
            }
        }
        HttpResponse handle(const HttpRequest& request) override {
            HttpResponse response;
            if (request.method != "GET" && request.method != "POST") {
                response.status = 405;
                return response;
            }
            const auto state = owner.lock();
            if (!state || !state->running) {
                response.status = 404;
                return response;
            }
            const auto viewer = state->viewers ? state->viewers->agent(agent) : std::nullopt;
            if (request.method == "GET") {
                if (viewer)
                    return read(*state, *viewer);
                response.status = 503;
                response.headers["Retry-After"] = "5";
                return response;
            }
            if (!state->policies.bypass_estate_permissions && !state->is_estate_manager(agent) &&
                (!viewer || viewer->circuit->verified_foreign_login || state->god_rights_level(agent) < 200))
                return result(*state, "Insufficient estate permissions, settings has not been saved.");
            if (!viewer || !viewer->root) {
                response.status = 404;
                return response;
            }
            if (state->agent_environment(viewer->circuit->session_id))
                return result(*state, "The environment you see is a forced one. Disable if on control object or tp out and back to region");
            if (const auto parcel = state->policies.script_parcel_at
                    ? state->policies.script_parcel_at(viewer->position) : nullptr) {
                std::lock_guard lock(state->environment_mutex);
                if (state->parcel_environment(*parcel))
                    return result(*state, "The parcel where you are has own environment set. You need a updated viewer to change environment");
            }
            try {
                ExtendedEnvironmentRoute::store_windlight(*state, deserialize_sdk_osd_stream(request.body));
                return result(*state);
            } catch (const std::exception&) {
                return result(*state, "Environment Set for region " + state->descriptor.name.value_or("") +
                                      " has failed, settings not saved.");
            }
        }
    };
    struct AgentPreferencesRoute final : server::CapabilitySimpleHandler {
        std::weak_ptr<State> owner;
        Uuid agent;
        std::string route = "/" + Uuid::random().str();
        AgentPreferencesRoute(std::weak_ptr<State> state, Uuid id) : owner(std::move(state)), agent(id) {}
        std::optional<std::string> path() const override {
            return route;
        }
        HttpResponse handle(const HttpRequest& request) override {
            HttpResponse response;
            const auto state = owner.lock();
            if (!state || !state->running || request.method != "POST") {
                response.status = 404;
                return response;
            }
            Value input;
            try {
                input = deserialize_sdk_llsd_xml(request.body);
            } catch (const std::exception&) {
                response.status = 400;
                return response;
            }
            if (input.kind != Value::Kind::Map) {
                response.status = 400;
                return response;
            }
            const auto& access = input.get("access_prefs");
            const auto& perms = input.get("default_object_perm_masks");
            AgentPreferences prefs;
            {
                std::lock_guard lock(state->preferences_mutex);
                if (state->policies.load_preferences)
                    prefs = state->policies.load_preferences(agent);
                bool changed = false;
                if (access.kind == Value::Kind::Map) {
                    prefs.access = osd_string(access.get("max"));
                    changed = true;
                }
                if (perms.kind == Value::Kind::Map) {
                    prefs.everyone = osd_integer(perms.get("Everyone"));
                    prefs.group = osd_integer(perms.get("Group"));
                    prefs.next_owner = osd_integer(perms.get("NextOwner"));
                    changed = true;
                }
                if (input.contains("hover_height")) {
                    prefs.hover = static_cast<float>(osd_real(input.get("hover_height")));
                    changed = true;
                }
                if (input.contains("language")) {
                    prefs.language = osd_string(input.get("language"));
                    changed = true;
                }
                if (input.contains("language_is_public")) {
                    prefs.language_public = osd_boolean(input.get("language_is_public"));
                    changed = true;
                }
                if (changed && state->policies.save_preferences)
                    state->policies.save_preferences(agent, prefs);
                state->set_preference_hover(agent, prefs.hover);
            }
            Value hover;
            hover.kind = Value::Kind::Real;
            hover.real = prefs.hover;
            response.content_type = "application/llsd+xml";
            response.body = llsd_xml(Value::object(
                {{"access_prefs", Value::object({{"max", Value(prefs.access)}})},
                 {"default_object_perm_masks", Value::object({{"Everyone", Value(prefs.everyone)},
                                                              {"Group", Value(prefs.group)},
                                                              {"NextOwner", Value(prefs.next_owner)}})},
                 {"god_level", Value(std::int32_t{0})},
                 {"hover_height", hover},
                 {"language", Value(prefs.language)},
                 {"language_is_public", Value(prefs.language_public)}}));
            return response;
        }
    };
    void set_preference_hover(Uuid agent, float hover) {
        if (!std::isfinite(hover))
            return;
        std::unique_lock lock(appearance_mutex);
        const auto current = viewers->agent(agent);
        if (!current || !current->root)
            return;
        const auto previous = current->appearance();
        if (previous && previous->hover_z == hover)
            return;
        auto appearance = std::make_shared<AvatarAppearance>(previous.get());
        appearance->hover_z = hover;
        const auto updated =
            viewers->update_appearance(agent, current->circuit->session_id, std::move(appearance));
        lock.unlock();
        if (updated)
            for (const auto& other : viewers->agents())
                if (other.circuit->agent_id != agent)
                    send_appearance(*updated, other);
    }
    std::mutex terrain_seed_mutex;
    std::map<Uuid, std::shared_ptr<AgentCircuitData>> pending_terrain_seeds;
    std::uint32_t terrain_cap_flags(const AgentCircuitData& circuit) const {
        if (!capability_runtime)
            return 0;
        const auto caps = std::dynamic_pointer_cast<Caps>(
            capability_runtime->capabilities().get_caps_for_user(circuit.circuit_code));
        return caps ? static_cast<std::uint32_t>(caps->flags) : 0;
    }
    void poll_environment_time() {
        if (!policies.environment_enabled)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (!environment_time_refresh.exchange(false) && now < next_environment_time)
            return;
        next_environment_time = now + std::chrono::milliseconds(2500);

        const auto epoch = std::chrono::system_clock::now().time_since_epoch();
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
        const auto unix_seconds = std::chrono::duration<double>(epoch).count();
        for (const auto& viewer : viewers->agents()) {
            if (!viewer.root || viewer.logout_requested)
                continue;
            {
                std::lock_guard lock(teleport_mutex);
                if (std::ranges::any_of(pending_teleports, [&](const auto& transfer) {
                        return transfer.agent == viewer.circuit->agent_id &&
                               transfer.session == viewer.circuit->session_id;
                    }))
                    continue;
            }
            const auto parcel = policies.script_parcel_at ? policies.script_parcel_at(viewer.position) : nullptr;
            const auto day = [&] {
                if (const auto override = agent_environment(viewer.circuit->session_id))
                    return *override;
                std::lock_guard lock(environment_mutex);
                const auto override = parcel ? parcel_environment(*parcel) : std::nullopt;
                return override.value_or(environment_day);
            }();
            auto phase = environment_day_fraction(day, unix_seconds);
            if (terrain_cap_flags(*viewer.circuit) & Caps::AdvEnv) {
                phase *= 2.f * std::numbers::pi_v<float>;
            } else {
                if (phase <= .25f)
                    phase += 1.5f;
                else if (phase > .75f)
                    phase += .5f;
                else if (phase >= .333333f)
                    phase = 3.f * phase - 1.f;
                else
                    phase = 3.f * phase + 1.f;
                phase = std::clamp(phase, 0.f, 2.f) * std::numbers::pi_v<float>;
            }
            auto packet = lludp::make_packet("SimulatorViewerTimeMessage");
            packet.reliable = false;
            packet.zerocoded = true;
            auto& info = packet.block("TimeInfo");
            info["UsecSinceStart"] = static_cast<std::uint64_t>(micros);
            info["SecPerDay"] = std::uint64_t{14400};
            info["SecPerYear"] = std::uint64_t{158400};
            info["SunDirection"] = std::vector<double>{0, 0, 0};
            info["SunPhase"] = static_cast<double>(phase);
            info["SunAngVelocity"] = std::vector<double>{0, 0, 0};
            viewers->transport().enqueue_if_session(viewer.circuit->agent_id, viewer.circuit->session_id,
                                                    std::move(packet), ThrottleOutPacketType::Task);
        }
    }
    bool supports_terrain_pbr(const ViewerAgent& viewer) const {
        return (terrain_cap_flags(*viewer.circuit) & Caps::TPBR) != 0;
    }
    void send_terrain_handshake(const AgentCircuitData& circuit, bool pbr) {
        viewers->transport().enqueue_if_session(
            circuit.agent_id, circuit.session_id, viewers->region_handshake(circuit.agent_id, pbr),
            ThrottleOutPacketType::Task);
        if (pbr) {
            std::lock_guard lock(terrain_seed_mutex);
            pending_terrain_seeds.erase(circuit.session_id);
        }
    }
    void finish_pending_terrain_seeds() {
        std::vector<std::shared_ptr<AgentCircuitData>> ready;
        {
            std::lock_guard lock(terrain_seed_mutex);
            std::erase_if(pending_terrain_seeds, [&](const auto& pending) {
                const auto flags = terrain_cap_flags(*pending.second);
                if (!(flags & Caps::SentSeeds))
                    return false;
                if (flags & Caps::TPBR)
                    ready.push_back(pending.second);
                return true;
            });
        }
        for (const auto& circuit : ready)
            send_terrain_handshake(*circuit, true);
    }
    void entered(const ViewerAgent& incoming) {
        const auto flags = terrain_cap_flags(*incoming.circuit);
        if (!(flags & Caps::SentSeeds)) {
            std::lock_guard lock(terrain_seed_mutex);
            pending_terrain_seeds.insert_or_assign(incoming.circuit->session_id, incoming.circuit);
        } else if (flags & Caps::TPBR) {
            send_terrain_handshake(*incoming.circuit, true);
        }
        auto a = incoming;
        {
            const auto rights = a.circuit->verified_foreign_login ? 0 : god_rights_level(a.circuit->agent_id);
            const auto initial_level = policies.automatic_viewer_gods ? rights : 0;
            const auto transferred = a.circuit->transfer_viewer_god_mode;
            const auto level = transferred ? (*transferred ? rights : 0) : initial_level;
            const auto previous_level =
                transferred ? (*transferred && initial_level < 200 ? 200 : initial_level) : 0;
            std::lock_guard lock(viewer_god_mutex);
            viewer_god_modes.insert_or_assign(a.circuit->agent_id,
                                              ViewerGodMode{a.circuit->session_id, rights, level});
            if (level != previous_level)
                send_god_level(a, level);
        }
        if (policies.viewer_active_group) {
            update_viewer_active_group(a, {}, false);
            const auto current = viewers->agent(a.circuit->agent_id);
            if (!current || current->circuit->session_id != a.circuit->session_id)
                return;
            a = *current;
        }
        if (policies.fetch_baked_appearance && policies.schedule_baked_fetch &&
            policies.baked_texture_cache && policies.baked_texture_cache()) {
            const auto appearance = a.appearance();
            if (!appearance || !appearance->cache_items) {
                try {
                    policies.schedule_baked_fetch(
                        [weak = weak_from_this(), actor = a, fetch = policies.fetch_baked_appearance] {
                            auto restored = fetch(actor.circuit->agent_id);
                            if (restored.empty())
                                return;
                            if (const auto state = weak.lock()) {
                                std::lock_guard lock(state->commands_mutex);
                                if (state->running)
                                    state->pending_bake_restores.emplace_back(actor, std::move(restored));
                            }
                        });
                } catch (const std::exception& e) {
                    error(std::string("XBakes fetch scheduling failed: ") + e.what());
                }
            }
        }
        {
            std::lock_guard lock(actors_mutex);
            auto i = actors.find(a.circuit->agent_id);
            if (i != actors.end())
                i->second->root = true;
        }
        sessions.report(a.circuit->session_id, descriptor.id);
        viewers->send_terrain(a.circuit->agent_id);
        std::string release_uri;
        {
            std::lock_guard lock(new_mutex);
            const auto callback = arrival_callbacks.find(a.circuit->agent_id);
            if (callback != arrival_callbacks.end() && callback->second.circuit == a.circuit) {
                release_uri = std::move(callback->second.uri);
                arrival_callbacks.erase(callback);
            }
        }
        if (!release_uri.empty() && policies.release_teleport_source)
            policies.release_teleport_source(release_uri);
        if (const auto physics = viewer_physics.load())
            physics->entered(physics_agent(a));
        else
            replay_avatar_animations(a);
        for (auto& other : viewers->agents())
            if (other.root) {
                send_avatar(a, other);
                send_appearance(a, other);
                if (other.circuit->agent_id != a.circuit->agent_id) {
                    send_avatar(other, a);
                    send_appearance(other, a);
                }
            }
        auto initial_objects = world.objects();
        std::stable_partition(initial_objects.begin(), initial_objects.end(), [](const SceneObject& object) {
            return object.parent_id == 0 || object.is_attachment;
        });
        for (const auto& object : initial_objects)
            send_object(object, a);
        constexpr std::uint32_t login_flags = (1u << 7) | (1u << 30); // ViaLogin and ViaHGLogin
        if ((a.circuit->teleport_flags & login_flags) != 0 && policies.friend_status_changed)
            policies.friend_status_changed(a.circuit->agent_id, a.circuit->session_id, true);
        if ((a.circuit->teleport_flags & login_flags) != 0 && policies.online_friends) {
            const auto online = policies.online_friends(a.circuit->agent_id);
            for (std::size_t offset = 0; offset < online.size();) {
                auto packet = lludp::make_packet("OnlineNotification");
                const auto end = std::min(online.size(), offset + 50);
                for (; offset < end; ++offset) {
                    auto agent = lludp::make_block(packet.spec->blocks[0]);
                    agent["AgentID"] = online[offset];
                    packet.blocks["AgentBlock"].push_back(std::move(agent));
                }
                viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id,
                                                        std::move(packet), ThrottleOutPacketType::Task);
            }
        }
        if ((a.circuit->teleport_flags & login_flags) != 0 && policies.pending_friendships)
            for (auto& offer : policies.pending_friendships(a.circuit->agent_id)) {
                offer.region = descriptor.id;
                deliver_instant_message(offer, a.circuit->session_id);
            }
        if (policies.entered)
            policies.entered(a);
        send_viewer_group_memberships(a);
    }
    void update_viewer_active_group(const ViewerAgent& viewer, std::optional<Uuid> selection,
                                    bool broadcast) {
        if (!policies.viewer_active_group || !viewer.root)
            return;
        const auto& circuit = *viewer.circuit;
        const auto group = policies.viewer_active_group(circuit.agent_id, selection);
        if (!group)
            return;
        const auto updated = viewers->update_active_group(circuit.agent_id, circuit.session_id, *group);
        if (!updated)
            return;
        auto reply = lludp::make_packet("AgentDataUpdate");
        auto& data = reply.block("AgentData");
        data["AgentID"] = circuit.agent_id;
        data["ActiveGroupID"] = group->id;
        data["GroupPowers"] = group->powers;
        const auto wire = [](const std::string& value) {
            return profile_string_bytes(framework::utf16(value), 255);
        };
        data["FirstName"] = wire(circuit.first_name.value_or("Unknown"));
        data["LastName"] = wire(circuit.last_name.value_or("Unknown"));
        data["GroupTitle"] = wire(group->title);
        data["GroupName"] = wire(group->name);
        viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id, std::move(reply),
                                                ThrottleOutPacketType::Task);
        if (broadcast)
            for (const auto& recipient : viewers->agents())
                if (recipient.root)
                    send_avatar(*updated, recipient);
    }
    void send_viewer_group_memberships(const ViewerAgent& viewer) {
        if (!policies.viewer_group_memberships || !viewer.root)
            return;
        const auto id = viewer.circuit->agent_id;
        auto memberships = policies.viewer_group_memberships(id);
        const auto current = viewers->agent(id);
        if (!current || current->circuit->session_id != viewer.circuit->session_id)
            return;
        if (capability_runtime && capability_runtime->event_queue()) {
            capability_runtime->event_queue()->group_membership(id, &memberships);
            return;
        }
        auto packet = lludp::make_packet("AgentGroupDataUpdate");
        packet.block("AgentData")["AgentID"] = id;
        for (const auto& group : memberships)
            packet.blocks["GroupData"].push_back(
                {{"GroupID", group->group},
                 {"GroupPowers", group->powers},
                 {"AcceptNotices", group->notices},
                 {"GroupInsigniaID", group->picture},
                 {"Contribution", std::int64_t(group->contribution)},
                 {"GroupName", profile_string_bytes(framework::utf16(group->name.value_or("")), 255)}});
        viewers->transport().enqueue_if_session(id, viewer.circuit->session_id, std::move(packet),
                                                ThrottleOutPacketType::Task);
    }
    void alert(Uuid a, const std::string& text) {
        auto p = lludp::make_packet("AlertMessage");
        std::vector<std::uint8_t> bytes(text.begin(), text.end());
        bytes.push_back(0);
        p.block("AlertData")["Message"] = std::move(bytes);
        viewers->transport().send(a, std::move(p));
    }
    void send_region_notice(const std::string& message) {
        if (!viewers)
            return;
        for (const auto& viewer : viewers->agents()) {
            if (!viewer.root)
                continue;
            const auto& circuit = *viewer.circuit;
            viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id,
                agent_alert(circuit.agent_id, message, false), ThrottleOutPacketType::Task);
        }
    }
    static bool is_hud_attachment(const SceneObject& object) {
        const auto point = object.shape.state;
        return object.is_attachment && point >= 31 && point <= 38;
    }
    bool object_visible_to(const SceneObject& object, const ViewerAgent& recipient) const {
        if (!object.is_attachment)
            return true;
        if (is_hud_attachment(object))
            return recipient.circuit->agent_id == object.owner;
        const auto wearer = attachment_wearer(object);
        return wearer && can_see_avatar(*wearer, recipient);
    }
    void send_object(const SceneObject& object, const ViewerAgent& viewer) {
        if (!object_visible_to(object, viewer))
            return;
        const auto recipient = viewer.circuit->agent_id;
        ObjectUpdateOptions options;
        options.recipient = recipient;
        options.disable_facelights = policies.disable_facelights;
        {
            std::lock_guard lock(objects_mutex);
            auto i = object_state.find(object.id);
            if (i != object_state.end()) {
                options.pseudo_crc = i->second.crc;
                options.create_selected = i->second.selected;
                if (recipient == object.owner)
                    i->second.selected = false;
            }
        }
        options.generated_client_flags =
            policies.client_flags ? policies.client_flags(recipient, object) : object.flags;
        options.click_action = object.click_action;
        options.material = object.material;
        viewers->transport().enqueue_if_session(
            recipient, viewer.circuit->session_id,
            object_update(object, descriptor.handle(), time_dilation, options), ThrottleOutPacketType::Task);
    }
    void broadcast_terse(const SceneObject& object) {
        for (const auto& viewer : viewers->agents())
            if (viewer.root && object_visible_to(object, viewer))
                viewers->transport().enqueue_if_session(viewer.circuit->agent_id, viewer.circuit->session_id,
                                                        object_terse_update(object, descriptor.handle()),
                                                        ThrottleOutPacketType::Task);
    }
    bool allow_group_position(const SceneObject& object, Vector3 position) {
        if (!object.is_attachment && policies.can_object_entry &&
            !policies.can_object_entry(object, position))
            return false;
        if (!policies.group_move || policies.group_move(object.id, position))
            return true;
        broadcast_terse(object);
        return false;
    }
    Vector3 clamp_prim_scale(Vector3 scale) const {
        const auto clamp = [&](float axis) {
            if (!(axis < policies.nonphysical_prim_max))
                return policies.nonphysical_prim_max;
            if (!(axis > policies.nonphysical_prim_min))
                return policies.nonphysical_prim_min;
            return axis;
        };
        return {clamp(scale.x), clamp(scale.y), clamp(scale.z)};
    }
    void resize_linkset(const SceneObject& root, Vector3 requested, std::optional<Vector3> position = {}) {
        constexpr std::uint32_t physics_flag = 1u;
        const bool physical = (root.flags & physics_flag) != 0;
        const float minimum = physical ? policies.physical_prim_min : policies.nonphysical_prim_min;
        const float maximum = physical ? policies.physical_prim_max : policies.nonphysical_prim_max;
        if (minimum > maximum)
            return;
        world.resize_group(root.id,
                           {std::clamp(requested.x, minimum, maximum),
                            std::clamp(requested.y, minimum, maximum),
                            std::clamp(requested.z, minimum, maximum)},
                           minimum, maximum, position);
    }
    std::vector<ViewerAgent> linkset_sitters(Uuid id) const {
        std::vector<ViewerAgent> sitters;
        if (!viewers)
            return sitters;
        const auto root = world.object(world.link_key(id, 1));
        if (!root)
            return sitters;
        for (auto& avatar : viewers->agents())
            if (avatar.root && avatar.seat && avatar.seat->parent_local == root->local_id)
                sitters.push_back(std::move(avatar));
        std::ranges::sort(sitters, {}, [](const ViewerAgent& avatar) { return avatar.seat->order; });
        return sitters;
    }
    void notify_seat_linkset(std::uint32_t parent) {
        if (!parent)
            return;
        if (policies.parcel_prim_count_tainted)
            policies.parcel_prim_count_tainted();
        std::lock_guard lock(task_data_mutex);
        if (!scripts_enabled())
            return;
        for (auto& [id, script] : scripts)
            if (script.instance)
                if (const auto part = world.object(script.item.parent);
                    part && (part->local_id == parent || part->parent_id == parent))
                    script.instance->post_event({"changed", {std::int32_t(32)}, {}});
    }
    void publish_seat_change(const ViewerAgent& avatar, std::uint32_t parent, bool send_transform = true) {
        if (send_transform)
            for (const auto& viewer : viewers->agents())
                if (viewer.root)
                    send_avatar(avatar, viewer);
        {
            std::lock_guard lock(task_data_mutex);
            auto& entries = active_animations(avatar);
            auto& animation = avatar_script_animations.at(avatar.circuit->agent_id);
            animation.default_suppressed = false;
            animation_sequence = (animation_sequence + 1) & 0x7fffffffu;
            animation.default_sequence = static_cast<std::int32_t>(animation_sequence);
            publish_avatar_animations(avatar, entries);
        }
        notify_seat_linkset(parent);
    }
    void clear_seat_camera(const ViewerAgent& avatar, Uuid root) {
        auto clear = lludp::make_packet("ClearFollowCamProperties");
        clear.block("ObjectData")["ObjectID"] = root;
        viewers->transport().enqueue_if_session(avatar.circuit->agent_id, avatar.circuit->session_id,
                                                std::move(clear), ThrottleOutPacketType::Task);
    }
    void finish_local_seat_exit(const ViewerAgent& before, const ViewerAgent& after) {
        if (before.seat)
            if (const auto root = world.object(before.seat->parent_local))
                clear_seat_camera(after, root->id);
        if (const auto physics = viewer_physics.load())
            physics->entered(physics_agent(after));
        publish_seat_change(after, before.seat ? before.seat->parent_local : 0, false);
    }
    void change_ground_sit(const ViewerAgent& avatar, bool sitting) {
        const auto updated = viewers->update_ground_sit(avatar.circuit->agent_id, avatar.circuit->session_id,
                                                         sitting);
        if (!updated)
            return;
        if (const auto physics = viewer_physics.load()) {
            if (sitting)
                physics->left(avatar);
            else
                physics->entered(physics_agent(*updated));
        }
        publish_seat_change(*updated, 0);
    }
    void stand_from_seat(const ViewerAgent& avatar, const SceneObject* removed_part = nullptr,
                         const SceneObject* removed_root = nullptr) {
        if (!avatar.seat)
            return;
        auto position = avatar.position;
        auto rotation = avatar.rotation;
        const auto root = removed_root ? std::optional(*removed_root) : world.object(avatar.seat->parent_local);
        if (root) {
            const auto offset = framework::sdk_rotate_vector(avatar.seat->offset, root->rotation);
            position = {root->position.x + offset.x, root->position.y + offset.y, root->position.z + offset.z};
            rotation = framework::sdk_multiply_quaternion(root->rotation, avatar.seat->rotation);
        }
        const auto part = removed_part ? std::optional(*removed_part) : world.object(avatar.seat->part);
        const auto appearance = avatar.appearance();
        const auto height = appearance ? appearance->avatar_height : 0.f;
        const auto actor_half_height = viewer_physics.load() && appearance
            ? appearance->avatar_box_size().z * 0.5f : 2.f;
        auto adjustment = Vector3{0.65f, 0, actor_half_height * 0.5f + 0.1f};
        auto heading = rotation;
        if (part && part->stand_target != Vector3{}) {
            position = part->position;
            heading = part->rotation;
            if (part->parent_id && root) {
                position = linked_world_position(*part, *root);
                heading = framework::sdk_multiply_quaternion(root->rotation, heading);
            }
            adjustment = part->stand_target;
            adjustment.z += height > 0 ? height * 0.5f + 0.1f : 0.9f;
        }
        const auto length = std::hypot(heading.z, heading.w);
        const auto yaw = length > 0 ? framework::Quaternion{0, 0, heading.z / length, heading.w / length}
                                    : framework::Quaternion{0, 0, 0, 1};
        adjustment = framework::sdk_rotate_vector(adjustment, yaw);
        position = {position.x + adjustment.x, position.y + adjustment.y, position.z + adjustment.z};
        const auto updated = viewers->update_seat(avatar.circuit->agent_id, avatar.circuit->session_id,
                                                   std::nullopt, position, rotation, avatar.seat->order);
        if (!updated)
            return;
        if (root)
            clear_seat_camera(avatar, root->id);
        if (const auto physics = viewer_physics.load())
            physics->entered(physics_agent(*updated));
        publish_seat_change(*updated, avatar.seat->parent_local);
    }
    static bool has_configured_sit_target(const SceneObject& part) {
        const auto& rotation = part.sit_target_orientation;
        return part.sit_target_position != Vector3{} ||
               (rotation != framework::Quaternion{} && rotation != framework::Quaternion{0, 0, 0, 1});
    }
    void request_object_seat(const ViewerAgent& avatar, Uuid target, Vector3 requested_offset) {
        if (!avatar.root || (avatar.seat && avatar.seat->part == target))
            return;
        if (avatar.seat)
            stand_from_seat(avatar);
        if (avatar.ground_sitting)
            change_ground_sit(avatar, false);
        auto part = world.object(target);
        if (!part || part->is_attachment)
            return;
        const auto root = part->parent_id ? world.object(part->parent_id) : part;
        if (!root)
            return;
        const auto agents = viewers->agents();
        const auto available = [&](const SceneObject& candidate) {
            return has_configured_sit_target(candidate) && candidate.sit_active_range >= 0 &&
                   std::none_of(agents.begin(), agents.end(), [&](const ViewerAgent& other) {
                return other.seat && other.seat->explicit_target && other.seat->part == candidate.id;
            });
        };
        const auto clicked = *part;
        if (!available(*part)) {
            part.reset();
            if (available(*root))
                part = root;
            else
                for (const auto& candidate : world.objects())
                    if (!candidate.is_attachment && candidate.parent_id == root->local_id &&
                        available(candidate) && (!part || candidate.link_order < part->link_order))
                        part = candidate;
        }
        const bool explicit_target = part.has_value();
        if (!part)
            part = clicked;
        if (part->sit_active_range < 0)
            return;
        const auto clicked_position = script_world_position(*part);
        const Vector3 requested_position{clicked_position.x + requested_offset.x,
                                         clicked_position.y + requested_offset.y,
                                         clicked_position.z + requested_offset.z};
        const auto dx = requested_position.x - avatar.position.x;
        const auto dy = requested_position.y - avatar.position.y;
        const auto dz = requested_position.z - avatar.position.z;
        const auto distance_squared = dx * dx + dy * dy + dz * dz;
        const auto range = part->sit_active_range;
        if (!std::isfinite(distance_squared) || (range > 1e-5f && distance_squared > range * range))
            return;
        auto response_offset = explicit_target ? part->sit_target_position : requested_offset;
        auto camera_eye = part->camera_eye_offset;
        auto camera_at = part->camera_at_offset;
        auto rotation = explicit_target ? part->sit_target_orientation : framework::Quaternion{0, 0, 0, 1};
        auto offset = explicit_target ? configured_seat_offset(avatar, *part) : Vector3{};
        if (part->id != root->id) {
            offset = framework::sdk_rotate_vector(offset, part->rotation);
            response_offset = framework::sdk_rotate_vector(response_offset, part->rotation);
            for (auto* value : {&offset, &response_offset}) {
                value->x += part->position.x;
                value->y += part->position.y;
                value->z += part->position.z;
            }
            rotation = framework::sdk_multiply_quaternion(part->rotation, rotation);
            if (camera_eye == Vector3{} && camera_at == Vector3{}) {
                camera_eye = root->camera_eye_offset;
                camera_at = root->camera_at_offset;
            } else {
                for (auto* camera : {&camera_eye, &camera_at}) {
                    *camera = framework::sdk_rotate_vector(*camera, part->rotation);
                    camera->x += part->position.x;
                    camera->y += part->position.y;
                    camera->z += part->position.z;
                }
            }
        }
        const auto world_offset = framework::sdk_rotate_vector(offset, root->rotation);
        Vector3 position{root->position.x + world_offset.x, root->position.y + world_offset.y,
                         root->position.z + world_offset.z};
        if (!explicit_target) {
            if (part->physics.shape == 1) {
                alert(avatar.circuit->agent_id, " There is no suitable surface to sit on, try another spot.");
                return;
            }
            if (distance_squared > 100.f)
                return;
            position = requested_position;
            const auto appearance = avatar.appearance();
            position.z += viewer_physics.load() && appearance ? appearance->avatar_box_size().z * 0.5f : 2.f;
            offset = {position.x - root->position.x, position.y - root->position.y, position.z - root->position.z};
        }
        const auto updated = viewers->update_seat(avatar.circuit->agent_id, avatar.circuit->session_id,
            ViewerAgent::Seat{part->id, root->local_id, offset,
                              explicit_target ? rotation : avatar.rotation, explicit_target}, position);
        if (!updated)
            return;
        if (const auto physics = viewer_physics.load())
            physics->left(avatar);
        auto packet = lludp::make_packet("AvatarSitResponse");
        packet.block("SitObject")["ID"] = root->id;
        auto& transform = packet.block("SitTransform");
        const auto response_rotation = framework::sdk_multiply_quaternion(root->rotation, rotation);
        transform["AutoPilot"] = true;
        transform["SitPosition"] = std::vector<double>{response_offset.x, response_offset.y, response_offset.z};
        transform["SitRotation"] = std::vector<double>{response_rotation.x, response_rotation.y,
                                                       response_rotation.z, response_rotation.w};
        transform["CameraEyeOffset"] = std::vector<double>{camera_eye.x, camera_eye.y, camera_eye.z};
        transform["CameraAtOffset"] = std::vector<double>{camera_at.x, camera_at.y, camera_at.z};
        transform["ForceMouselook"] = part->force_mouselook;
        viewers->transport().enqueue_if_session(avatar.circuit->agent_id, avatar.circuit->session_id,
                                                std::move(packet), ThrottleOutPacketType::Task);
        publish_seat_change(*updated, root->local_id);
    }
    Vector3 configured_seat_offset(const ViewerAgent& avatar, const SceneObject& part) const {
        auto q = part.sit_target_orientation;
        const auto horizontal_norm = q.x * q.x + q.y * q.y;
        const auto vertical_norm = q.z * q.z + q.w * q.w;
        const auto norm = horizontal_norm + vertical_norm;
        float correction;
        if (policies.legacy_sit_offsets) {
            correction = norm > 0.000001f ? -0.05f / norm : -0.05f;
        } else {
            if (std::abs(1.f - norm) > 0.000001f) {
                if (norm == 0.f) {
                    q = {0, 0, 0, 1};
                } else {
                    const auto inverse = 1.f / std::sqrt(norm);
                    q = {q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse};
                }
            }
            const auto appearance = avatar.appearance();
            correction = (appearance ? appearance->avatar_height : 0.f) * 0.02638f;
        }
        auto offset = part.sit_target_position;
        offset.x += 2.f * (q.x * q.z + q.y * q.w) * correction;
        offset.y += 2.f * (-q.x * q.w + q.y * q.z) * correction;
        const auto up_z = policies.legacy_sit_offsets ? vertical_norm - horizontal_norm
                                                      : -q.x * q.x - q.y * q.y + q.z * q.z + q.w * q.w;
        offset.z += up_z * correction + 0.4f;
        return offset;
    }
    void flush_objects() {
        auto events = world.drain_events();
        if (std::ranges::any_of(events, [](const auto& event) { return event.kind == WorldEventKind::Removed; })) {
            for (const auto& avatar : viewers->agents()) {
                if (!avatar.root || !avatar.seat)
                    continue;
                const SceneObject* removed_part = nullptr;
                const SceneObject* removed_root = nullptr;
                for (const auto& event : events) {
                    if (event.kind != WorldEventKind::Removed)
                        continue;
                    if (event.object.id == avatar.seat->part)
                        removed_part = &event.object;
                    if (event.object.local_id == avatar.seat->parent_local)
                        removed_root = &event.object;
                }
                if (removed_part || removed_root)
                    stand_from_seat(avatar, removed_part, removed_root);
            }
        }
        for (const auto& event : events) {
            if (event.script_changes && scripts_enabled())
                for (auto& [id, script] : scripts)
                    if (script.item.parent == event.object.id && script.instance)
                        script.instance->post_event({"changed", {event.script_changes}, {}});
            if (event.kind == WorldEventKind::TerseUpdated) {
                broadcast_terse(event.object);
                continue;
            }
            if (event.kind == WorldEventKind::Updated) {
                std::lock_guard lock(objects_mutex);
                ++object_state[event.object.id].crc;
            }
            for (const auto& viewer : viewers->agents())
                if (viewer.root) {
                    if (event.kind != WorldEventKind::Removed)
                        send_object(event.object, viewer);
                    else if (!is_hud_attachment(event.object) ||
                             viewer.circuit->agent_id == event.object.owner) {
                        auto kill = lludp::make_packet("KillObject");
                        kill.blocks["ObjectData"].push_back({{"ID", std::uint64_t(event.object.local_id)}});
                        viewers->transport().enqueue_if_session(viewer.circuit->agent_id,
                                                                viewer.circuit->session_id, std::move(kill),
                                                                ThrottleOutPacketType::Task);
                    }
                }
            if (event.kind == WorldEventKind::Removed) {
                std::lock_guard lock(objects_mutex);
                object_state.erase(event.object.id);
            }
        }
    }
    struct RegionProfileScene final : ProfileRequestScene {
        std::weak_ptr<State> owner;
        ProfileText name;
        std::uint32_t x, y;
        std::uint64_t handle;
        explicit RegionProfileScene(const std::shared_ptr<State>& region)
            : owner(region),
              name(region->descriptor.name ? ProfileText{framework::utf16_replacing(*region->descriptor.name)}
                                           : ProfileText{}),
              x(static_cast<std::uint32_t>(region->descriptor.x)),
              y(static_cast<std::uint32_t>(region->descriptor.y)), handle(region->descriptor.handle()) {}
        std::shared_ptr<ProfileRequestLand> land(float local_x, float local_y) override {
            auto region = owner.lock();
            if (!region || !region->policies.advanced_profile_land)
                return {};
            return region->policies.advanced_profile_land(local_x, local_y);
        }
        std::shared_ptr<ProfileRequestMoney> money() override {
            auto region = owner.lock();
            return region ? region->policies.advanced_profile_money : nullptr;
        }
        ProfileText region_name() override {
            return name;
        }
        ProfileText scene_name() override {
            return name;
        }
        std::uint32_t world_x() override {
            return x;
        }
        std::uint32_t world_y() override {
            return y;
        }
        std::uint64_t region_handle() override {
            return handle;
        }
    };
    std::shared_ptr<ProfileRequestScene> profile_scene;
    struct ViewerProfilePresence final : ProfileRequestPresence {
        framework::Vector3 position;
        bool god = false;
        std::shared_ptr<ProfileRequestScene> region_scene;
        std::shared_ptr<ProfileNpcClient> npc_client() override {
            return {};
        }
        bool viewer_ui_god() override {
            return god;
        }
        framework::Vector3 absolute_position() override {
            return position;
        }
        std::shared_ptr<ProfileRequestScene> scene() override {
            return region_scene;
        }
    };
    std::shared_ptr<ProfileRequestPresence> profile_presence(Uuid id, std::optional<Uuid> session = {}) {
        if (!running || !viewers)
            return {};
        auto current = viewers->agent(id);
        if (session && (!current || current->logout_requested || current->circuit->session_id != *session))
            return {};
        if (policies.advanced_profile_presence) {
            if (auto provided = policies.advanced_profile_presence(id))
                return provided;
        }
        if (!current || current->logout_requested)
            return {};
        auto presence = std::make_shared<ViewerProfilePresence>();
        presence->child = !current->root;
        presence->position = current->position;
        presence->god = policies.viewer_ui_god && policies.viewer_ui_god(id);
        presence->region_scene = profile_scene;
        if (policies.advanced_profile_parcel)
            presence->current_parcel = policies.advanced_profile_parcel(*current);
        return presence;
    }
    struct CircuitProfileClient final : ProfileViewerReplies,
                                        ProfileEvents,
                                        std::enable_shared_from_this<CircuitProfileClient> {
        std::weak_ptr<State> region;
        const ViewerAgent viewer;
        std::mutex event_mutex;
        bool closed = false;
        void close() {
            std::lock_guard lock(event_mutex);
            closed = true;
        }
        CircuitProfileClient(std::weak_ptr<State> owner, ViewerAgent agent)
            : region(std::move(owner)), viewer(std::move(agent)) {}
        Uuid agent_id() override {
            return viewer.circuit->agent_id;
        }
        bool is_active() override {
            auto owner = region.lock();
            return owner && owner->inventory_active(viewer);
        }
        std::shared_ptr<ProfileRequestPresence> scene_presence() override {
            auto owner = region.lock();
            return owner ? owner->profile_presence(agent_id(), viewer.circuit->session_id) : nullptr;
        }
        std::shared_ptr<ProfileRequestScene> scene() override {
            auto owner = region.lock();
            return owner ? owner->profile_scene : nullptr;
        }
        framework::Vector3 scene_agent_position() override {
            auto owner = region.lock();
            if (owner && owner->inventory_active(viewer)) {
                auto current = owner->viewers->agent(agent_id());
                if (current && current->circuit->session_id == viewer.circuit->session_id)
                    return current->position;
            }
            return viewer.position;
        }
        void alert(std::u16string_view message, bool modal) override {
            send_profile_packet(agent_alert(agent_id(), framework::utf8(message), modal));
        }
        bool enqueue(server::PollBytes bytes, Uuid recipient) override {
            std::lock_guard lock(event_mutex);
            auto owner = region.lock();
            if (closed || !owner || recipient != agent_id() || !owner->inventory_active(viewer) ||
                !owner->capability_runtime || !owner->capability_runtime->event_queue())
                return false;
            return owner->capability_runtime->event_queue()->enqueue(std::move(bytes), recipient);
        }

      protected:
        void send_profile_packet(lludp::Packet packet) override {
            auto owner = region.lock();
            if (owner && owner->inventory_active(viewer))
                owner->viewers->transport().enqueue_if_session(
                    agent_id(), viewer.circuit->session_id, std::move(packet), ThrottleOutPacketType::Task);
        }
        std::shared_ptr<ProfileEvents> profile_event_queue() override {
            auto owner = region.lock();
            if (!owner || !owner->inventory_active(viewer) || !owner->capability_runtime ||
                !owner->capability_runtime->event_queue())
                return {};
            return shared_from_this();
        }
    };
    std::unordered_map<Uuid, std::shared_ptr<CircuitProfileClient>, framework::UuidHash> profile_clients;
    std::shared_ptr<CircuitProfileClient> profile_client(const ViewerAgent& viewer) {
        std::lock_guard lock(actors_mutex);
        if (!inventory_active(viewer))
            return {};
        auto& client = profile_clients[viewer.circuit->agent_id];
        if (!client || client->viewer.circuit->session_id != viewer.circuit->session_id)
            client = std::make_shared<CircuitProfileClient>(weak_from_this(), viewer);
        return client;
    }
    bool inventory_active(const ViewerAgent& a) {
        if (!running || !viewers || !a.circuit)
            return false;
        auto current = viewers->agent(a.circuit->agent_id);
        return current && !current->logout_requested && current->circuit->session_id == a.circuit->session_id;
    }
    InventoryPacketSink inventory_sink(const ViewerAgent& a) {
        return [this, a](lludp::Packet packet, bool split) {
            viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id,
                                                    std::move(packet), ThrottleOutPacketType::Asset, split);
        };
    }
    bool inventory_packet(const ViewerAgent& a, const lludp::Packet& p) {
        if (!legacy_inventory)
            return false;
        if (p.spec->name == "FetchInventory") {
            InventoryCommand command;
            command.agent = a;
            for (const auto& b : p.blocks.at("InventoryData"))
                command.items.push_back({std::get<Uuid>(b.at("OwnerID")), std::get<Uuid>(b.at("ItemID"))});
            std::lock_guard lock(inventory_mutex);
            inventory_items.push_back(std::move(command));
            inventory_items_running = true;
            return true;
        }
        if (p.spec->name == "FetchInventoryDescendents") {
            const auto& b = p.block("InventoryData");
            LegacyFolderRequest request{std::get<Uuid>(b.at("FolderID")),
                                        std::get<bool>(b.at("FetchFolders")),
                                        std::get<bool>(b.at("FetchItems"))};
            if (legacy_inventory->receive_folder(a.circuit->agent_id, request, inventory_sink(a)) ==
                LegacyFolderDisposition::Queued) {
                InventoryCommand command;
                command.agent = a;
                command.folder = request;
                std::lock_guard lock(inventory_mutex);
                inventory_folders.push_back(std::move(command));
                inventory_folders_running = true;
            }
            return true;
        }
        return false;
    }
    void run_inventory() {
        if (!legacy_inventory)
            return;
        for (;;) {
            std::optional<InventoryCommand> command;
            {
                std::lock_guard lock(inventory_mutex);
                if (!inventory_items_running)
                    break;
                if (inventory_items.empty()) {
                    inventory_items_running = false;
                    break;
                }
                command = std::move(inventory_items.front());
                inventory_items.pop_front();
            }
            try {
                legacy_inventory->process_items(
                    command->agent.circuit->agent_id, command->items,
                    [&] { return inventory_active(command->agent); }, inventory_sink(command->agent));
            } catch (const std::exception& e) {
                error(std::string("Legacy item inventory: ") + e.what());
                std::lock_guard lock(inventory_mutex);
                inventory_items_running = false;
                break;
            }
        }
        if (std::chrono::steady_clock::now() < next_inventory_folder)
            return;
        for (;;) {
            std::optional<InventoryCommand> command;
            {
                std::lock_guard lock(inventory_mutex);
                if (!inventory_folders_running)
                    break;
                if (inventory_folders.empty()) {
                    inventory_folders_running = false;
                    break;
                }
                command = std::move(inventory_folders.front());
                inventory_folders.pop_front();
            }
            try {
                if (legacy_inventory->process_folder(
                        command->agent.circuit->agent_id, command->folder,
                        [&] { return inventory_active(command->agent); }, inventory_sink(command->agent))) {
                    next_inventory_folder = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
                    break;
                }
            } catch (const std::exception& e) {
                error(std::string("Legacy folder inventory: ") + e.what());
                std::lock_guard lock(inventory_mutex);
                inventory_folders_running = false;
                break;
            }
        }
    }
    static TaskInventoryFileItem file_item(const framework::TaskInventoryItem& item) {
        TaskInventoryFileItem out;
        out.item = item.item_id;
        out.asset = item.asset_id;
        out.creator = item.creator_id;
        out.last_owner = item.last_owner_id;
        out.owner = item.owner_id;
        out.group = item.group_id;
        out.creation_date = item.creation_date;
        out.flags = item.flags;
        out.base_permissions = item.base_permissions;
        out.current_permissions = item.current_permissions;
        out.group_permissions = item.group_permissions;
        out.everyone_permissions = item.everyone_permissions;
        out.next_permissions = item.next_permissions;
        out.type = item.type;
        out.inventory_type = item.inventory_type;
        out.name = item.name.value_or("");
        out.description = item.description.value_or("");
        return out;
    }
    static std::shared_ptr<framework::TaskInventoryItem> task_model(const TaskInventoryFileItem& item,
                                                                    Uuid parent) {
        auto out = std::make_shared<framework::TaskInventoryItem>();
        out->item_id = item.item;
        out->asset_id = item.asset;
        out->creator_id = item.creator;
        out->last_owner_id = item.last_owner;
        out->owner_id = item.owner;
        out->group_id = item.group;
        out->creation_date = item.creation_date;
        out->flags = item.flags;
        out->base_permissions = item.base_permissions;
        out->current_permissions = item.current_permissions;
        out->group_permissions = item.group_permissions;
        out->everyone_permissions = item.everyone_permissions;
        out->next_permissions = item.next_permissions;
        out->type = item.type;
        out->inventory_type = item.inventory_type;
        out->name = item.name;
        out->description = item.description;
        out->parent_id = out->parent_part_id = parent;
        return out;
    }
    static InventoryItem service_item(const framework::TaskInventoryItem& task) {
        InventoryItem item;
        item.id = task.item_id;
        item.asset_id = task.asset_id;
        item.creator_id = task.creator_id.str();
        item.owner = task.owner_id;
        item.group_id = task.group_id;
        item.parent = task.parent_part_id;
        item.creation_date = std::bit_cast<std::int32_t>(task.creation_date);
        item.flags = std::bit_cast<std::int32_t>(task.flags);
        item.base_permissions = std::bit_cast<std::int32_t>(task.base_permissions);
        item.current_permissions = std::bit_cast<std::int32_t>(task.current_permissions);
        item.group_permissions = std::bit_cast<std::int32_t>(task.group_permissions);
        item.everyone_permissions = std::bit_cast<std::int32_t>(task.everyone_permissions);
        item.next_permissions = std::bit_cast<std::int32_t>(task.next_permissions);
        item.asset_type = task.type;
        item.inventory_type = task.inventory_type;
        item.name = task.name.value_or("");
        item.description = task.description.value_or("");
        item.group_owned = !task.group_id.is_zero() && task.owner_id == task.group_id;
        return item;
    }
    TaskInventoryRegistry& task_inventory(Uuid object) {
        auto& part = task_parts[object];
        if (!part.inventory) {
            auto model = world.object(object);
            if (!model)
                throw std::runtime_error("Task inventory part disappeared");
            part.scripted = (model->flags & 0x40u) != 0;
            TaskInventoryRegistryHooks hooks;
            hooks.part = [this, object] {
                auto part = world.object(object);
                if (!part)
                    throw std::runtime_error("Task inventory part disappeared");
                return TaskInventoryPart{object, part->group, part->local_id};
            };
            hooks.changed = [this, object](std::uint32_t mask, std::optional<Uuid>) {
                if (!scripts_enabled())
                    return;
                for (auto& [id, script] : scripts)
                    if (script.item.parent == object && script.instance)
                        script.instance->post_event({"changed", {std::bit_cast<std::int32_t>(mask)}, {}});
            };
            hooks.aggregate_permissions = [this, object] { refresh_script_flags(object); };
            hooks.deep_permissions = [] {};
            hooks.clear_scripted = [this, object] {
                task_parts.at(object).scripted = false;
                refresh_script_flags(object);
            };
            hooks.schedule_full = hooks.group_changed = [this, object] { refresh_script_flags(object); };
            hooks.remove_script = [this, object](std::uint32_t, Uuid item) {
                script_events(object).remove(item);
                if (auto entry = scripts.find(item); entry != scripts.end()) {
                    replace_script_instance(entry->second);
                    scripts.erase(entry);
                }
                refresh_script_flags(object);
            };
            hooks.log = [this](const auto& text) { error(text); };
            part.inventory = std::make_unique<TaskInventoryRegistry>(std::move(hooks));
        }
        return *part.inventory;
    }
    std::shared_ptr<framework::TaskInventoryItem> add_task_script(const InventoryItem& item) {
        std::lock_guard data_lock(task_data_mutex);
        TaskInventoryFileItem task;
        task.item = item.id;
        task.asset = item.asset_id;
        framework::try_parse_sdk_uuid(item.creator_id, task.creator);
        task.owner = item.owner;
        task.group = item.group_id;
        task.creation_date = static_cast<std::uint32_t>(item.creation_date);
        task.flags = static_cast<std::uint32_t>(item.flags);
        task.base_permissions = static_cast<std::uint32_t>(item.base_permissions);
        task.current_permissions = static_cast<std::uint32_t>(item.current_permissions);
        task.group_permissions = 0;
        task.everyone_permissions = static_cast<std::uint32_t>(item.everyone_permissions);
        task.next_permissions = static_cast<std::uint32_t>(item.next_permissions);
        task.type = item.asset_type;
        task.inventory_type = item.inventory_type;
        task.name = item.name;
        task.description = item.description;
        auto model = task_model(task, item.parent);
        task_inventory(item.parent).add(model, false);
        return model;
    }
    void request_task_file(const ViewerAgent& actor, std::uint32_t local) {
        std::lock_guard data_lock(task_data_mutex);
        auto object = world.object(local);
        if (!object || !xfer)
            return;
        auto& inventory = task_inventory(object->id);
        auto& part = task_parts.at(object->id);
        if (!part.file) {
            TaskInventoryFileBackend backend;
            backend.can_edit_inventory = [this](Uuid id, Uuid agent) {
                auto object = world.object(id);
                if (!object)
                    return false;
                auto& policy = policies.can_edit_inventory ? policies.can_edit_inventory : policies.can_edit;
                return policy && policy(agent, *object);
            };
            backend.add_file = [this](const auto& filename, auto bytes) {
                return xfer->add(filename, std::move(bytes));
            };
            backend.reply = [this](Uuid id, std::int16_t serial, std::vector<std::uint8_t> filename) {
                if (!task_recipient)
                    return;
                auto p = task_inventory_reply(id, serial, filename);
                viewers->transport().enqueue_if_session(task_recipient->circuit->agent_id,
                                                        task_recipient->circuit->session_id, std::move(p),
                                                        ThrottleOutPacketType::Task, false);
            };
            part.file = std::make_unique<TaskInventoryFile>(object->id, std::move(backend));
        }
        std::vector<TaskInventoryFileItem> snapshot;
        for (const auto& item : inventory.items())
            snapshot.push_back(file_item(*item));
        task_recipient = actor;
        try {
            part.file->request(actor.circuit->agent_id, inventory.serial(), snapshot,
                               policies.viewer_ui_god && policies.viewer_ui_god(actor.circuit->agent_id));
        } catch (...) {
            task_recipient.reset();
            throw;
        }
        task_recipient.reset();
    }
    std::optional<AgentInventoryClient> inventory_client(Uuid agent) {
        auto actor = viewers->agent(agent);
        if (!actor || !inventory_active(*actor))
            return {};
        AgentInventoryClient client;
        client.agent = agent;
        auto session = actor->circuit->session_id;
        client.remove_items = [this, actor = *actor, agent, session](const std::vector<Uuid>& ids) {
            if (!inventory_active(actor))
                return;
            InventoryNotificationBackend backend;
            if (capability_runtime && capability_runtime->event_queue())
                backend.enqueue = [this](auto bytes, Uuid recipient) {
                    return capability_runtime->event_queue()->enqueue(std::move(bytes), recipient);
                };
            backend.debug = [this](auto text) { debug(text); };
            send_remove_inventory_items(agent, session, &ids, backend);
        };
        client.bulk_item = [this, actor = *actor, agent](auto item) {
            if (inventory_active(actor) && capability_runtime && capability_runtime->event_queue())
                capability_runtime->event_queue()->bulk_inventory(&item->item, agent);
        };
        client.alert = [this, actor = *actor, agent, session](const std::string& text, bool modal) {
            if (inventory_active(actor))
                viewers->transport().enqueue_if_session(agent, session, agent_alert(agent, text, modal),
                                                        ThrottleOutPacketType::Task);
        };
        client.create_update = [this, actor = *actor, agent, session](auto item, std::uint32_t callback) {
            if (inventory_active(actor))
                viewers->transport().enqueue_if_session(
                    agent, session, inventory_item_create(agent, item->item, {}, callback),
                    ThrottleOutPacketType::Asset);
        };
        return client;
    }
    AgentInventoryAddController agent_inventory_add() {
        if (!policies.inventory_service)
            throw std::runtime_error("Task inventory transfer requires an InventoryService");
        AgentInventoryAddBackend backend;
        backend.add_item = [this](auto item) {
            return policies.inventory_service->add_item(inventory_storage_projection(*item));
        };
        backend.system_folder = [this](Uuid agent, std::int8_t type) {
            return policies.inventory_service->for_type(agent, type);
        };
        backend.root_folder = [this](Uuid agent) { return policies.inventory_service->root(agent); };
        backend.folder = [this](Uuid agent, Uuid id) {
            return policies.inventory_service->folder(agent, id);
        };
        backend.is_god = [this](Uuid agent) {
            return policies.inventory_is_god && policies.inventory_is_god(agent);
        };
        backend.upload_complete = [this](auto item, int level) {
            if (policies.inventory_upload_complete)
                policies.inventory_upload_complete(item->item, level);
        };
        backend.client = [this](Uuid agent) { return inventory_client(agent); };
        backend.debug = backend.warn = [this](auto text) { debug(text); };
        return AgentInventoryAddController(std::move(backend));
    }
    TaskInventoryMoveController task_inventory_move() {
        TaskInventoryMoveBackend backend;
        backend.part = [this](std::uint32_t local) -> std::optional<TaskInventoryMovePart> {
            auto part = world.object(local);
            if (!part)
                return {};
            return TaskInventoryMovePart{part->id, part->owner, local};
        };
        backend.task_item = [this](const auto& part, Uuid key) { return task_inventory(part.id).find(key); };
        backend.can_copy = [this](Uuid key, Uuid object, Uuid agent) {
            if (policies.can_copy_task_inventory)
                return policies.can_copy_task_inventory(key, object, agent);
            auto part = world.object(object);
            return part && part->owner == agent;
        };
        backend.propagate_permissions = [this] { return policies.propagate_permissions; };
        backend.bypass_permissions = [this] { return policies.bypass_inventory_permissions; };
        backend.unix_time = []() {
            return std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count()));
        };
        backend.add_client = [this](const auto& client, auto item) {
            auto target = inventory_client(client.agent);
            if (!target)
                throw std::runtime_error("Inventory transfer client disconnected");
            agent_inventory_add().add(*target, std::move(item));
        };
        backend.add_offline = [this](auto item) { agent_inventory_add().add(std::move(item)); };
        backend.client = [this](Uuid agent) -> std::optional<TaskInventoryMoveClient> {
            auto client = inventory_client(agent);
            if (!client)
                return {};
            return TaskInventoryMoveClient{agent, [client = *client](const auto& text, bool modal) {
                                               client.alert(text.value_or(""), modal);
                                           }};
        };
        backend.remove_script_events = [this](const auto& part, Uuid key) {
            script_events(part.id).remove(key);
        };
        backend.active_script_count = [](const auto&, int) {};
        backend.remove_item = [this](const auto& part, Uuid key) { task_inventory(part.id).remove(key); };
        backend.debug = backend.warn = backend.error = [this](auto text) { debug(text); };
        return TaskInventoryMoveController(std::move(backend));
    }
    RezLocationBackend rez_geometry() {
        const auto intersect = [this](const SceneObject& part, Vector3 origin, Vector3 direction, bool front,
                                      bool center) {
            auto position = part.position;
            auto rotation = part.rotation;
            if (part.is_attachment) {
                const auto wearer = attachment_wearer(part);
                if (!wearer)
                    return RezIntersection{};
                const auto offset = framework::sdk_rotate_vector(part.position, part.rotation);
                position = {wearer->position.x + offset.x, wearer->position.y + offset.y,
                            wearer->position.z + offset.z};
                rotation = framework::sdk_multiply_quaternion(part.rotation, part.rotation);
            } else if (part.parent_id) {
                const auto root = world.object(part.parent_id);
                if (!root)
                    return RezIntersection{};
                position = linked_world_position(part, *root);
                rotation = framework::sdk_multiply_quaternion(root->rotation, rotation);
            }
            return intersect_rez_box(position, rotation, part.scale, origin, direction, front, center);
        };
        RezLocationBackend geometry;
        geometry.water_height = water_height;
        geometry.target_intersection = [this, intersect](Uuid target, Vector3 origin, Vector3 direction, auto,
                                                         bool front,
                                                         bool center) -> std::optional<RezIntersection> {
            const auto part = world.object(target);
            if (!part)
                return std::nullopt;
            return intersect(*part, origin, direction, front, center);
        };
        geometry.closest_intersection = [this, intersect](Vector3 origin, Vector3 direction, bool front,
                                                          bool center) {
            RezIntersection closest;
            auto distance = 280.f * 280.f;
            for (const auto& part : world.objects()) {
                if (part.shape.pcode != 9)
                    continue;
                const auto hit = intersect(part, origin, direction, front, center);
                if (!hit.hit)
                    continue;
                const auto x = hit.point.x - origin.x, y = hit.point.y - origin.y, z = hit.point.z - origin.z;
                const auto squared = x * x + y * y + z * z;
                if (squared < distance) {
                    closest = hit;
                    distance = squared;
                }
            }
            return closest;
        };
        return geometry;
    }
    void rez_inventory_object(const ViewerAgent& actor, const lludp::Packet& packet) {
        if (!policies.load_asset)
            return;
        const auto agent = actor.circuit->agent_id;
        const auto& fields = packet.block("RezData");
        const auto source = std::get<Uuid>(fields.at("FromTaskID"));
        const auto item_id = std::get<Uuid>(packet.block("InventoryData").at("ItemID"));
        std::lock_guard lock(task_data_mutex);
        std::optional<InventoryItem> item;
        TaskInventoryRegistry* source_inventory = nullptr;
        if (source.is_zero()) {
            if (!policies.inventory_service)
                return;
            item = policies.inventory_service->item(agent, item_id);
            if (!item || item->owner != agent || item->asset_type != 6)
                return;
        } else {
            if (!can_edit_inventory(source, agent))
                return;
            const auto found = task_parts.find(source);
            if (found == task_parts.end() || !found->second.inventory)
                return;
            source_inventory = found->second.inventory.get();
            const auto entry = source_inventory->find(item_id);
            if (!entry || entry->inventory_type != 6 || entry->type != 6)
                return;
            item = service_item(*entry);
        }
        const auto asset = policies.load_asset(item->asset_id);
        if (!asset || !asset->data)
            return;
        auto object_asset = read_object_asset_xml(*asset->data);
        if (!object_asset)
            return;
        auto& linksets = object_asset->linksets;
        const bool multiple_linksets = linksets.size() > 1;
        const auto& first_root = linksets.front().parts.front().object;
        const auto vector = [&](const char* name) {
            const auto& v = std::get<std::vector<double>>(fields.at(name));
            return Vector3{static_cast<float>(v.at(0)), static_cast<float>(v.at(1)),
                           static_cast<float>(v.at(2))};
        };
        const auto finite = [](Vector3 v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        };
        RezLocationRequest ray;
        ray.ray_start = vector("RayStart");
        ray.ray_end = vector("RayEnd");
        ray.ray_end_is_intersection = std::get<bool>(fields.at("RayEndIsIntersection"));
        ray.bypass_raycast = static_cast<std::uint8_t>(std::get<std::uint64_t>(fields.at("BypassRaycast")));
        ray.target = std::get<Uuid>(fields.at("RayTargetID"));
        float height_offset = 0.f;
        if (!source_inventory) {
            if (object_asset->coalesced)
                ray.scale = object_asset->bounds;
            else {
                const auto infinity = std::numeric_limits<float>::infinity();
                Vector3 lower{infinity, infinity, infinity}, upper{-infinity, -infinity, -infinity};
                for (const auto& part : linksets.front().parts) {
                    const auto& object = part.object;
                    const bool is_root = &object == &first_root;
                    const auto offset =
                        is_root ? Vector3{}
                                : framework::sdk_rotate_vector(object.position, first_root.rotation);
                    const auto rotation =
                        is_root ? object.rotation
                                : framework::sdk_multiply_quaternion(first_root.rotation, object.rotation);
                    const auto x = framework::sdk_rotate_vector({object.scale.x * .5f, 0, 0}, rotation);
                    const auto y = framework::sdk_rotate_vector({0, object.scale.y * .5f, 0}, rotation);
                    const auto z = framework::sdk_rotate_vector({0, 0, object.scale.z * .5f}, rotation);
                    const Vector3 extent{std::abs(x.x) + std::abs(y.x) + std::abs(z.x),
                                         std::abs(x.y) + std::abs(y.y) + std::abs(z.y),
                                         std::abs(x.z) + std::abs(y.z) + std::abs(z.z)};
                    lower = {std::min(lower.x, offset.x - extent.x), std::min(lower.y, offset.y - extent.y),
                             std::min(lower.z, offset.z - extent.z)};
                    upper = {std::max(upper.x, offset.x + extent.x), std::max(upper.y, offset.y + extent.y),
                             std::max(upper.z, offset.z + extent.z)};
                }
                ray.scale = {upper.x - lower.x, upper.y - lower.y, upper.z - lower.z};
                height_offset = -(upper.z + lower.z) * .5f;
            }
        }
        ray.rotation = first_root.rotation;
        if (!finite(ray.ray_start) || !finite(ray.ray_end))
            return;
        if (!finite(ray.scale))
            return;
        auto location = get_new_rez_location(ray, rez_geometry());
        location.z += height_offset;
        for (auto& linkset : linksets) {
            auto& parts = linkset.parts;
            auto& root = parts.front().object;
            for (const auto& part : parts) {
                const auto& object = part.object;
                if (object.shape.pcode != 9 || !finite(object.position) || !finite(object.scale) ||
                    object.scale.x <= 0 || object.scale.y <= 0 || object.scale.z <= 0)
                    return;
            }
            root.position = location;
            if (multiple_linksets || (!source_inventory && object_asset->coalesced)) {
                const auto bounds = object_asset->bounds;
                root.position = {location.x + linkset.offset.x - bounds.x * .5f,
                                 location.y + linkset.offset.y - bounds.y * .5f,
                                 location.z + linkset.offset.z - bounds.z * .5f};
            } else if (source_inventory && parts.size() > 1) {
                Vector3 center{};
                for (std::size_t index = 1; index < parts.size(); ++index) {
                    const auto position = parts[index].object.position;
                    center.x += position.x;
                    center.y += position.y;
                    center.z += position.z;
                }
                const auto count = static_cast<float>(parts.size());
                center = framework::sdk_rotate_vector({center.x / count, center.y / count, center.z / count},
                                                      root.rotation);
                root.position = {root.position.x - center.x, root.position.y - center.y,
                                 root.position.z - center.z};
            }
            if (source_inventory && object_asset->coalesced && !multiple_linksets) {
                root.position.x += linkset.offset.x;
                root.position.y += linkset.offset.y;
                root.position.z += linkset.offset.z;
            }
            std::string reason;
            if (!finite(root.position) || !policies.can_rez ||
                !policies.can_rez(agent, root.position, reason)) {
                alert(agent, "You cannot rez objects here.");
                return;
            }
            if (!multiple_linksets) {
                root.name = item->name;
                root.description = item->description;
            }
        }
        const auto group = std::get<Uuid>(packet.block("AgentData").at("GroupID"));
        const auto base = static_cast<std::uint32_t>(item->base_permissions);
        const auto current = static_cast<std::uint32_t>(item->current_permissions);
        const auto flags = static_cast<std::uint32_t>(item->flags);
        constexpr std::uint32_t slam_sale = 0x200u, slam_permissions = 0x100u, overwrite_group = 0x40000u,
                                overwrite_everyone = 0x80000u, overwrite_next = 0x100000u,
                                multiple_items = 0x200000u;
        constexpr std::uint32_t copy = 1u << 15;
        const bool consume = !(current & copy) && !policies.bypass_inventory_permissions;
        std::vector<Uuid> created_roots;
        const auto rollback = [&] {
            for (const auto id : created_roots)
                world.remove(id, false);
        };
        std::vector<std::pair<Uuid, Uuid>> scripts_to_start;
        for (auto& linkset : linksets) {
            auto& root = linkset.parts.front().object;
            const bool slam = root.owner != agent || (current & 0x10u) || (flags & slam_permissions);
            const bool propagate_slam = slam && policies.propagate_permissions;
            if (!source_inventory && (flags & slam_sale)) {
                root.sale_type = static_cast<std::uint8_t>(item->sale_type);
                root.sale_price = item->sale_price;
            }
            if (!source_inventory)
                root.rez_start_string.reset();
            if (!source_inventory && slam) {
                root.sale_type = 0;
                root.sale_price = 10;
            }
            if (!source_inventory && !consume)
                root.from_inventory_item = item->id;
            std::uint32_t root_local = 0;
            for (auto& part : linkset.parts) {
                auto& object = part.object;
                object.id = Uuid::random();
                object.local_id = 0;
                object.parent_id = root_local;
                object.is_attachment = false;
                object.shape.state = 0;
                const bool owner_changes = object.owner != agent;
                if (owner_changes && object.owner != object.group)
                    object.last_owner = object.owner;
                object.owner = object.rezzer = agent;
                object.group = group;
                const auto next = static_cast<std::uint32_t>(item->next_permissions);
                const auto everyone = static_cast<std::uint32_t>(item->everyone_permissions);
                const auto group_mask = static_cast<std::uint32_t>(item->group_permissions);
                if (source_inventory) {
                    if (flags & overwrite_next)
                        object.next_mask = next;
                    if (propagate_slam) {
                        object.base_mask &= object.next_mask | 0x10000u;
                        object.owner_mask &= object.next_mask;
                        object.everyone_mask &= object.next_mask | 0x10000u;
                        object.group_mask = 0;
                    }
                    if (flags & overwrite_everyone)
                        object.everyone_mask = everyone;
                    if (flags & overwrite_group)
                        object.group_mask = group_mask;
                } else if (propagate_slam) {
                    const auto folded = base & 0xfu;
                    object.next_mask = folded ? ((folded << 13) | 0x80000u) : next;
                    object.base_mask &= object.next_mask | 0x10000u;
                    object.owner_mask &= object.next_mask;
                    object.everyone_mask &= object.next_mask | 0x10000u;
                    object.group_mask = 0;
                    if (!(flags & multiple_items)) {
                        object.group_mask = group_mask & object.base_mask;
                        object.everyone_mask = everyone & object.base_mask;
                        object.next_mask = next & object.base_mask;
                    }
                } else if (!slam) {
                    if (flags & overwrite_group)
                        object.group_mask = group_mask;
                    if (flags & overwrite_everyone)
                        object.everyone_mask = everyone;
                    if (flags & overwrite_next)
                        object.next_mask = next;
                }
                const auto created = world.add(object, true, false);
                if (!created) {
                    rollback();
                    return;
                }
                if (!root_local) {
                    root_local = created->local_id;
                    created_roots.push_back(root.id);
                }
                if (!part.inventory.empty()) {
                    std::vector<TaskInventoryRegistry::Item> inventory;
                    for (auto& entry : part.inventory) {
                        entry.reset_ids(object.id);
                        const bool change_inventory_owner = source_inventory ? owner_changes : propagate_slam;
                        if (change_inventory_owner) {
                            if (entry.owner_id != agent)
                                entry.last_owner_id = entry.owner_id;
                            entry.owner_id = agent;
                        }
                        if (propagate_slam) {
                            entry.base_permissions &= entry.next_permissions;
                            entry.current_permissions &= entry.next_permissions;
                            entry.everyone_permissions &= entry.next_permissions;
                        }
                        if (change_inventory_owner || propagate_slam) {
                            entry.permissions_granter = {};
                            entry.permissions_mask = 0;
                            entry.owner_changed = true;
                        }
                        inventory.push_back(std::make_shared<framework::TaskInventoryItem>(std::move(entry)));
                    }
                    auto& registry = task_inventory(object.id);
                    registry.restore(inventory);
                    registry.serial(part.inventory_serial);
                    for (const auto& entry : inventory)
                        if (entry->inventory_type == 10)
                            scripts_to_start.emplace_back(object.id, entry->item_id);
                }
            }
        }
        if (consume) {
            if (source_inventory)
                source_inventory->remove(item_id);
            else {
                bool removed = false;
                try {
                    removed = policies.inventory_service->delete_items(agent, {item->id});
                } catch (...) {
                    rollback();
                    throw;
                }
                if (!removed) {
                    rollback();
                    return;
                }
                if (auto client = inventory_client(agent))
                    client->remove_items({item->id});
            }
        }
        const bool selected = source_inventory || std::get<bool>(fields.at("RezSelected"));
        {
            std::lock_guard state_lock(objects_mutex);
            for (const auto& linkset : linksets)
                for (const auto& part : linkset.parts)
                    initialize_object_update(part.object.id, selected);
        }
        for (const auto id : created_roots) {
            if (selected)
                world.set_selected(id, true);
            world.publish_added_linkset(id);
        }
        for (const auto& [object, script] : scripts_to_start) {
            const auto diagnostics = compile_task(object, script);
            for (const auto& diagnostic : diagnostics)
                error("Native script " + script.str() + ": " + diagnostic);
            const auto started = scripts.find(script);
            if (diagnostics.empty() && started != scripts.end() && started->second.instance &&
                started->second.instance->running())
                started->second.instance->post_event({"on_rez", {std::int32_t{0}}, {}});
        }
    }
    void initialize_object_update(Uuid id, bool selected) {
        const auto ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                                   .count() /
                               100 +
                           621355968000000000LL;
        object_state[id] = {static_cast<std::uint32_t>(ticks), selected};
    }
    void buy_object_copy(const ViewerAgent& actor, const SceneObject& root, Uuid folder) {
        const auto agent = actor.circuit->agent_id;
        const auto client = inventory_client(agent);
        if (!client)
            return;
        constexpr std::uint32_t transfer = 1u << 13, modify = 1u << 14, copy = 1u << 15,
                                export_bit = 1u << 16, move = 1u << 19;
        ObjectAssetPart original{root, 0, {}};
        std::vector<ObjectAssetPart> children;
        for (auto& part : world.objects())
            if (part.id != root.id && !part.is_attachment && root.local_id && part.parent_id == root.local_id)
                children.push_back({std::move(part), 0, {}});
        std::sort(children.begin(), children.end(), [](const auto& left, const auto& right) {
            return left.object.link_order < right.object.link_order;
        });
        auto effective = root.owner_mask, owner_mask = root.owner_mask;
        std::uint32_t folded = modify | copy | move | transfer | 0xfu;
        {
            std::lock_guard lock(task_data_mutex);
            const auto snapshot = [&](ObjectAssetPart& part) {
                owner_mask &= part.object.base_mask;
                const auto found = task_parts.find(part.object.id);
                if (found == task_parts.end() || !found->second.inventory)
                    return;
                const auto& inventory = *found->second.inventory;
                part.inventory_serial = inventory.serial();
                for (const auto& item : inventory.items()) {
                    part.inventory.push_back(item->clone());
                    if (item->inventory_type == 3)
                        continue;
                    effective &= item->current_permissions;
                    folded &= item->current_permissions | ~(copy | transfer | export_bit);
                    folded &= ((item->current_permissions & item->next_permissions) >> 13) | ~0xdu;
                }
            };
            snapshot(original);
            for (auto& part : children)
                snapshot(part);
        }
        if (!(effective & (copy | transfer)))
            effective |= transfer;
        effective &= root.base_mask;
        if (!(effective & transfer)) {
            client->alert("This item doesn't appear to be for sale", false);
            return;
        }
        if (!(effective & copy)) {
            client->alert("This sale has been blocked by the permissions system", false);
            return;
        }
        const auto unavailable = [&] {
            client->alert("Cannot buy now. Your inventory is unavailable", false);
        };
        if (!policies.inventory_service || !policies.store_inventory_asset) {
            unavailable();
            return;
        }
        original.object.sale_type = 0;
        original.object.sale_price = 10;
        original.object.click_action = 0;
        std::u16string xml;
        framework::XmlTextWriterIo io;
        io.encoding = [] { return std::shared_ptr<framework::NativeXmlTextEncoding>{}; };
        io.write = [&](auto, auto text) {
            if (text)
                xml.append(*text);
        };
        io.write_array = [&](auto data, int offset, int count) { xml.append(data->data() + offset, count); };
        io.write_whole_array = [&](auto data) { xml.append(data->data(), data->size()); };
        framework::NativeXmlTextWriter writer(std::move(io));
        write_object_asset_xml(writer, original, children);
        const auto utf8 = framework::utf8(xml);
        auto asset = std::make_shared<framework::AssetBase>(Uuid::random(), root.name, 6, root.creator.str());
        asset->description(root.description);
        asset->data = std::make_shared<std::vector<std::uint8_t>>(utf8.begin(), utf8.end());
        const auto stored = policies.store_inventory_asset(asset);
        auto value = std::make_shared<TaskInventoryAgentItem>();
        auto& item = value->item;
        if (!stored || !framework::try_parse_sdk_uuid(*stored, item.asset_id) || item.asset_id.is_zero()) {
            unavailable();
            return;
        }
        item.id = Uuid::random();
        item.owner = agent;
        item.creator_id = root.creator.str();
        item.name = root.name;
        item.description = root.description;
        item.asset_type = item.inventory_type = 6;
        item.parent = folder;
        folded &= owner_mask | ~(modify | copy | transfer | export_bit);
        framework::PermissionsUtil::apply_no_mod_folded_permissions(folded, folded);
        const auto permissions =
            framework::PermissionsUtil::fix_and_fold_permissions(folded & root.next_mask);
        item.base_permissions = item.current_permissions = std::bit_cast<std::int32_t>(permissions);
        item.next_permissions = std::bit_cast<std::int32_t>(permissions & root.next_mask);
        item.everyone_permissions = std::bit_cast<std::int32_t>(permissions & root.everyone_mask);
        item.group_permissions = std::bit_cast<std::int32_t>(permissions & root.group_mask);
        item.flags = 0x100;
        item.creation_date = user_inventory_unix_time();
        if (agent_inventory_add().add(value))
            client->create_update(value, 0);
        else
            unavailable();
    }
    void buy_objects(const ViewerAgent& a, const lludp::Packet& p) {
        if (!policies.sample_economy_data)
            return;
        const auto id = a.circuit->agent_id;
        const auto blocks = p.blocks.find("ObjectData");
        if (blocks == p.blocks.end())
            return;
        for (const auto& block : blocks->second) {
            const auto price = std::get<std::int64_t>(block.at("SalePrice"));
            if (!policies.sample_economy_sell_enabled || price != 0) {
                RegionInstantMessage notice;
                notice.recipient = id;
                notice.dialog = 1;
                notice.message = policies.sample_economy_sell_enabled
                                     ? u"Buying anything for a price other than zero is not implemented"
                                     : u"Buying is not implemented in this version";
                deliver_instant_message(notice, a.circuit->session_id);
                continue;
            }
            const auto object =
                world.object(static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID"))));
            if (object && object->parent_id && !object->is_attachment)
                continue;
            std::string error;
            if (!object)
                error = "Unable to buy now. The object was not found.";
            else if (object->sale_type == 0)
                error = "Object " + object->name + " is not for sale";
            else if (object->sale_price != price)
                error = "Object " + object->name + " price does not match selected price";
            else if (object->sale_type != std::get<std::uint64_t>(block.at("SaleType")))
                error = "Object " + object->name + " sell type does not match selected type";
            if (!error.empty()) {
                auto notice = lludp::make_packet("AgentAlertMessage");
                notice.block("AgentData")["AgentID"] = id;
                notice.block("AlertData")["Message"] =
                    profile_string_bytes(framework::utf16("/" + error), 255);
                viewers->transport().enqueue_if_session(id, a.circuit->session_id, std::move(notice),
                                                        ThrottleOutPacketType::Task);
            } else if (object->sale_type == 1)
                buy_original_object(a, *object);
            else if (object->sale_type == 2)
                buy_object_copy(a, *object, std::get<Uuid>(p.block("AgentData").at("CategoryID")));
            else if (object->sale_type == 3)
                buy_object_contents(a, *object);
        }
    }
    void buy_original_object(const ViewerAgent& actor, const SceneObject& root) {
        const auto owner = actor.circuit->agent_id;
        std::lock_guard lock(task_data_mutex);
        auto parts = world.objects();
        std::erase_if(parts, [&](const auto& part) {
            return part.id != root.id &&
                   (part.is_attachment || !root.local_id || part.parent_id != root.local_id);
        });
        auto effective = root.owner_mask;
        constexpr std::uint32_t transfer = 1u << 13, copy = 1u << 15;
        for (const auto& part : parts) {
            const auto found = task_parts.find(part.id);
            if (found == task_parts.end() || !found->second.inventory)
                continue;
            for (const auto& item : found->second.inventory->items())
                if (item->inventory_type != 3)
                    effective &= item->current_permissions;
        }
        if (!(effective & (copy | transfer)))
            effective |= transfer;
        if (!(effective & root.base_mask & transfer)) {
            viewers->transport().enqueue_if_session(
                owner, actor.circuit->session_id,
                agent_alert(owner, "This item doesn't appear to be for sale", false),
                ThrottleOutPacketType::Task);
            return;
        }
        const bool propagate = policies.propagate_permissions;
        const bool next = propagate || (owner != root.owner && !policies.bypass_sale_permissions);
        if (!world.sell_original(root, owner, actor.active_group_id(), next, propagate))
            return;
        if (next) {
            for (const auto& part : parts) {
                const auto found = task_parts.find(part.id);
                if (found == task_parts.end() || !found->second.inventory)
                    continue;
                auto& inventory = *found->second.inventory;
                const auto items = inventory.items();
                if (items.empty())
                    continue;
                for (const auto& item : items) {
                    if (propagate && item->owner_id != owner) {
                        item->last_owner_id = item->owner_id;
                        item->owner_id = owner;
                    }
                    item->base_permissions &= item->next_permissions;
                    item->current_permissions &= item->next_permissions;
                    item->everyone_permissions &= item->next_permissions;
                    item->permissions_mask = 0;
                    item->permissions_granter = {};
                    item->owner_changed = true;
                }
                inventory.inventory_changed(true);
                if (propagate)
                    inventory.serial(inventory.serial() + 1);
            }
        }
        send_part_properties(root.id, owner);
    }
    void buy_object_contents(const ViewerAgent& actor, const SceneObject& object) {
        if (!policies.inventory_service)
            return;
        const auto agent = actor.circuit->agent_id;
        const auto client = inventory_client(agent);
        if (!client)
            return;
        auto& service = *policies.inventory_service;
        std::lock_guard lock(task_data_mutex);
        const auto items = task_inventory(object.id).items();
        if (items.empty())
            return;
        constexpr std::uint32_t transfer = 1u << 13;
        for (const auto& item : items) {
            if (!(item->current_permissions & transfer)) {
                client->alert("This item's inventory doesn't appear to be for sale", false);
                return;
            }
        }
        const auto root = service.root(agent);
        if (!root)
            return;
        const services::InventoryFolder folder{object.name,    -1,    root->version,
                                               Uuid::random(), agent, root->id};
        if (!service.add_folder(folder))
            return;
        auto mover = task_inventory_move();
        auto add = agent_inventory_add();
        const TaskInventoryMovePart part{object.id, object.owner, object.local_id};
        for (const auto& item : items) {
            auto result = mover.create_agent_item(agent, part, item->item_id);
            if (!result.item) {
                client->alert(result.message.value_or(""), false);
                continue;
            }
            result.item->item.parent = folder.id;
            if (add.add(result.item))
                mover.remove_noncopy(part, item->item_id);
        }
        for (const auto& id : {root->id, folder.id}) {
            const auto containing = service.folder(agent, id);
            if (!containing)
                continue;
            const auto contents = service.content(agent, id);
            const std::vector<std::optional<InventoryItem>> entries(contents.items.begin(),
                                                                    contents.items.end());
            const std::vector<std::optional<services::InventoryFolder>> folders(contents.folders.begin(),
                                                                                contents.folders.end());
            inventory_folder_details(agent, agent, id, entries, folders, containing->version,
                                     static_cast<int>(entries.size() + folders.size()), id == root->id,
                                     id == folder.id, inventory_sink(actor));
        }
    }
    void transfer_task_inventory(const ViewerAgent& actor, const lludp::Packet& packet) {
        if (!policies.inventory_service)
            throw std::runtime_error("Task inventory transfer requires an InventoryService");
        std::lock_guard lock(task_data_mutex);
        auto agent = actor.circuit->agent_id, session = actor.circuit->session_id;
        auto send_alert = [this, actor, agent, session](const std::optional<std::string>& text, bool modal) {
            if (inventory_active(actor))
                viewers->transport().enqueue_if_session(agent, session, agent_alert(agent, text, modal),
                                                        ThrottleOutPacketType::Task);
        };
        const auto& data = packet.block("InventoryData");
        auto local = static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("LocalID")));
        auto key = std::get<Uuid>(data.at("ItemID"));
        if (packet.spec->name == "MoveTaskInventory") {
            task_inventory_move().client_move(
                {agent, send_alert}, std::get<Uuid>(packet.block("AgentData").at("FolderID")), local, key);
            return;
        }
        TaskInventoryRemoveBackend backend;
        backend.part = [this](std::uint32_t local) -> std::optional<TaskInventoryRemovePart> {
            auto part = world.object(local);
            if (!part)
                return {};
            return TaskInventoryRemovePart{part->id, true};
        };
        backend.can_edit = [this](Uuid object, Uuid agent) { return can_edit_inventory(object, agent); };
        backend.group_item = [this](const auto& part, std::uint32_t local,
                                    Uuid key) -> std::shared_ptr<framework::TaskInventoryItem> {
            auto object = world.object(part.id);
            return object && object->local_id == local ? task_inventory(part.id).find(key) : nullptr;
        };
        backend.trash_folder = [this](Uuid agent) -> std::optional<Uuid> {
            auto folder = policies.inventory_service->for_type(agent, 14);
            if (folder)
                return folder->id;
            return {};
        };
        backend.move_to_user = [this](const auto& client, Uuid folder, const auto& part, Uuid key) {
            auto object = world.object(part.id);
            if (!object)
                return TaskInventoryMoveResult{false, "Part not found"};
            auto result = task_inventory_move().move({client.agent, client.alert}, folder,
                                                     {object->id, object->owner, object->local_id}, key);
            return TaskInventoryMoveResult{bool(result.item), result.message};
        };
        backend.remove_script_events = [this](const auto& part, Uuid key) {
            script_events(part.id).remove(key);
        };
        backend.active_script_count = [](const auto&, int) {};
        backend.group_remove = [this](const auto& part, std::uint32_t, Uuid key) {
            task_inventory(part.id).remove(key);
        };
        backend.invalidate_effective = [this](const auto& part) { refresh_script_flags(part.id); };
        backend.properties = [this](const auto& part, const auto& client) {
            send_part_properties(part.id, client.agent);
        };
        backend.warn = [this](Uuid id, const auto& message) {
            warn("Task inventory removal " + id.str() + ": " + message.value_or(""));
        };
        TaskInventoryRemoveController(std::move(backend)).remove({agent, send_alert}, key, local);
    }
    void update_task_inventory(const ViewerAgent& actor, const lludp::Packet& packet) {
        auto request = task_inventory_update_request(packet);
        if (!request)
            return;
        std::lock_guard lock(task_data_mutex);
        TaskInventoryUpdateBackend backend;
        backend.part = [this](std::uint32_t local) -> std::optional<TaskInventoryUpdatePart> {
            auto part = world.object(local);
            if (!part)
                return {};
            return TaskInventoryUpdatePart{part->id, part->owner, local};
        };
        backend.task_item = [this](const auto& part, Uuid key) { return task_inventory(part.id).find(key); };
        backend.inventory_item = [this](Uuid agent, Uuid key) {
            return policies.inventory ? policies.inventory->item(agent, key) : std::optional<InventoryItem>{};
        };
        backend.library_item = policies.library_inventory_item;
        backend.can_drop = [this](const auto& item, const auto& client, const auto& part) {
            auto object = world.object(part.id);
            if (!object)
                return false;
            if (policies.can_drop_inventory)
                return policies.can_drop_inventory(client.agent, *object, item);
            InventoryDropPermissionBackend permission;
            permission.bypass = [this] { return policies.bypass_inventory_permissions; };
            permission.root_flags = [this, object](const auto&) {
                return inventory_drop_root(*object).flags;
            };
            permission.object_permissions = [object](Uuid agent, const auto&, bool) {
                return agent == object->owner ? object->owner_mask : 0u;
            };
            return can_drop_object_inventory(
                &item,
                InventoryDropPresence{client.agent, false,
                                      policies.inventory_is_god && policies.inventory_is_god(client.agent)},
                InventoryDropPart{part.id, part.owner, true, false, object->owner_mask}, permission);
        };
        backend.can_edit_object = [this](const auto& part, const auto& client) {
            auto object = world.object(part.id);
            return object && policies.can_edit && policies.can_edit(client.agent, *object);
        };
        backend.add_group = [this](Uuid agent, const auto& part, std::uint32_t local, const auto& source,
                                   Uuid copy, bool modify) {
            auto item = source;
            item.creator_id = item.creator_id.substr(0, item.creator_id.find(';'));
            InventoryScriptRezController(inventory_script_backend())
                .add_to_group(agent, {part.id, part.owner}, local, item, copy, modify);
        };
        backend.fetch_remote_assets = policies.fetch_remote_inventory_assets;
        backend.properties = [this](const auto& part, const auto& client) {
            send_part_properties(part.id, client.agent);
        };
        backend.bypass_permissions = [this] { return policies.bypass_inventory_permissions; };
        backend.remove_user_items = [this](const auto& client, const auto& keys) {
            if (!policies.inventory_service)
                throw std::runtime_error("Inventory drop requires a user inventory service");
            policies.inventory_service->delete_items(client.agent, keys);
        };
        backend.can_edit_inventory = [this](Uuid object, Uuid agent) {
            return can_edit_inventory(object, agent);
        };
        std::optional<Uuid> ready_upload;
        if (policies.task_inventory_transaction)
            backend.transaction_update = [this](const auto& client, const auto& part, Uuid transaction,
                                                auto current) {
                policies.task_inventory_transaction(client.agent, part.id, transaction, std::move(current));
            };
        else if (policies.store_inventory_asset && policies.inventory_service)
            backend.transaction_update = [this, &actor, &ready_upload](const auto&, const auto& part,
                                                                       Uuid transaction, const auto& current) {
                auto& upload = asset_uploads[actor.circuit->session_id][transaction];
                if (upload.inventory_attempted || upload.creation || upload.update)
                    return;
                upload.task_update = AssetUpload::TaskUpdate{part.id, current->item_id, current->asset_id,
                                                              current->type};
                if (upload.complete && upload.success)
                    ready_upload = transaction;
            };
        backend.is_god = [this](Uuid agent) {
            return policies.inventory_is_god && policies.inventory_is_god(agent);
        };
        backend.update_item = [this, &actor, &ready_upload](const auto& part, auto item) {
            if (ready_upload) {
                auto& upload = asset_uploads.at(actor.circuit->session_id).at(*ready_upload);
                apply_task_asset_upload(actor, upload, *item);
            }
            return task_inventory(part.id).update(std::move(item));
        };
        backend.error = backend.warn = backend.info = [this](auto text) { debug(text); };
        auto agent = actor.circuit->agent_id;
        TaskInventoryUpdateClient client{
            agent,
            [this, agent](Uuid group) {
                return policies.inventory_group_member && policies.inventory_group_member(agent, group);
            },
            [this, agent](Uuid group) -> std::uint64_t {
                return policies.inventory_group_powers ? policies.inventory_group_powers(agent, group) : 0;
            }};
        TaskInventoryUpdateController(std::move(backend))
            .update(client, request->transaction, std::move(request->item), request->local);
    }
    static InventoryCopyItem copy_item(std::optional<InventoryItem> value) {
        if (!value)
            return {};
        auto result = std::make_shared<TaskInventoryAgentItem>();
        result->item = std::move(*value);
        auto at = result->item.creator_id.find(';');
        result->creator_data =
            at == std::string::npos ? std::string{} : result->item.creator_id.substr(at + 1);
        if (at != std::string::npos)
            result->item.creator_id.resize(at);
        return result;
    }
    InventoryCopyBackend inventory_copy_backend(const ViewerAgent& actor) {
        InventoryCopyBackend backend;
        backend.can_create_user = [this](auto type, auto agent) {
            return !policies.can_create_user_inventory || policies.can_create_user_inventory(type, agent);
        };
        backend.has_presence = [this](auto agent) { return bool(viewers->agent(agent)); };
        backend.library = [this]() -> std::optional<InventoryCopyLibrary> {
            if (!policies.library_inventory_owner || !policies.library_inventory_item)
                return {};
            auto owner = policies.library_inventory_owner();
            if (!owner)
                return {};
            return InventoryCopyLibrary{
                *owner, [this](Uuid key) { return copy_item(policies.library_inventory_item(key)); }};
        };
        backend.item = [this](Uuid agent, Uuid key) {
            return copy_item(policies.inventory_service->item(agent, key));
        };
        backend.bypass_permissions = [this] { return policies.bypass_inventory_permissions; };
        backend.can_copy_user = [this](Uuid agent, Uuid key) {
            return !policies.can_copy_user_inventory || policies.can_copy_user_inventory(agent, key);
        };
        backend.unix_time = user_inventory_unix_time;
        backend.add = [this](auto item) { return agent_inventory_add().add(std::move(item)); };
        backend.create_update = [this, actor](Uuid agent, auto item, std::uint32_t callback) {
            if (inventory_active(actor))
                viewers->transport().enqueue_if_session(
                    agent, actor.circuit->session_id, inventory_item_create(agent, item->item, {}, callback),
                    ThrottleOutPacketType::Asset);
        };
        backend.alert = [this, actor](Uuid agent, const std::string& text) {
            if (inventory_active(actor))
                viewers->transport().enqueue_if_session(agent, actor.circuit->session_id,
                                                        agent_alert(agent, text, false),
                                                        ThrottleOutPacketType::Task);
        };
        backend.debug = backend.error = backend.warn = [this](auto text) { debug(text); };
        return backend;
    }
    void copy_inventory_items(const ViewerAgent& actor, const lludp::Packet& packet) {
        InventoryCopyController controller(inventory_copy_backend(actor));
        for (const auto& fields : packet.blocks.at("InventoryData"))
            controller.copy(
                actor.circuit->agent_id,
                static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("CallbackID"))),
                std::get<Uuid>(fields.at("OldAgentID")), std::get<Uuid>(fields.at("OldItemID")),
                std::get<Uuid>(fields.at("NewFolderID")),
                framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("NewName"))));
    }
    void create_inventory_item(const ViewerAgent& actor, const lludp::Packet& packet, std::int32_t date) {
        if (!policies.inventory_service)
            throw std::runtime_error("Inventory creation requires an InventoryService");
        InventoryNewBackend backend;
        backend.can_create = [this](auto type, auto agent) {
            return !policies.can_create_user_inventory || policies.can_create_user_inventory(type, agent);
        };
        backend.folder = [this](auto agent, auto id) {
            return policies.inventory_service->folder(agent, id);
        };
        backend.folder_type = [this](auto agent, auto type) {
            return policies.inventory_service->for_type(agent, type);
        };
        backend.transactions = [this, actor]() -> std::optional<std::function<bool(const InventoryNewRequest&)>> {
            if (policies.inventory_creation_transaction)
                return policies.inventory_creation_transaction;
            if (policies.store_inventory_asset)
                return [this, actor](const InventoryNewRequest& request) {
                    auto& upload = asset_uploads[actor.circuit->session_id][request.transaction];
                    if (upload.inventory_attempted || upload.update || upload.task_update)
                        return true;
                    upload.creation = request;
                    if (upload.complete && upload.success)
                        create_from_asset_upload(actor, upload);
                    return true;
                };
            return {};
        };
        backend.presence = [this](auto agent) {
            auto presence = viewers->agent(agent);
            return InventoryNewPresence{bool(presence), bool(presence)};
        };
        backend.landmark = [this](auto agent) {
            if (policies.inventory_landmark)
                return policies.inventory_landmark(agent);
            auto presence = viewers->agent(agent);
            if (!presence)
                throw std::runtime_error("Landmark presence disappeared");
            return generate_inventory_landmark(descriptor.id, presence->position, descriptor.handle());
        };
        backend.create_asset = [](const auto& request, auto data) {
            auto asset = std::make_shared<framework::AssetBase>(Uuid::random(), request.name,
                                                                request.asset_type, request.agent.str());
            asset->description(request.description);
            asset->data = std::make_shared<std::vector<std::uint8_t>>(std::move(data));
            return asset;
        };
        backend.store_asset = [this](auto asset) {
            if (!policies.store_asset)
                throw std::runtime_error("Landmark creation requires an asset service");
            policies.store_asset(std::move(asset));
        };
        backend.environment = [this]() -> std::optional<std::function<Uuid(std::uint8_t)>> {
            if (policies.environment_default_asset)
                return policies.environment_default_asset;
            return {};
        };
        auto controller = std::make_shared<InventoryCopyController>(inventory_copy_backend(actor));
        backend.create = [controller](const auto& request, auto asset, auto flags, auto everyone,
                                      auto group) {
            TaskInventoryAgentItem parameters;
            auto& item = parameters.item;
            item.creator_id = request.agent.str();
            parameters.creator_data = std::string{};
            item.parent = request.folder;
            item.name = request.name.value_or("");
            item.description = request.description.value_or("");
            item.flags = static_cast<std::int32_t>(flags);
            item.asset_id = asset;
            item.asset_type = request.asset_type;
            item.inventory_type = request.inventory_type;
            item.base_permissions = item.current_permissions = 0x9e000;
            item.everyone_permissions = static_cast<std::int32_t>(everyone);
            item.next_permissions = std::bit_cast<std::int32_t>(request.next_permissions);
            item.group_permissions = static_cast<std::int32_t>(group);
            item.creation_date = request.creation_date;
            controller->create(request.agent, request.callback, parameters);
        };
        backend.debug = backend.error = [this](auto message) { debug("Inventory creation: " + message); };
        const auto& fields = packet.block("InventoryBlock");
        InventoryNewRequest request;
        request.agent = actor.circuit->agent_id;
        request.transaction = std::get<Uuid>(fields.at("TransactionID"));
        request.folder = std::get<Uuid>(fields.at("FolderID"));
        request.callback = static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("CallbackID")));
        request.name = framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("Name")));
        request.description =
            framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("Description")));
        request.inventory_type = static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("InvType")));
        request.asset_type = static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("Type")));
        request.subtype = static_cast<std::uint8_t>(std::get<std::uint64_t>(fields.at("WearableType")));
        request.next_permissions =
            static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("NextOwnerMask")));
        request.creation_date = date;
        InventoryNewController(std::move(backend)).create(std::move(request));
    }
    void link_inventory_item(const ViewerAgent& actor, const lludp::Packet& packet) {
        const auto& fields = packet.block("InventoryBlock");
        InventoryCopyController(inventory_copy_backend(actor))
            .link(actor.circuit->agent_id,
                  static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("CallbackID"))),
                  std::get<Uuid>(fields.at("FolderID")), std::get<Uuid>(fields.at("OldItemID")),
                  framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("Name"))),
                  framework::field_to_string(std::get<std::vector<std::uint8_t>>(fields.at("Description"))),
                  static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("InvType"))),
                  static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("Type"))));
    }
    void user_inventory_operation(const ViewerAgent& actor, const lludp::Packet& packet) {
        if (!policies.inventory_service)
            throw std::runtime_error("User inventory operation requires an InventoryService");
        if (packet.spec->name == "LinkInventoryItem") {
            link_inventory_item(actor, packet);
            return;
        }
        if (packet.spec->name == "CopyInventoryItem") {
            copy_inventory_items(actor, packet);
            return;
        }
        UserInventoryBackend backend;
        backend.item = [this](Uuid agent, Uuid key) -> UserInventoryItem {
            auto item = policies.inventory_service->item(agent, key);
            return item ? std::make_shared<InventoryItem>(std::move(*item)) : nullptr;
        };
        backend.update = [this](auto item) { return policies.inventory_service->update_item(*item); };
        backend.move = [this](Uuid agent, UserInventoryMoveList& list) {
            std::vector<InventoryItem> items;
            for (std::size_t n = 0; n < list.size(); ++n) {
                auto item = list.at(n);
                if (!item)
                    throw std::invalid_argument("Null inventory move item");
                items.push_back(*item);
            }
            return policies.inventory_service->move_items(agent, items);
        };
        backend.delete_items = [this](Uuid agent, const auto& keys) {
            policies.inventory_service->delete_items(agent, keys);
        };
        backend.delete_folders = [this](Uuid agent, const auto& keys) {
            policies.inventory_service->delete_folders(agent, keys);
        };
        backend.bulk = [this, actor](Uuid agent, auto item) {
            if (!inventory_active(actor) || !capability_runtime || !capability_runtime->event_queue())
                return;
            auto projected = *item;
            projected.creator_id = projected.creator_id.substr(0, projected.creator_id.find(';'));
            capability_runtime->event_queue()->bulk_inventory(&projected, agent);
        };
        backend.transaction = policies.user_inventory_transaction;
        if (!backend.transaction && policies.store_inventory_asset)
            backend.transaction = [this, actor](Uuid, Uuid transaction, const auto& item) {
                auto& upload = asset_uploads[actor.circuit->session_id][transaction];
                if (upload.inventory_attempted || upload.creation || upload.task_update)
                    return;
                upload.update = *item;
                if (upload.complete && upload.success)
                    update_from_asset_upload(actor, transaction, upload);
            };
        backend.unix_time = user_inventory_unix_time;
        backend.creator = [](const auto& item) {
            Uuid creator;
            framework::try_parse_sdk_uuid(item.creator_id.substr(0, item.creator_id.find(';')), creator);
            return creator;
        };
        backend.debug = backend.warn = backend.error = [this](auto text) { debug(text); };
        UserInventoryOperations operations(std::move(backend));
        auto agent = actor.circuit->agent_id;
        auto ids = [&](const char* block, const char* key) {
            std::vector<Uuid> values;
            for (const auto& fields : packet.blocks.at(block))
                values.push_back(std::get<Uuid>(fields.at(key)));
            return values;
        };
        if (packet.spec->name == "UpdateInventoryItem") {
            for (auto& request : user_inventory_update_requests(packet))
                operations.update(agent, request.transaction, request.key, std::move(request.item));
        } else if (packet.spec->name == "MoveInventoryItem") {
            auto list = user_inventory_move_request(agent, packet);
            operations.move(agent, &list);
        } else if (packet.spec->name == "RemoveInventoryItem")
            operations.remove_items(agent, ids("InventoryData", "ItemID"));
        else if (packet.spec->name == "RemoveInventoryFolder")
            operations.remove_folders(agent, ids("FolderData", "FolderID"));
        else {
            operations.remove_folders(agent, ids("FolderData", "FolderID"));
            operations.remove_items(agent, ids("ItemData", "ItemID"));
        }
    }
    struct ScriptAnimations {
        Uuid session;
        std::vector<AvatarAnimationEntry> entries;
        bool default_suppressed = false;
        std::map<Uuid, Uuid> overrides;
        std::int32_t default_sequence = 1;
    };
    std::map<Uuid, ScriptAnimations> avatar_script_animations;
    std::uint32_t animation_sequence = 0x20000000u;
    Uuid resolve_script_animation(Uuid part, const std::string& name, bool allow_id) {
        Uuid animation;
        if (allow_id)
            framework::try_parse_sdk_uuid(name, animation);
        if (animation.is_zero()) {
            const auto inventory = task_parts.find(part);
            if (inventory != task_parts.end() && inventory->second.inventory)
                for (const auto& item : inventory->second.inventory->items())
                    if (item->type == 20 && item->name == name) {
                        animation = item->asset_id;
                        break;
                    }
        }
        if (animation.is_zero() && policies.animations) {
            auto upper = name;
            for (auto& c : upper)
                if (c >= 'a' && c <= 'z')
                    c -= 'a' - 'A';
            const auto found = policies.animations->by_name.find(upper);
            if (found != policies.animations->by_name.end())
                animation = found->second;
        }
        return animation;
    }
    Uuid movement_animation_id(std::string_view movement) const {
        if (!policies.animations)
            return {};
        constexpr std::pair<std::string_view, std::string_view> names[]{
            {"Crouching", "CROUCH"},      {"CrouchWalking", "CROUCHWALK"},
            {"Falling Down", "FALLDOWN"}, {"Flying", "FLY"},
            {"FlyingSlow", "FLYSLOW"},    {"Hovering", "HOVER"},
            {"Hovering Up", "HOVER_UP"},  {"Hovering Down", "HOVER_DOWN"},
            {"Jumping", "JUMP"},          {"Landing", "LAND"},
            {"PreJumping", "PREJUMP"},    {"Running", "RUN"},
            {"Sitting", "SIT"},           {"Sitting on Ground", "SIT_GROUND"},
            {"Standing", "STAND"},        {"Standing Up", "STANDUP"},
            {"Striding", "STRIDE"},       {"Soft Landing", "SOFT_LAND"},
            {"Turning Left", "TURNLEFT"}, {"Turning Right", "TURNRIGHT"},
            {"Walking", "WALK"}};
        for (const auto& [label, name] : names)
            if (framework::ordinal_equals_ignore_case(label, movement))
                return policies.animations->resolve(std::string(name));
        return {};
    }
    void change_animation_override(Uuid part, Uuid item, std::string_view movement,
                                   const std::optional<std::string>& name) {
        std::lock_guard lock(task_data_mutex);
        const auto found = scripts.find(item);
        if (found == scripts.end() || !found->second.task)
            return;
        const auto& task = *found->second.task;
        if (!(task.permissions_mask & 0x8000))
            return;
        const auto avatar = viewers->agent(task.permissions_granter);
        if (!avatar || !avatar->root)
            return;
        auto& entries = active_animations(*avatar);
        auto& state = avatar_script_animations.at(task.permissions_granter);
        const auto before = default_avatar_animation(task.permissions_granter);
        if (!name && framework::ordinal_equals_ignore_case(movement, "ALL"))
            state.overrides.clear();
        else {
            const auto original = movement_animation_id(movement);
            if (original.is_zero())
                return;
            if (name) {
                const auto replacement = resolve_script_animation(part, *name, false);
                if (replacement.is_zero())
                    return;
                state.overrides[original] = replacement;
            } else
                state.overrides.erase(original);
        }
        state.default_suppressed = false;
        if (const auto physics = viewer_physics.load())
            physics->set_animation_overrides(task.permissions_granter, state.overrides);
        else if (before != default_avatar_animation(task.permissions_granter)) {
            animation_sequence = (animation_sequence + 1) & 0x7fffffffu;
            state.default_sequence = static_cast<std::int32_t>(animation_sequence);
        }
        publish_avatar_animations(*avatar, entries);
    }
    void change_script_animation(Uuid part, Uuid script_id, const std::string& name, bool start) {
        std::lock_guard lock(task_data_mutex);
        const auto script = scripts.find(script_id);
        if (script == scripts.end() || !script->second.task)
            return;
        const auto& task = *script->second.task;
        if (task.permissions_granter.is_zero() || !(task.permissions_mask & 0x10))
            return;
        const auto wearer = viewers->agent(task.permissions_granter);
        if (!wearer || !wearer->root)
            return;
        apply_script_animation(*wearer, part, resolve_script_animation(part, name, !start), start, true);
    }
    void change_ossl_avatar_animation(Uuid part, Uuid agent, const std::string& name, bool start) {
        std::lock_guard lock(task_data_mutex);
        const auto avatar = viewers->agent(agent);
        if (!avatar || !avatar->root)
            return;

        Uuid animation;
        if (start || !framework::try_parse_sdk_uuid(name, animation)) {
            const auto inventory = task_parts.find(part);
            if (inventory != task_parts.end() && inventory->second.inventory) {
                for (const auto& item : inventory->second.inventory->items()) {
                    // Play searches only animation items. Stop checks the type of
                    // the first item of any type with this name.
                    if (item->name != name || (start && item->type != 20))
                        continue;
                    if (item->type == 20)
                        animation = item->asset_id;
                    break;
                }
            }
        }
        if (animation.is_zero() && policies.animations)
            animation = policies.animations->resolve(name);
        apply_script_animation(*avatar, part, animation, start, false);
    }
    void apply_script_animation(const ViewerAgent& avatar, Uuid part, Uuid animation,
                                bool start, bool publish_default_removal) {
        if (animation.is_zero())
            return;
        const auto agent = avatar.circuit->agent_id;
        auto& entries = active_animations(avatar);
        if (animation == default_avatar_animation(agent)) {
            if (!start) {
                if (const auto physics = viewer_physics.load(); physics && !avatar.seat && !avatar.ground_sitting)
                    physics->stop_default_animation(agent, true);
                else
                    avatar_script_animations.at(agent).default_suppressed = true;
                if (publish_default_removal)
                    publish_avatar_animations(avatar, entries);
            }
            return;
        }
        if (change_avatar_animation(entries, animation, part, start))
            publish_avatar_animations(avatar, entries);
    }
    std::vector<AvatarAnimationEntry>& active_animations(const ViewerAgent& avatar) {
        auto& state = avatar_script_animations[avatar.circuit->agent_id];
        if (state.session != avatar.circuit->session_id)
            state = {avatar.circuit->session_id, {}};
        return state.entries;
    }
    bool change_avatar_animation(std::vector<AvatarAnimationEntry>& entries, Uuid animation, Uuid source,
                                 bool start) {
        if (animation.is_zero())
            return false;
        const auto entry = std::find_if(entries.begin(), entries.end(),
                                        [&](const auto& value) { return value.animation == animation; });
        if (start) {
            if (entry != entries.end() || entries.size() >= 110)
                return false;
            animation_sequence = (animation_sequence + 1) & 0x7fffffffu;
            entries.push_back({animation, source, static_cast<std::int32_t>(animation_sequence)});
        } else {
            if (entry == entries.end())
                return false;
            entries.erase(entry);
        }
        return true;
    }
    Uuid default_avatar_animation(Uuid agent) const {
        const auto viewer = viewers->agent(agent);
        const bool ground = viewer && viewer->ground_sitting;
        const bool seated = viewer && viewer->seat.has_value();
        if (!seated && !ground)
            if (const auto physics = viewer_physics.load())
                return physics->default_animation(agent);
        const auto found = avatar_script_animations.find(agent);
        const bool current = viewer && found != avatar_script_animations.end() &&
                             found->second.session == viewer->circuit->session_id;
        if (current && found->second.default_suppressed)
            return {};
        const auto movement = policies.animations
            ? policies.animations->resolve(ground ? "SIT_GROUND_CONSTRAINED" : seated ? "SIT" : "STAND")
            : Uuid{};
        if (current && !ground)
            if (const auto override = found->second.overrides.find(movement);
                override != found->second.overrides.end())
                return override->second;
        return movement;
    }
    std::vector<AvatarAnimationEntry>
    animation_snapshot(Uuid agent, const std::vector<AvatarAnimationEntry>& entries) const {
        std::vector<AvatarAnimationEntry> result;
        const auto movement = default_avatar_animation(agent);
        const auto state = avatar_script_animations.find(agent);
        const auto viewer = viewers->agent(agent);
        const bool current = viewer && state != avatar_script_animations.end() &&
                             state->second.session == viewer->circuit->session_id;
        if (!movement.is_zero())
            result.push_back({movement, {}, current ? state->second.default_sequence : 1});
        if (current && viewer->ground_sitting && policies.animations) {
            const auto ground = policies.animations->resolve("SIT_GROUND");
            if (const auto override = state->second.overrides.find(ground);
                override != state->second.overrides.end() && override->second != movement)
                result.push_back({override->second, {}, state->second.default_sequence});
        }
        for (const auto& entry : entries)
            if (entry.animation != movement)
                result.push_back(entry);
        return result;
    }
    void publish_avatar_animations(const ViewerAgent& avatar,
                                   const std::vector<AvatarAnimationEntry>& entries) {
        const auto agent = avatar.circuit->agent_id;
        if (const auto physics = viewer_physics.load();
            !avatar.seat && !avatar.ground_sitting && physics && physics->set_script_animations(agent, entries))
            return;
        const auto snapshot = animation_snapshot(agent, entries);
        for (const auto& viewer : viewers->agents())
            if (viewer.root && can_see_avatar(avatar, viewer))
                viewers->transport().enqueue_if_session(
                    viewer.circuit->agent_id, viewer.circuit->session_id,
                    avatar_animations(agent, viewer.circuit->agent_id, snapshot),
                    ThrottleOutPacketType::Task);
    }
    void apply_viewer_animations(const ViewerAgent& avatar, const lludp::Packet& packet) {
        if (!avatar.root)
            return;
        std::lock_guard lock(task_data_mutex);
        auto& entries = active_animations(avatar);
        if (const auto blocks = packet.blocks.find("AnimationList"); blocks != packet.blocks.end())
            for (const auto& block : blocks->second) {
                const auto agent = avatar.circuit->agent_id;
                const auto animation = std::get<Uuid>(block.at("AnimID"));
                const auto start = std::get<bool>(block.at("StartAnim"));
                if (!animation.is_zero() && animation == default_avatar_animation(agent)) {
                    if (const auto physics = viewer_physics.load(); !start && physics)
                        physics->stop_default_animation(agent, false);
                    continue;
                }
                change_avatar_animation(entries, animation, agent, start);
            }
        publish_avatar_animations(avatar, entries);
    }
    void replay_avatar_animations(const ViewerAgent& recipient) {
        std::lock_guard lock(task_data_mutex);
        for (const auto& source : viewers->agents()) {
            if (!source.root)
                continue;
            const auto agent = source.circuit->agent_id;
            const auto state = avatar_script_animations.find(agent);
            const auto snapshot =
                animation_snapshot(agent, state != avatar_script_animations.end() &&
                                                  state->second.session == source.circuit->session_id
                                              ? state->second.entries
                                              : std::vector<AvatarAnimationEntry>{});
            if (snapshot.empty())
                continue;
            viewers->transport().enqueue_if_session(
                recipient.circuit->agent_id, recipient.circuit->session_id,
                avatar_animations(agent, recipient.circuit->agent_id, snapshot), ThrottleOutPacketType::Task);
        }
    }
    void request_script_permissions(Uuid part_id, Uuid script_id, std::string_view target,
                                    std::int32_t mask) {
        Uuid agent;
        if (!framework::try_parse_sdk_uuid(target, agent) || agent.is_zero())
            return;
        std::lock_guard lock(task_data_mutex);
        const auto found = scripts.find(script_id);
        const auto part = world.object(part_id);
        if (found == scripts.end() || !part || !found->second.instance || !found->second.task)
            return;
        auto& script = found->second;
        script.question.reset();
        if (!mask) {
            script.task->permissions_granter = {};
            script.task->permissions_mask = 0;
            script.instance->post_event({"run_time_permissions", {std::int32_t{0}}, {}});
            return;
        }
        const auto recipient = viewers->agent(agent);
        const auto root = !part->is_attachment && part->parent_id ? world.object(part->parent_id) : part;
        if (!root)
            return;
        constexpr std::int32_t attachment_permissions = 0x04 | 0x10 | 0x20 | 0x400 | 0x800 | 0x8000;
        if (root->is_attachment && recipient && recipient->local_id == root->parent_id &&
            (mask & ~attachment_permissions) == 0) {
            script.task->permissions_granter = agent;
            script.task->permissions_mask = mask;
            script.instance->post_event({"run_time_permissions", {mask}, {}});
            return;
        }
        if (!recipient || !inventory_active(*recipient)) {
            script.instance->post_event({"run_time_permissions", {std::int32_t{0}}, {}});
            return;
        }
        std::u16string owner_name;
        const auto owner_account = policies.basic_profile_accounts
                                       ? policies.basic_profile_accounts->get(descriptor.scope, root->owner)
                                       : nullptr;
        if (owner_account) {
            owner_name = owner_account->first.value_or(u"") + u" " + owner_account->last.value_or(u"");
        } else if (const auto owner_object = world.object(root->owner)) {
            owner_name = framework::utf16(owner_object->name);
        } else if (const auto owner = viewers->agent(root->owner)) {
            owner_name = framework::utf16(owner->circuit->first_name.value_or("") + " " +
                                          owner->circuit->last_name.value_or(""));
        }
        if (owner_name.empty() || owner_name == u" ")
            owner_name = u"(hippos)";
        auto packet = lludp::make_packet("ScriptQuestion");
        auto& data = packet.block("Data");
        data["TaskID"] = part_id;
        data["ItemID"] = script_id;
        data["ObjectName"] = profile_string_bytes(framework::utf16(root->name), 255);
        data["ObjectOwner"] = profile_string_bytes(owner_name, 255);
        data["Questions"] = std::int64_t(mask);
        const auto session = recipient->circuit->session_id;
        script.task->permissions_granter = agent;
        script.task->permissions_mask = 0;
        script.question = PermissionQuestion{agent, session, mask};
        viewers->transport().enqueue_if_session(agent, session, std::move(packet),
                                                ThrottleOutPacketType::Task);
    }
    void answer_script_permissions(const ViewerAgent& actor, const lludp::Packet& packet) {
        std::lock_guard lock(task_data_mutex);
        const auto& data = packet.block("Data");
        const auto found = scripts.find(std::get<Uuid>(data.at("ItemID")));
        if (found == scripts.end())
            return;
        auto& script = found->second;
        if (!script.question || !script.instance || !script.instance->running() || !script.task ||
            script.item.parent != std::get<Uuid>(data.at("TaskID")) ||
            script.question->agent != actor.circuit->agent_id ||
            script.question->session != actor.circuit->session_id)
            return;
        const auto answer =
            static_cast<std::int32_t>(std::get<std::int64_t>(data.at("Questions"))) & script.question->mask;
        script.question.reset();
        script.task->permissions_mask = answer;
        script.instance->post_event({"run_time_permissions", {answer}, {}});
    }
    void set_viewer_appearance(const ViewerAgent& actor, const lludp::Packet& packet) {
        const auto agent = actor.circuit->agent_id;
        const auto session = actor.circuit->session_id;
        std::unique_lock lock(appearance_mutex);
        const auto current = viewers->agent(agent);
        if (!current || !current->root || current->circuit->session_id != session)
            return;
        auto appearance = std::make_shared<AvatarAppearance>(current->appearance().get());
        const auto& texture =
            std::get<std::vector<std::uint8_t>>(packet.block("ObjectData").at("TextureEntry"));
        const bool texture_changed =
            texture.size() > 1 && appearance->set_texture_entries(std::make_shared<TextureEntry>(texture));
        std::set<Uuid> missing_bakes;
        bool upload_bakes = false;
        WearableCacheItems upload_assets;
        const auto submitted_bakes = packet.blocks.find("WearableData");
        if (texture.size() > 1 && submitted_bakes != packet.blocks.end() &&
            !submitted_bakes->second.empty()) {
            const auto asset_cache = policies.baked_texture_cache ? policies.baked_texture_cache() : nullptr;
            auto cache = std::make_shared<WearableCacheItems>(WearableCacheItem::defaults());
            if (policies.store_baked_appearance)
                upload_assets.resize(cache->size());
            if (appearance->cache_items) {
                const auto count = std::min(cache->size(), appearance->cache_items->size());
                std::copy_n(appearance->cache_items->begin(), count, cache->begin());
            }
            std::bitset<TextureEntry::max_faces> submitted_faces;
            std::size_t hits = 0;
            for (const auto& row : submitted_bakes->second) {
                const auto index = std::get<std::uint64_t>(row.at("TextureIndex"));
                if (index >= cache->size()) {
                    ++hits;
                    continue;
                }
                submitted_faces.set(index);
                auto item = std::make_shared<WearableCacheItem>();
                item->texture_index = static_cast<std::uint32_t>(index);
                const auto& face = appearance->texture->faces[index];
                if (face && !face->texture_id().is_zero() &&
                    face->texture_id() != AvatarAppearance::default_texture_id) {
                    if (asset_cache)
                        asset_cache->get(face->texture_id().str(), item->texture_asset);
                    if (item->texture_asset) {
                        item->cache_id = std::get<Uuid>(row.at("CacheID"));
                        item->texture_id = face->texture_id();
                        ++hits;
                        const auto& previous = (*cache)[index];
                        upload_bakes |= !previous || previous->cache_id != item->cache_id ||
                                        previous->texture_id != item->texture_id;
                    } else
                        missing_bakes.insert(face->texture_id());
                } else if (index == 19) {
                    ++hits;
                    const auto& previous = (*cache)[index];
                    upload_bakes |= previous && !previous->cache_id.is_zero();
                }
                if (!upload_assets.empty()) {
                    if (item->texture_asset)
                        upload_assets[index] = std::make_shared<WearableCacheItem>(*item);
                    else
                        upload_assets[index].reset();
                }
                item->texture_asset.reset();
                (*cache)[index] = std::move(item);
            }
            for (std::size_t i = avatar_appearance::bakes_count_pv7;
                 i < avatar_appearance::bake_indices.size(); ++i) {
                const auto index = avatar_appearance::bake_indices[i];
                if (submitted_faces.test(index))
                    continue;
                appearance->texture->faces[index].reset();
                auto item = std::make_shared<WearableCacheItem>();
                item->texture_index = index;
                (*cache)[index] = std::move(item);
            }
            appearance->cache_items = std::move(cache);
            upload_bakes = upload_bakes && hits == submitted_bakes->second.size();
        }
        auto visual = std::make_shared<std::vector<std::uint8_t>>();
        if (const auto params = packet.blocks.find("VisualParam"); params != packet.blocks.end())
            for (const auto& param : params->second)
                visual->push_back(static_cast<std::uint8_t>(std::get<std::uint64_t>(param.at("ParamValue"))));
        const bool visual_changed = appearance->set_visual_params(visual);
        const auto& size = std::get<std::vector<double>>(packet.block("AgentData").at("Size"));
        const auto old_size = appearance->avatar_size();
        appearance->set_size({float(size.at(0)), float(size.at(1)), float(size.at(2))});
        const bool changed = texture_changed || visual_changed || old_size != appearance->avatar_size();
        const auto updated = viewers->update_appearance(agent, session, appearance);
        if (!updated)
            return;
        if (changed)
            queue_appearance_save(*updated);
        const auto now = std::chrono::steady_clock::now();
        pending_appearance_sends.insert_or_assign(
            agent, WearablesReply{now + std::chrono::seconds(policies.appearance_send_delay_seconds), agent,
                                  session});
        std::erase_if(rebake_deadlines, [now](const auto& entry) { return entry.second <= now; });
        std::erase_if(missing_bakes, [&](const auto& id) {
            return !rebake_deadlines.insert_or_assign({agent, id}, now + std::chrono::seconds(30)).second;
        });
        lock.unlock();
        if (upload_bakes && policies.store_baked_appearance) {
            try {
                policies.store_baked_appearance(agent, upload_assets);
            } catch (const std::exception& e) {
                error(std::string("XBakes upload failed: ") + e.what());
            }
        }
        for (const auto& id : missing_bakes) {
            auto rebake = lludp::make_packet("RebakeAvatarTextures");
            rebake.block("TextureData")["TextureID"] = id;
            viewers->transport().enqueue_if_session(agent, session, std::move(rebake),
                                                    ThrottleOutPacketType::Task);
        }
        if (const auto physics = viewer_physics.load())
            physics->appearance_changed(*updated);
    }
    void restore_baked_appearance(const ViewerAgent& actor, WearableCacheItems restored) {
        const auto agent = actor.circuit->agent_id, session = actor.circuit->session_id;
        const auto asset_cache = policies.baked_texture_cache ? policies.baked_texture_cache() : nullptr;
        if (!asset_cache || !inventory_active(actor))
            return;
        std::unique_lock lock(appearance_mutex);
        const auto current = viewers->agent(agent);
        if (!current || !current->root || current->circuit->session_id != session)
            return;
        if (current->appearance_override != actor.appearance_override)
            return;
        const auto previous = current->appearance();
        if (previous && previous->cache_items)
            return;
        auto appearance = std::make_shared<AvatarAppearance>(previous.get());
        appearance->cache_items = std::make_shared<WearableCacheItems>(WearableCacheItem::defaults());
        for (auto& item : restored) {
            item->texture_asset->temporary(true);
            item->texture_asset->local(true);
            asset_cache->cache(item->texture_asset);
            appearance->texture->get_face(item->texture_index)->texture_id(item->texture_id);
            item->texture_asset.reset();
            (*appearance->cache_items)[item->texture_index] = std::move(item);
        }
        const auto updated = viewers->update_appearance(agent, session, std::move(appearance));
        lock.unlock();
        if (updated)
            for (const auto& other : viewers->agents())
                if (other.circuit->agent_id != agent)
                    send_appearance(*updated, other);
    }
    void answer_cached_texture(const ViewerAgent& actor, const lludp::Packet& p) {
        const auto current = viewers->agent(actor.circuit->agent_id);
        if (!current || current->circuit->session_id != actor.circuit->session_id)
            return;
        const auto& a = *current;
        auto response = lludp::make_packet("AgentCachedTextureResponse");
        response.zerocoded = true;
        auto& identity = response.block("AgentData");
        identity["AgentID"] = a.circuit->agent_id;
        identity["SessionID"] = a.circuit->session_id;
        identity["SerialNum"] = p.block("AgentData").at("SerialNum");
        const auto appearance = a.appearance();
        const auto cache = appearance ? appearance->cache_items : nullptr;
        if (const auto requested = p.blocks.find("WearableData"); requested != p.blocks.end()) {
            auto& rows = response.blocks["WearableData"];
            for (const auto& request : requested->second) {
                const auto index = std::get<std::uint64_t>(request.at("TextureIndex"));
                const auto cached = cache && index < cache->size() ? (*cache)[index] : nullptr;
                const auto texture = cached && cached->cache_id == std::get<Uuid>(request.at("ID"))
                                         ? cached->texture_id
                                         : Uuid{};
                rows.push_back({{"TextureIndex", index},
                                {"TextureID", texture},
                                {"HostName", std::vector<std::uint8_t>{}}});
            }
        }
        viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id,
                                                std::move(response), ThrottleOutPacketType::Task);
    }
    void set_viewer_wearables(const ViewerAgent& actor, const lludp::Packet& packet) {
        const auto agent = actor.circuit->agent_id, session = actor.circuit->session_id;
        std::lock_guard lock(appearance_mutex);
        const auto current = viewers->agent(agent);
        if (!current || !current->root || current->circuit->session_id != session)
            return;
        const auto previous = current->appearance();
        auto appearance = std::make_shared<AvatarAppearance>(previous.get(), false);
        // The viewer changes only wearable slots in this packet.
        if (previous) {
            appearance->texture = previous->texture;
            appearance->cache_items = previous->cache_items;
        }
        if (const auto rows = packet.blocks.find("WearableData"); rows != packet.blocks.end()) {
            for (const auto& row : rows->second) {
                const auto type = std::get<std::uint64_t>(row.at("WearableType"));
                const auto id = std::get<Uuid>(row.at("ItemID"));
                while (appearance->wearables->size() <= type)
                    appearance->wearables->push_back(std::make_shared<AvatarWearable>());
                (*appearance->wearables)[type]->add(id, Uuid{});
            }
        }
        if (previous)
            appearance->get_assets_from(*previous);
        if (const auto updated = viewers->update_appearance(agent, session, appearance))
            queue_appearance_save(*updated);
    }
    // Appearance changes and saves run on the region tick.
    void queue_appearance_save(const ViewerAgent& viewer) {
        if (viewer.circuit->verified_foreign_login || !policies.save_appearance)
            return;
        const auto agent = viewer.circuit->agent_id;
        pending_appearance_saves.insert_or_assign(
            agent, WearablesReply{std::chrono::steady_clock::now() +
                                      std::chrono::seconds(policies.appearance_save_delay_seconds),
                                  agent, viewer.circuit->session_id});
    }
    void resolve_wearable_assets(Uuid agent, AvatarAppearance& appearance) {
        if (policies.inventory_service && policies.inventory_service->root(agent)) {
            const auto defaults = AvatarWearable::defaults();
            for (std::size_t type = 0; type < appearance.wearables->size(); ++type) {
                auto& wearable = *(*appearance.wearables)[type];
                for (std::size_t index = 0; index < wearable.count();) {
                    const auto entry = wearable.at(static_cast<int>(index));
                    if (entry.item.is_zero() || (type < defaults.size() && defaults[type]->count() &&
                                                 defaults[type]->at(0).item == entry.item)) {
                        ++index;
                        continue;
                    }
                    const auto item = policies.inventory_service->item(agent, entry.item);
                    if (!item || item->owner != agent) {
                        wearable.remove_item(entry.item);
                        continue;
                    }
                    wearable.add(entry.item, item->asset_id);
                    ++index;
                }
            }
        }
    }
    void send_due_appearances() {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = pending_appearance_sends.begin(); it != pending_appearance_sends.end();) {
            if (it->second.due > now) {
                ++it;
                continue;
            }
            const auto [due, agent, session] = it->second;
            it = pending_appearance_sends.erase(it);
            const auto current = viewers->agent(agent);
            if (!current || !current->root || current->circuit->session_id != session)
                continue;
            auto recipients = viewers->agents();
            std::erase_if(recipients, [&](const auto& other) { return !can_see_avatar(*current, other); });
            for (const auto& other : recipients)
                if (other.circuit->agent_id != agent)
                    send_appearance(*current, other);
            if (const auto physics = viewer_physics.load(); physics && physics->broadcast_animations(agent, recipients))
                continue;
            std::lock_guard lock(task_data_mutex);
            const auto snapshot = animation_snapshot(agent, active_animations(*current));
            for (const auto& other : recipients)
                viewers->transport().enqueue_if_session(
                    other.circuit->agent_id, other.circuit->session_id,
                    avatar_animations(agent, other.circuit->agent_id, snapshot), ThrottleOutPacketType::Task);
        }
    }
    void save_due_appearances() {
        const auto now = std::chrono::steady_clock::now();
        for (auto it = pending_appearance_saves.begin(); it != pending_appearance_saves.end();) {
            if (it->second.due > now) {
                ++it;
                continue;
            }
            const auto [due, agent, session] = it->second;
            it = pending_appearance_saves.erase(it);
            try {
                std::lock_guard lock(appearance_mutex);
                const auto current = viewers->agent(agent);
                if (!current || current->circuit->session_id != session)
                    continue;
                const auto previous = current->appearance();
                if (!previous)
                    continue;
                auto appearance = std::make_shared<AvatarAppearance>(previous.get());
                appearance->texture = previous->texture;
                appearance->cache_items = previous->cache_items;
                resolve_wearable_assets(agent, *appearance);
                if (viewers->update_appearance(agent, session, appearance) &&
                    !policies.save_appearance(agent, *appearance))
                    error("Could not save viewer appearance for " + agent.str());
            } catch (const std::exception& e) {
                error(std::string("Viewer appearance save failed: ") + e.what());
            }
        }
    }
    void send_due_wearables() {
        std::vector<WearablesReply> ready;
        {
            std::lock_guard lock(commands_mutex);
            const auto now = std::chrono::steady_clock::now();
            while (!pending_wearables.empty() && pending_wearables.front().due <= now) {
                ready.push_back(pending_wearables.front());
                pending_wearables.pop_front();
            }
        }
        for (const auto& reply : ready) {
            const auto agent = viewers->agent(reply.agent);
            if (!agent || agent->circuit->session_id != reply.session)
                continue;
            const auto appearance = agent->appearance();
            if (appearance && appearance->wearables)
                viewers->transport().enqueue_if_session(
                    reply.agent, reply.session,
                    avatar_wearables(reply.agent, reply.session, *appearance->wearables, appearance->serial),
                    ThrottleOutPacketType::Task);
        }
    }
    void run_task_inventory() {
        std::vector<TaskCommand> commands;
        std::vector<std::pair<ViewerAgent, WearableCacheItems>> restores;
        std::vector<Uuid> closed_uploads;
        {
            std::lock_guard lock(commands_mutex);
            commands.swap(task_commands);
            restores.swap(pending_bake_restores);
            closed_uploads.swap(closed_asset_upload_sessions);
        }
        for (const auto& session : closed_uploads)
            asset_uploads.erase(session);
        for (auto& [actor, bakes] : restores)
            try {
                restore_baked_appearance(actor, std::move(bakes));
            } catch (const std::exception& e) {
                error(std::string("XBakes restore failed: ") + e.what());
            }
        for (auto& command : commands) {
            if (!inventory_active(command.actor))
                continue;
            auto& p = command.packet;
            try {
                if (p.spec->name == "AssetUploadRequest") {
                    start_asset_upload(command.actor, p);
                    continue;
                }
                if (p.spec->name == "SendXferPacket") {
                    receive_asset_upload(command.actor, p);
                    continue;
                }
                if (p.spec->name == "AbortXfer" && receive_asset_upload(command.actor, p))
                    continue;
                if (p.spec->name == "CreateInventoryItem") {
                    create_inventory_item(command.actor, p, command.creation_date);
                    continue;
                }
                if (p.spec->name == "LinkInventoryItem" || p.spec->name == "CopyInventoryItem" ||
                    p.spec->name == "UpdateInventoryItem" || p.spec->name == "MoveInventoryItem" ||
                    p.spec->name == "RemoveInventoryItem" || p.spec->name == "RemoveInventoryFolder" ||
                    p.spec->name == "RemoveInventoryObjects") {
                    user_inventory_operation(command.actor, p);
                    continue;
                }
                if (p.spec->name == "ObjectBuy") {
                    buy_objects(command.actor, p);
                    continue;
                }
                if (p.spec->name == "RezObject") {
                    rez_inventory_object(command.actor, p);
                    continue;
                }
                if (p.spec->name == "AgentAnimation") {
                    apply_viewer_animations(command.actor, p);
                    continue;
                }
                if (p.spec->name == "ScriptAnswerYes") {
                    answer_script_permissions(command.actor, p);
                    continue;
                }
                if (p.spec->name == "AgentSetAppearance") {
                    set_viewer_appearance(command.actor, p);
                    continue;
                }
                if (p.spec->name == "AgentIsNowWearing") {
                    set_viewer_wearables(command.actor, p);
                    continue;
                }
                if (p.spec->name == "AgentCachedTexture") {
                    answer_cached_texture(command.actor, p);
                    continue;
                }
                if (p.spec->name == "UpdateTaskInventory") {
                    update_task_inventory(command.actor, p);
                    continue;
                }
                if (p.spec->name == "MoveTaskInventory" || p.spec->name == "RemoveTaskInventory") {
                    transfer_task_inventory(command.actor, p);
                    continue;
                }
                if (p.spec->name == "SetScriptRunning" || p.spec->name == "ScriptReset") {
                    ScriptControlBackend backend;
                    backend.can_edit = [this](Uuid item, Uuid object, Uuid agent) {
                        return can_edit_script(item, object, agent);
                    };
                    backend.can_reset = [this](Uuid object, Uuid, Uuid agent) {
                        return can_edit_inventory(object, agent);
                    };
                    backend.part_local_id = [this](Uuid object) -> std::optional<std::uint32_t> {
                        auto part = world.object(object);
                        if (part)
                            return part->local_id;
                        return {};
                    };
                    backend.instance = [this](Uuid item) -> std::optional<ScriptControlInstance> {
                        auto it = scripts.find(item);
                        if (it == scripts.end() || !it->second.instance)
                            return {};
                        return ScriptControlInstance{
                            [this, item](bool value) { scripts.at(item).instance->running(value); },
                            [this, item](bool value) { scripts.at(item).task->script_running = value; },
                            [this, item] {
                                auto& entry = scripts.at(item);
                                entry.instance->reset();
                                script_events(entry.item.parent).set(item, entry.instance->event_mask());
                            }};
                    };
                    ScriptControlController* controller = nullptr;
                    backend.start = [&](auto local, auto item) { controller->on_start(local, item); };
                    backend.stop = [&](auto local, auto item) { controller->on_stop(local, item); };
                    backend.reset_event = [&](auto local, auto item) { controller->on_reset(local, item); };
                    ScriptControlController controls(std::move(backend));
                    controller = &controls;
                    auto& b = p.block("Script");
                    auto object = std::get<Uuid>(b.at("ObjectID")), item = std::get<Uuid>(b.at("ItemID"));
                    if (p.spec->name == "SetScriptRunning")
                        controls.set_running(command.actor.circuit->agent_id, object, item,
                                             std::get<bool>(b.at("Running")));
                    else
                        controls.process_reset(command.actor.circuit->agent_id, object, item);
                    continue;
                }
                if (p.spec->name == "GetScriptRunning") {
                    ScriptRunningBackend backend;
                    backend.find_instance = [this](Uuid item) -> std::optional<ScriptRunningInstance> {
                        if (!scripts_enabled())
                            return {};
                        auto i = scripts.find(item);
                        if (i == scripts.end() || !i->second.instance)
                            return {};
                        return ScriptRunningInstance{
                            [this, item] { return scripts.at(item).instance->running(); },
                            [this, item](bool running) { scripts.at(item).task->script_running = running; }};
                    };
                    backend.reply = [this, actor = command.actor](Uuid agent, Uuid object, Uuid item,
                                                                  bool running) {
                        if (!inventory_active(actor))
                            return;
                        if (capability_runtime && capability_runtime->event_queue())
                            capability_runtime->event_queue()->script_running(object, item, running, agent);
                        else
                            viewers->transport().enqueue_if_session(
                                agent, actor.circuit->session_id,
                                script_running_reply_packet(object, item, running),
                                ThrottleOutPacketType::Task);
                    };
                    GetScriptRunningController controller(std::move(backend));
                    auto& b = p.block("Script");
                    controller.request(command.actor.circuit->agent_id, std::get<Uuid>(b.at("ObjectID")),
                                       std::get<Uuid>(b.at("ItemID")));
                    continue;
                }
                if (p.spec->name == "RequestTaskInventory") {
                    request_task_file(command.actor, static_cast<std::uint32_t>(std::get<std::uint64_t>(
                                                         p.block("InventoryData").at("LocalID"))));
                    continue;
                }
                auto& b = p.block("XferID");
                auto id = std::get<std::uint64_t>(b.at("ID"));
                if (p.spec->name == "ConfirmXferPacket") {
                    xfer->ack(id, static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("Packet"))));
                    continue;
                }
                if (p.spec->name == "AbortXfer") {
                    xfer->abort(id);
                    continue;
                }
                auto metrics = viewers->transport().transfer_metrics(command.actor.circuit->agent_id,
                                                                     command.actor.circuit->session_id);
                if (!metrics)
                    continue;
                auto& filename = std::get<std::vector<std::uint8_t>>(b.at("Filename"));
                std::string name(filename.begin(), std::find(filename.begin(), filename.end(), 0));
                if (name.starts_with("mutes") && name != "mutes" + command.actor.circuit->agent_id.str())
                    continue;
                std::weak_ptr<State> weak = shared_from_this();
                auto agent = command.actor.circuit->agent_id, session = command.actor.circuit->session_id;
                int initial_ping = metrics->ping_ms;
                xfer->request(
                    name, id, metrics->task_throttle, initial_ping,
                    [weak, agent, session, initial_ping] {
                        if (auto state = weak.lock()) {
                            auto metrics = state->viewers->transport().transfer_metrics(agent, session);
                            if (metrics)
                                return metrics->ping_ms;
                        }
                        return initial_ping;
                    },
                    [weak, agent, session](auto id, auto number, const auto& data, int offset, int length) {
                        if (auto state = weak.lock())
                            state->viewers->transport().enqueue_if_session(
                                agent, session, xfer_packet(id, number, data, offset, length),
                                ThrottleOutPacketType::Task, false);
                    });
            } catch (const std::exception& e) {
                error(std::string("Task inventory/Xfer: ") + e.what());
            }
        }
        xfer->heartbeat();
        std::lock_guard data_lock(task_data_mutex);
        for (auto i = task_parts.begin(); i != task_parts.end();)
            if (!world.object(i->first))
                i = task_parts.erase(i);
            else
                ++i;
    }
    bool can_edit_inventory(Uuid object, Uuid agent) const {
        auto part = world.object(object);
        if (!part)
            return false;
        auto& policy = policies.can_edit_inventory ? policies.can_edit_inventory : policies.can_edit;
        return policy && policy(agent, *part);
    }
    bool can_edit_script(Uuid item, Uuid object, Uuid agent) const {
        auto part = world.object(object);
        if (!part || !can_edit_inventory(object, agent))
            return false;
        std::lock_guard lock(task_data_mutex);
        auto p = task_parts.find(object);
        if (p == task_parts.end())
            return false;
        if (!p->second.inventory)
            return false;
        auto i = p->second.inventory->find(item);
        if (!i)
            return false;
        if (policies.can_edit_script)
            return policies.can_edit_script(agent, *part, file_item(*i));
        return i->owner_id == agent && (i->current_permissions & 0xc000u) == 0xc000u;
    }
    static ObjectPropertiesData part_properties(const SceneObject& object, const SceneObject& root) {
        ObjectPropertiesData data;
        data.id = object.id;
        data.creator = object.creator;
        data.owner = object.owner;
        data.group = object.group;
        data.last_owner = object.last_owner;
        data.from_inventory_item = object.from_inventory_item;
        data.creation_date = object.creation_date;
        data.base_mask = root.base_mask;
        data.owner_mask = root.owner_mask;
        data.group_mask = root.group_mask;
        data.everyone_mask = root.everyone_mask;
        data.next_owner_mask = root.next_mask;
        data.sale_type = root.sale_type;
        data.sale_price = root.sale_price;
        data.category = object.category;
        data.name = object.name;
        data.description = object.description;
        data.touch_name = object.touch_name;
        data.sit_name = object.sit_name;
        return data;
    }
    void send_part_properties(Uuid id, Uuid agent) {
        const auto object = world.object(id);
        if (!object)
            return;
        const auto root =
            !object->is_attachment && object->parent_id ? world.object(object->parent_id) : object;
        if (!root)
            return;
        auto data = part_properties(*object, *root);
        {
            std::lock_guard lock(task_data_mutex);
            auto it = task_parts.find(id);
            if (it != task_parts.end() && it->second.inventory)
                data.inventory_serial = it->second.inventory->serial();
        }
        auto recipient = viewers->agent(agent);
        if (!recipient)
            return;
        if (capability_runtime && capability_runtime->event_queue()) {
            const auto& physics = object->physics;
            capability_runtime->event_queue()->physics_properties(
                object->local_id, physics.shape, physics.density, physics.friction, physics.restitution,
                physics.gravity, agent);
        }
        viewers->transport().enqueue_if_session(agent, recipient->circuit->session_id,
                                                object_properties(data), ThrottleOutPacketType::Task, false);
    }
    SceneObject inventory_drop_root(const SceneObject& part) const {
        if (part.parent_id && !part.is_attachment)
            if (auto root = world.object(part.parent_id))
                return std::move(*root);
        return part;
    }
    PartScriptEvents& script_events(Uuid object) {
        auto& part = task_parts[object];
        if (!part.events) {
            PartScriptEventsBackend backend;
            backend.has_group =
                backend.has_scene = [this, object] { return world.object(object).has_value(); };
            backend.has_root = backend.has_group;
            backend.is_root = [] { return true; };
            backend.allowed_drop = [this, object] {
                auto part = world.object(object);
                return part && (part->flags & 0x10000u) != 0;
            };
            backend.remove_targets = [this](Uuid item) {
                if (!policies.remove_script_targets)
                    throw std::runtime_error("Script target registry is not connected");
                policies.remove_script_targets(item);
            };
            backend.email = [this]() -> std::optional<PartEmailEvents> {
                return policies.script_email_events ? policies.script_email_events() : std::nullopt;
            };
            backend.aggregate_group = [this, object] {
                auto& events = *task_parts.at(object).events;
                if (policies.script_events_changed)
                    policies.script_events_changed(object, events.aggregated(), events.local_flags());
                refresh_script_flags(object);
            };
            backend.physics_events = []() {
                throw std::runtime_error("Child-part script physics subscriptions are not connected");
            };
            backend.full_update = [this, object] { refresh_script_flags(object); };
            part.events = std::make_unique<PartScriptEvents>(object, std::move(backend));
        }
        return *part.events;
    }
    void refresh_script_flags(Uuid object) {
        auto p = task_parts.find(object);
        bool empty = p == task_parts.end() || !p->second.inventory || p->second.inventory->items().empty();
        bool scripted = p != task_parts.end() && p->second.scripted;
        std::optional<std::uint32_t> flags;
        if (p != task_parts.end() && p->second.events)
            flags = p->second.events->local_flags();
        world.set_inventory_state(object, empty, scripted, flags && (*flags & 0x80u) != 0, flags);
    }
    void mark_scripted(Uuid object) {
        auto& part = task_parts[object];
        if (!part.scripted) {
            part.scripted = true;
            refresh_script_flags(object);
        }
    }

    static constexpr float time_dilation = 1.f;
    std::chrono::steady_clock::time_point stats_start{};
    std::size_t stats_frames = 0;
    std::atomic<std::uint64_t> stats_agent_updates = 0;
    std::size_t stats_physics_steps = 0;
    double stats_physics_ms = 0.;
    double stats_script_ms = 0.;
    double stats_sleep_ms = 0.;
    double stats_other_ms = 0.;
    std::atomic<std::int64_t> stats_agent_ns = 0;
    std::size_t stats_script_events = 0;
    CircuitTransportStatistics previous_packet_stats{};
    std::atomic<float> reported_fps = 0;
    void report_stats(double host_sleep_ms) {
        const auto now = std::chrono::steady_clock::now();
        if (stats_start == std::chrono::steady_clock::time_point{}) {
            stats_start = now;
            previous_packet_stats = viewers->transport().statistics();
            return;
        }
        ++stats_frames;
        stats_sleep_ms += host_sleep_ms;
        const auto elapsed = std::chrono::duration<double>(now - stats_start).count();
        if (elapsed < 3.)
            return;
        const auto normalized_frames = stats_frames * double(policies.statistics_fps_factor);
        const auto fps = static_cast<float>(normalized_frames / elapsed);
        const auto frame_ms = elapsed * 1000. / normalized_frames;
        const auto sleep_ms = std::exchange(stats_sleep_ms, 0.) / normalized_frames;
        const auto busy_ms = std::max(0., frame_ms - sleep_ms);
        const auto spare_ms =
            std::clamp(policies.target_poll_ms / policies.statistics_fps_factor - busy_ms, 0., frame_ms);
        const auto script_ms = std::exchange(stats_script_ms, 0.) / normalized_frames;
        const auto physics_ms = std::exchange(stats_physics_ms, 0.) / normalized_frames;
        reported_fps = fps;
        stats_frames = 0;
        stats_start = now;
        auto packet = lludp::make_packet("SimStats");
        auto& region = packet.block("Region");
        region["RegionX"] = std::uint64_t(descriptor.x / 256);
        region["RegionY"] = std::uint64_t(descriptor.y / 256);
        region["RegionFlags"] = std::uint64_t(region_flags());
        region["ObjectCapacity"] = std::uint64_t(policies.object_capacity);
        // Viewer-facing order from SimStats.StatsIndexID, excluding internal counters.
        static constexpr std::uint64_t viewer_stats[] = {0,  1,  2,  3,  13, 14, 11, 12, 4,  5,  7,  9,  6,
                                                         17, 18, 24, 8,  19, 20, 15, 33, 32, 27, 21, 22, 23,
                                                         25, 26, 31, 38, 34, 35, 36, 37, 39, 40, 30, 10};
        double values[41]{};
        const auto add_stat = [&](std::uint64_t id, double value) { values[id] = value; };
        const auto agents = viewers->agents();
        const auto roots =
            std::count_if(agents.begin(), agents.end(), [](const auto& agent) { return agent.root; });
        std::size_t script_instances;
        {
            std::lock_guard lock(task_data_mutex);
            script_instances = std::count_if(scripts.begin(), scripts.end(),
                                             [](const auto& entry) { return bool(entry.second.instance); });
        }
        add_stat(0, time_dilation);
        add_stat(1, std::round(double(fps) * 10.) / 10.);
        const auto physics_fps = std::min(double(fps), std::exchange(stats_physics_steps, 0) *
                                                           double(policies.statistics_fps_factor) / elapsed);
        add_stat(2, std::round(physics_fps * 10.) / 10.);
        add_stat(3, stats_agent_updates.exchange(0, std::memory_order_relaxed) / elapsed);
        add_stat(4, frame_ms);
        add_stat(6, std::round(std::exchange(stats_other_ms, 0.) / normalized_frames * 1000.) / 1000.);
        add_stat(7, std::round(physics_ms * 1000.) / 1000.);
        add_stat(8, stats_agent_ns.exchange(0, std::memory_order_relaxed) / 1000000. / normalized_frames);
        add_stat(10, script_ms);
        add_stat(11, static_cast<double>(world.size()));
        add_stat(13, static_cast<double>(roots));
        add_stat(14, static_cast<double>(agents.size() - roots));
        add_stat(15, static_cast<double>(script_instances));
        const auto packet_stats = viewers->transport().statistics();
        add_stat(17, std::round((packet_stats.received - previous_packet_stats.received) / elapsed));
        add_stat(18, std::round((packet_stats.sent - previous_packet_stats.sent) / elapsed));
        add_stat(24, static_cast<double>(packet_stats.unacked_bytes) / 1024.);
        add_stat(27, physics::UbodeWorld::time_step * 1000.f);
        add_stat(31, std::round(std::exchange(stats_script_events, 0) / elapsed));
        add_stat(32, std::round(spare_ms * 1000.) / 1000.);
        add_stat(33, std::round(sleep_ms * 1000.) / 1000.);
        auto& stats = packet.blocks["Stat"];
        for (const auto id : viewer_stats)
            stats.push_back({{"StatID", id}, {"StatValue", values[id]}});
        previous_packet_stats = packet_stats;
        for (const auto& agent : agents)
            if (agent.root)
                viewers->transport().enqueue_if_session(agent.circuit->agent_id, agent.circuit->session_id,
                                                        packet, ThrottleOutPacketType::Task);
    }
    static bool parcel_script_danger(const ScriptParcel& parcel, Uuid object_group) {
        constexpr std::uint32_t entry_and_create = 0x8000000u | 0x40u;
        const bool entry = (parcel.flags & entry_and_create) == entry_and_create;
        const bool scripts =
            (parcel.flags & 0x2u) || ((parcel.flags & 0x2000000u) && object_group == parcel.group);
        return !entry || !scripts;
    }
    bool can_edit_linkset(Uuid agent, const SceneObject& object) const {
        if (!policies.can_edit)
            return false;
        if (!object.is_attachment && object.parent_id) {
            const auto root = world.object(object.parent_id);
            return root && policies.can_edit(agent, *root);
        }
        return policies.can_edit(agent, object);
    }
    Uuid linkset_group(const SceneObject& object) const {
        if (!object.is_attachment && object.parent_id != 0)
            if (const auto root = world.object(object.parent_id))
                return root->group;
        return object.group;
    }
    std::optional<ViewerAgent> attachment_wearer(const SceneObject& object) const {
        if (object.is_attachment && viewers)
            for (const auto& wearer : viewers->agents())
                if (wearer.local_id == object.parent_id)
                    return wearer;
        return {};
    }
    bool avatar_sound_allowed(const ViewerAgent& avatar) const {
        if (!policies.script_parcel_at)
            return true;
        const auto parcel = policies.script_parcel_at(avatar.position);
        return !parcel || parcel->any_avatar_sounds ||
               (parcel->group_avatar_sounds && policies.inventory_group_member &&
                policies.inventory_group_member(avatar.circuit->agent_id, parcel->group));
    }
    std::shared_ptr<const ScriptParcel> parcel_at_truncated_position(Vector3 position) const {
        if (!policies.script_parcel_at)
            return {};
        // The original land channel truncates coordinates before checking region bounds.
        position.x = std::trunc(position.x);
        position.y = std::trunc(position.y);
        return policies.script_parcel_at(position);
    }
    Vector3 script_world_position(const SceneObject& object) const {
        if (const auto wearer = attachment_wearer(object))
            return wearer->position;
        if (!object.is_attachment && object.parent_id != 0)
            if (const auto root = world.object(object.parent_id))
                return linked_world_position(object, *root);
        return object.position;
    }
    static Vector3 linked_world_position(const SceneObject& child, const SceneObject& root) {
        const auto offset = framework::sdk_rotate_vector(child.position, root.rotation);
        return {root.position.x + offset.x, root.position.y + offset.y, root.position.z + offset.z};
    }
    struct LinksetDataSnapshot {
        framework::OrderedDictionary<std::string, std::pair<std::string, std::string>> entries;
        std::int32_t free = 0;
        std::int32_t limit = 0;
    };
    LinksetDataSnapshot linkset_data_snapshot(Uuid id) const {
        auto root = world.object(id);
        if (root && !root->is_attachment && root->parent_id)
            root = world.object(root->parent_id);
        if (!root || root->linkset_data.size() < 8) {
            const auto limit = root ? root->empty_linkset_data_limit.value_or(policies.linkset_data_limit)
                                    : policies.linkset_data_limit;
            return {{}, limit, limit};
        }
        const auto& data = root->linkset_data;
        std::size_t offset = 0;
        const auto read_integer = [&]() -> std::uint32_t {
            std::uint32_t value = 0;
            for (unsigned shift = 0; shift <= 28; shift += 7) {
                const auto byte = data.at(offset++);
                if (shift == 28 && byte > 15)
                    break;
                value |= static_cast<std::uint32_t>(byte & 127u) << shift;
                if (!(byte & 128u))
                    return value;
            }
            throw std::out_of_range("Invalid linkset data length");
        };
        const auto read_string = [&] {
            const auto size = read_integer();
            if (size > data.size() - offset)
                throw std::out_of_range("Truncated linkset data");
            const std::string_view value(reinterpret_cast<const char*>(data.data() + offset), size);
            offset += size;
            return value;
        };
        try {
            read_integer(); // storage version
            auto limit = read_integer();
            if (limit > 256 * 1024u)
                limit = 256 * 1024u;
            const auto count = read_integer();
            std::size_t used = 0;
            LinksetDataSnapshot result;
            for (std::uint32_t i = 0; i < count; ++i) {
                const auto name = read_string();
                if (name.empty())
                    continue;
                used += framework::utf16_replacing(name).size();
                const auto value = read_string();
                if (value.empty())
                    continue;
                used += framework::utf16_replacing(value).size();
                const auto pass = read_string();
                used += framework::utf16_replacing(pass).size();
                if (used > limit)
                    break;
                result.entries[std::string(name)] = {std::string(value), std::string(pass)};
            }
            result.free = used < limit ? static_cast<std::int32_t>(limit - used) : 0;
            result.limit = static_cast<std::int32_t>(limit);
            return result;
        } catch (const std::out_of_range&) {
            return {{}, policies.linkset_data_limit, policies.linkset_data_limit};
        }
    }
    std::int32_t write_linkset_data(Uuid id, std::string key, std::string value, std::string password,
                                    bool protected_call) {
        if (key.empty())
            return 2;
        std::lock_guard lock(task_data_mutex);
        auto root = world.object(id);
        if (root && !root->is_attachment && root->parent_id)
            root = world.object(root->parent_id);
        if (!root)
            return 4;
        auto snapshot = linkset_data_snapshot(root->id);
        const auto existing = snapshot.entries.find(key);
        const std::int32_t action = value.empty() ? 2 : 1;
        const std::string event_value = protected_call ? std::string{} : value;
        const auto length = [](std::string_view text) {
            return static_cast<std::int64_t>(framework::utf16_replacing(text).size());
        };
        if (existing != snapshot.entries.end()) {
            auto& [old_value, old_password] = existing->second;
            if (!old_password.empty() && old_password != password)
                return 3;
            if (value.empty())
                snapshot.entries.erase(key);
            else {
                if (old_value == value)
                    return 5;
                if (length(value) - length(old_value) > snapshot.free)
                    return 1;
                old_value = std::move(value);
            }
        } else {
            if (value.empty())
                return 4;
            if (length(key) + length(value) + length(password) > snapshot.free)
                return 1;
            snapshot.entries[key] = {std::move(value), std::move(password)};
        }
        if (!store_linkset_data(root->id, snapshot))
            return 4;
        post_linkset_data_event(*root, action, key, event_value);
        return 0;
    }
    bool store_linkset_data(Uuid root, const LinksetDataSnapshot& snapshot) {
        return world.set_linkset_data(root, encode_linkset_data(snapshot),
                                      snapshot.entries.empty() ? std::optional(snapshot.limit)
                                                               : std::nullopt);
    }
    bool link_prim_with_data(Uuid child, Uuid root) {
        std::lock_guard lock(task_data_mutex);
        const auto source = world.object(child);
        if (!source || child == root || source->parent_id || source->is_attachment)
            return false;
        const bool has_data = !source->linkset_data.empty() || source->empty_linkset_data_limit.has_value();
        auto incoming = has_data ? linkset_data_snapshot(child) : LinksetDataSnapshot{};
        if (!world.link_prim(child, root))
            return false;
        if (!has_data)
            return true;
        auto target = linkset_data_snapshot(root);
        using Entry = std::pair<std::string, std::pair<std::string, std::string>>;
        std::vector<Entry> entries;
        for (const auto& entry : incoming.entries)
            entries.emplace_back(entry.first, entry.second);
        if (incoming.limit - incoming.free >= target.free)
            std::stable_sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
                return framework::invariant_compare(left.first, right.first) < 0;
            });
        for (auto& [key, entry] : entries) {
            const auto size = framework::utf16_replacing(key).size() +
                              framework::utf16_replacing(entry.first).size() +
                              framework::utf16_replacing(entry.second).size();
            if (size >= static_cast<std::size_t>(std::max(0, target.free)))
                break;
            if (!target.entries.contains(key)) {
                target.entries[key] = std::move(entry);
                target.free -= static_cast<std::int32_t>(size);
            }
        }
        store_linkset_data(root, target);
        world.set_linkset_data(child, {});
        return true;
    }
    static std::vector<std::uint8_t> encode_linkset_data(const LinksetDataSnapshot& snapshot) {
        std::vector<std::uint8_t> bytes;
        if (!snapshot.entries.empty()) {
            const auto integer = [&](std::uint32_t number) {
                while (number >= 128) {
                    bytes.push_back(static_cast<std::uint8_t>((number & 127u) | 128u));
                    number >>= 7;
                }
                bytes.push_back(static_cast<std::uint8_t>(number));
            };
            const auto string = [&](std::string_view text) {
                integer(static_cast<std::uint32_t>(text.size()));
                bytes.insert(bytes.end(), text.begin(), text.end());
            };
            integer(1);
            integer(static_cast<std::uint32_t>(snapshot.limit));
            integer(static_cast<std::uint32_t>(snapshot.entries.size()));
            for (const auto& [name, entry] : snapshot.entries) {
                string(name);
                string(entry.first);
                string(entry.second);
            }
        }
        return bytes;
    }
    script::LslList delete_linkset_keys(Uuid id, const std::vector<std::string>& keys,
                                        std::string_view password) {
        std::int32_t deleted = 0, denied = 0;
        if (keys.empty())
            return {{deleted, denied}};
        std::lock_guard lock(task_data_mutex);
        auto root = world.object(id);
        if (root && !root->is_attachment && root->parent_id)
            root = world.object(root->parent_id);
        if (!root)
            return {{deleted, denied}};
        auto snapshot = linkset_data_snapshot(root->id);
        std::string names;
        for (const auto& key : keys) {
            const auto entry = snapshot.entries.find(key);
            if (entry == snapshot.entries.end())
                continue;
            if (!entry->second.second.empty() && entry->second.second != password) {
                ++denied;
                continue;
            }
            if (deleted++)
                names += ',';
            names += key;
            snapshot.entries.erase(key);
        }
        if (deleted) {
            if (!store_linkset_data(root->id, snapshot))
                return {{0, 0}};
            post_linkset_data_event(*root, 3, names, {});
        }
        return {{deleted, denied}};
    }
    void post_linkset_data_event(const SceneObject& root, std::int32_t action, const std::string& key,
                                 const std::string& value) {
        for (auto& [script_id, script] : scripts) {
            if (!script.instance)
                continue;
            const auto part = world.object(script.item.parent);
            if (part && (part->id == root.id || (!part->is_attachment && part->parent_id == root.local_id)))
                script.instance->post_event({"linkset_data", {action, key, value}, {}});
        }
    }
    void reset_linkset_data(Uuid id) {
        std::lock_guard lock(task_data_mutex);
        auto root = world.object(id);
        if (root && !root->is_attachment && root->parent_id)
            root = world.object(root->parent_id);
        if (!root || (root->linkset_data.empty() && !root->empty_linkset_data_limit))
            return;
        const bool changed = !linkset_data_snapshot(root->id).entries.empty();
        if (world.set_linkset_data(root->id, {}) && changed)
            post_linkset_data_event(*root, 0, {}, {});
    }
    script::LslContext script_context(const SceneObject& object, const services::InventoryItem& item) const {
        const auto wearer = attachment_wearer(object);
        std::optional<script::LslRotation> root_rotation;
        if (wearer) {
            const auto q = wearer->mouselook_rotation.value_or(wearer->rotation);
            root_rotation = script::LslRotation{q.x, q.y, q.z, q.w};
        }
        const auto root =
            !object.is_attachment && object.parent_id != 0 ? world.object(object.parent_id) : std::nullopt;
        if (root) {
            const auto& q = root->rotation;
            root_rotation = script::LslRotation{q.x, q.y, q.z, q.w};
        }
        const auto position = wearer ? wearer->position
                              : root ? linked_world_position(object, *root)
                                     : object.position;
        return {object.id,
                object.owner,
                root ? root->group : object.group,
                item.id,
                object.name,
                position,
                water_height,
                descriptor.name.value_or(""),
                {double(descriptor.x), double(descriptor.y), 0.},
                object.description,
                object.scale,
                object.is_attachment ? Vector3{} : object.velocity,
                object.angular_velocity,
                object.acceleration,
                object.creator,
                time_dilation,
                item.name,
                object.is_attachment ? object.shape.state : 0,
                object.position,
                {object.rotation.x, object.rotation.y, object.rotation.z, object.rotation.w},
                root_rotation,
                root ? std::optional(root->position) : std::nullopt,
                (root ? root->rez_start_string : object.rez_start_string).value_or("")};
    }
    std::unique_ptr<script::LslInstance>
    create_script_instance(std::shared_ptr<const script::LslProgram> program, const InventoryItem& item,
                           const SceneObject& object, bool start_running = true) {
        auto context = script_context(object, item);
        script::LslHost host;
        std::weak_ptr<State> weak = shared_from_this();
        host.min_timer_interval = policies.min_timer_interval;
        host.heap_limit_bytes = policies.script_heap_limit_bytes;
        host.script_delay_factor = policies.script_delay_factor;
        host.request_url = [weak](Uuid part, Uuid item, bool secure, bool allow_xss) {
            if (const auto state = weak.lock())
                return state->request_script_url(part, item, secure, allow_xss);
            return Uuid{};
        };
        host.ossl_enabled = policies.script_ossl_enabled;
        if (policies.environment_enabled) {
            host.region_day_fraction = [weak] {
                const auto state = weak.lock();
                if (!state)
                    return 0.f;
                std::lock_guard lock(state->environment_mutex);
                const auto day = state->environment_day;
                const auto elapsed = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                return environment_day_fraction(day, elapsed);
            };
            host.environment_timing = [weak](Vector3 position, bool region) {
                const auto state = weak.lock();
                const auto day = state ? state->environment_at(position, region) : EnvironmentDay{};
                return std::pair{day.length, day.offset};
            };
            host.environment_rotation = [weak](Vector3 position, bool region, bool moon)
                    -> std::optional<script::LslRotation> {
                const auto state = weak.lock();
                if (!state)
                    return std::nullopt;
                const auto day = state->environment_at(position, region);
                const auto& cycle = day.settings ? day.settings->get("day_cycle") : state->default_environment_cycle;
                const auto elapsed = std::chrono::duration<double>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                return sample_environment_rotation(cycle, day.settings.get(), position.z,
                                                   environment_day_fraction(day, elapsed), moon);
            };
        }
        host.replace_region_environment = [weak](Uuid part, const script::LslRegionEnvironmentEdit& update) {
            if (const auto state = weak.lock())
                return state->replace_script_region_environment(part, update);
            return std::int32_t{-3};
        };
        host.replace_parcel_environment = [weak](Uuid part, std::int32_t transition, const std::string& name) {
            if (const auto state = weak.lock())
                return state->replace_script_parcel_environment(part, transition, name);
            return std::int32_t{-1};
        };
        host.replace_agent_environment = [weak](Uuid part, Uuid item, const std::string& avatar,
                                                std::int32_t transition, const std::string& name) {
            if (const auto state = weak.lock())
                return state->replace_script_agent_environment(part, item, avatar, transition, name);
            return std::int32_t{-2};
        };
        host.region_water_height = [weak, initial = water_height.load()] {
            const auto state = weak.lock();
            return static_cast<double>(state ? state->water_height.load() : initial);
        };
        host.set_region_water_height = [weak](float height) {
            if (const auto state = weak.lock())
                state->update_region_terrain_settings(height);
        };
        host.set_region_sun_settings = [weak](bool use_estate) {
            if (const auto state = weak.lock()) {
                std::lock_guard lock(state->terrain_edit_mutex);
                if (state->policies.persist_region_sun_settings)
                    state->policies.persist_region_sun_settings(use_estate);
                state->use_estate_sun = use_estate;
            }
        };
        host.ossl_permission_error_to_owner = policies.script_ossl_permission_error_to_owner;
        host.ossl_permission_error = [weak](Uuid part, Uuid item, std::string_view function) {
            if (const auto state = weak.lock())
                return state->script_ossl_permission(part, item, function);
            return std::string(function) + " permission denied";
        };
        host.release_url = [weak](Uuid item, const std::string& url) {
            if (const auto state = weak.lock())
                state->revoke_script_urls(item, url);
        };
        host.free_urls = [weak]() -> std::int32_t {
            if (const auto state = weak.lock(); state && state->script_url_error.empty()) {
                std::lock_guard lock(state->script_http_mutex);
                return static_cast<std::int32_t>(state->policies.script_url_limit - std::ssize(state->script_urls));
            }
            return 0;
        };
        host.http_header = [weak](Uuid, Uuid id, const std::string& name) {
            if (const auto state = weak.lock()) {
                std::lock_guard lock(state->script_http_mutex);
                const auto request = state->script_http_requests.find(id);
                if (request != state->script_http_requests.end()) {
                    const auto header = request->second.headers.find(framework::utf8(
                        framework::invariant_lower_utf16(framework::utf16_replacing(name))));
                    if (header != request->second.headers.end())
                        return header->second;
                }
            }
            return std::string{};
        };
        host.http_response = [weak](Uuid, Uuid id, std::int32_t status, std::string response) {
            if (const auto state = weak.lock()) {
                std::lock_guard lock(state->script_http_mutex);
                const auto request = state->script_http_requests.find(id);
                if (request != state->script_http_requests.end() && !request->second.status) {
                    const auto agent = request->second.headers.find("user-agent");
                    if (request->second.content_type == "text/plain" && agent != request->second.headers.end() &&
                        framework::invariant_lower_utf16(framework::utf16_replacing(agent->second)).find(u"msie") !=
                            std::u16string::npos)
                        response = script_http_legacy_html(response);
                    request->second.status = status;
                    request->second.response = std::move(response);
                }
            }
        };
        host.http_content_type = [weak](Uuid part, Uuid, Uuid id, std::int32_t type) {
            const auto state = weak.lock();
            if (!state)
                return;
            constexpr std::array<std::string_view, 9> mime_types{
                "text/plain", "text/html", "application/xml", "application/xhtml+xml", "application/atom+xml",
                "application/json", "application/llsd+xml", "application/x-www-form-urlencoded",
                "application/rss+xml",
            };
            std::string owner_address;
            if (type == 1) {
                auto object = state->world.object(part);
                if (object && object->parent_id && !object->is_attachment)
                    object = state->world.object(object->parent_id);
                if (object)
                    if (const auto owner = state->viewers->agent(object->owner); owner && owner->root)
                        owner_address = owner->endpoint.address;
            }
            std::lock_guard lock(state->script_http_mutex);
            const auto request = state->script_http_requests.find(id);
            if (request == state->script_http_requests.end() || !request->second.deadline)
                return;
            auto& data = request->second;
            if (type == 1) {
                const auto user_agent = data.headers.find("user-agent");
                if (owner_address.empty() || user_agent == data.headers.end() ||
                    user_agent->second.find("SecondLife") == std::string::npos ||
                    framework::trim_utf8(data.headers.at("x-remote-ip")) != owner_address)
                    type = 0;
            }
            data.content_type = type >= 0 && type < std::ssize(mime_types) ? mime_types[type] : mime_types[0];
        };
        host.unix_time = user_inventory_unix_time;
        host.chat = [weak](const auto& chat) {
            if (auto state = weak.lock())
                state->script_chat(chat);
        };
        host.ground_height = [weak](float x, float y) {
            if (auto state = weak.lock())
                return state->world.terrain().height_at(x, y);
            return 0.f;
        };
        host.ground_normal = [weak](float x, float y, float offset_x, float offset_y) {
            if (auto state = weak.lock())
                return state->world.terrain().script_ground_normal(x, y, offset_x, offset_y);
            return Vector3{0.f, 0.f, 1.f};
        };
        host.target_omega = [weak](Uuid id, Vector3 angular_velocity, bool full_refresh) {
            if (auto state = weak.lock())
                state->world.update_angular_velocity(id, angular_velocity, full_refresh);
        };
        host.region_agent_count = [weak] {
            const auto state = weak.lock();
            if (!state || !state->viewers)
                return std::int32_t{0};
            const auto agents = state->viewers->agents();
            return static_cast<std::int32_t>(std::count_if(
                agents.begin(), agents.end(), [](const ViewerAgent& agent) { return agent.root; }));
        };
        host.agent_size = [weak](Uuid id) {
            const auto state = weak.lock();
            if (!state || !state->viewers)
                return Vector3{};
            const auto avatar = state->viewers->agent(id);
            const auto appearance = avatar && avatar->root ? avatar->appearance() : nullptr;
            return appearance ? appearance->avatar_size() : Vector3{};
        };
        if (policies.load_preferences)
            host.agent_language = [weak](Uuid id) {
                if (const auto state = weak.lock()) {
                    if (state->policies.load_preference_language)
                        return state->policies.load_preference_language(id);
                    const auto prefs = state->policies.load_preferences(id);
                    if (prefs.language_public)
                        return prefs.language;
                }
                return std::string("en-us");
            };
        host.object_owner = [weak](Uuid id) -> std::optional<Uuid> {
            if (const auto state = weak.lock())
                if (const auto object = state->world.object(id))
                    return object->owner;
            return {};
        };
        host.object_group = [weak](Uuid id) -> std::optional<Uuid> {
            const auto state = weak.lock();
            if (!state || !state->viewers)
                return {};
            if (const auto object = state->world.object(id)) {
                if (!object->is_attachment)
                    return state->linkset_group(*object);
                for (const auto& avatar : state->viewers->agents())
                    if (avatar.root && avatar.local_id == object->parent_id)
                        return avatar.active_group_id();
                return {};
            }
            if (const auto avatar = state->viewers->agent(id); avatar && avatar->root)
                return avatar->active_group_id();
            return {};
        };
        host.prim_count = [weak](Uuid id, bool include_attachments) {
            if (const auto state = weak.lock()) {
                const auto prims = state->world.prim_count(id, include_attachments);
                if (prims && include_attachments)
                    return prims + static_cast<std::int32_t>(state->linkset_sitters(id).size());
                return prims;
            }
            return std::int32_t{0};
        };
        host.face_count = [weak](Uuid id) {
            if (const auto state = weak.lock())
                if (const auto object = state->world.object(id))
                    return std::int32_t(object->shape.face_count());
            return std::int32_t{0};
        };
        host.object_name = [weak](Uuid id) -> std::optional<std::string> {
            if (const auto state = weak.lock())
                if (const auto object = state->world.object(id))
                    return object->name;
            return {};
        };
        host.avatar_ip_address = [weak](Uuid part_id, Uuid agent_id) -> std::string {
            const auto state = weak.lock();
            if (!state || !state->viewers)
                return {};
            const auto part = state->world.object(part_id);
            if (!part || part->owner.is_zero())
                return {};
            if (!state->policies.bypass_object_permissions && state->god_rights_level(part->owner) < 200)
                return {};
            const auto avatar = state->viewers->agent(agent_id);
            if (!avatar)
                return {};
            auto ip = IpAddress::parse(avatar->endpoint.address);
            if (!ip)
                return {};
            if (ip->v6())
                ip->scope_id(avatar->endpoint.scope_id);
            return ip->str();
        };
        host.avatar_name = [weak](Uuid id) -> std::optional<std::string> {
            if (const auto state = weak.lock())
                return state->script_avatar_name(id);
            return {};
        };
        host.request_identity_name = [weak](Uuid part, Uuid item, Uuid target,
                                            script::LslHost::NameFormatter format) {
            if (const auto state = weak.lock()) {
                const DataserverRequest request{part, item, target, Uuid::random(), format, {}};
                if (const auto avatar = state->script_avatar_name(target)) {
                    state->post_dataserver_reply(request, *avatar);
                    return std::pair{request.query, true};
                }
                state->dataserver_requests.push_back(request);
                return std::pair{request.query, false};
            }
            return std::pair{Uuid{}, false};
        };
        host.avatar_key_by_name = [weak](std::string_view name) {
            if (const auto state = weak.lock())
                return state->script_avatar_key(name);
            return Uuid{};
        };
        host.request_identity_key = [weak](Uuid part, Uuid item, script::LslAvatarName name) {
            if (const auto state = weak.lock()) {
                const auto id = state->script_avatar_key(name.scene_name());
                DataserverRequest request{part, item, {}, Uuid::random(), nullptr, std::move(name)};
                const auto query = request.query;
                if (!id.is_zero()) {
                    state->post_dataserver_reply(request, id.str());
                    return std::pair{query, true};
                }
                state->dataserver_requests.push_back(std::move(request));
                return std::pair{query, false};
            }
            return std::pair{Uuid{}, false};
        };
        host.linkset_data_read = [weak](Uuid id, std::string_view key, std::string_view password) {
            if (const auto state = weak.lock()) {
                const auto snapshot = state->linkset_data_snapshot(id);
                const auto entry = snapshot.entries.find(std::string(key));
                if (entry != snapshot.entries.end() &&
                    (entry->second.second.empty() || entry->second.second == password))
                    return entry->second.first;
            }
            return std::string{};
        };
        host.linkset_data_size = [weak](Uuid id, bool available) {
            if (const auto state = weak.lock()) {
                const auto snapshot = state->linkset_data_snapshot(id);
                return available ? snapshot.free : static_cast<std::int32_t>(snapshot.entries.size());
            }
            return available ? std::int32_t{32 * 1024} : std::int32_t{0};
        };
        host.linkset_data_write = [weak](Uuid id, std::string key, std::string value, std::string password,
                                         bool protected_call) {
            if (const auto state = weak.lock())
                return state->write_linkset_data(id, std::move(key), std::move(value), std::move(password),
                                                 protected_call);
            return std::int32_t{4};
        };
        host.linkset_data_reset = [weak](Uuid id) {
            if (const auto state = weak.lock())
                state->reset_linkset_data(id);
        };
        host.linkset_data_keys = [weak](Uuid id) {
            std::vector<std::string> keys;
            if (const auto state = weak.lock())
                for (const auto& [key, entry] : state->linkset_data_snapshot(id).entries)
                    keys.push_back(key);
            return keys;
        };
        host.linkset_data_delete_keys = [weak](Uuid id, const std::vector<std::string>& keys,
                                               std::string_view password) {
            if (const auto state = weak.lock())
                return state->delete_linkset_keys(id, keys, password);
            return script::LslList{{0, 0}};
        };
        host.link_key = [weak](Uuid id, std::int32_t number) {
            if (const auto state = weak.lock()) {
                const auto part = state->world.link_key(id, number);
                if (!part.is_zero() || number < 2)
                    return part;
                const auto prims = state->world.prim_count(id, true);
                if (prims && number > prims) {
                    const auto sitters = state->linkset_sitters(id);
                    const auto index = static_cast<std::size_t>(number - prims - 1);
                    if (index < sitters.size())
                        return sitters[index].circuit->agent_id;
                }
            }
            return Uuid{};
        };
        host.sit_target_avatar = [weak](Uuid id, std::int32_t link) {
            if (const auto state = weak.lock(); state && state->viewers) {
                const auto part = state->world.link_key(id, link);
                if (!part.is_zero())
                    for (const auto& avatar : state->viewers->agents())
                        if (avatar.root && avatar.seat && avatar.seat->explicit_target && avatar.seat->part == part)
                            return avatar.circuit->agent_id;
            }
            return Uuid{};
        };
        host.sit_flags = [weak](Uuid id, std::int32_t link) {
            if (const auto state = weak.lock())
                if (const auto part = state->world.object(state->world.link_key(id, link)))
                    return std::int32_t{0x32} | std::int32_t(has_configured_sit_target(*part));
            return std::int32_t{0};
        };
        host.force_sit = [weak](Uuid avatar_id, Uuid target_id) {
            const auto state = weak.lock();
            if (!state || !state->viewers)
                return;
            const auto avatar = state->viewers->agent(avatar_id);
            const auto target = state->world.object(target_id);
            if (!avatar || !target)
                return;

            const auto agents = state->viewers->agents();
            const auto target_is_occupied = [&](const ViewerAgent& other) {
                return other.seat && other.seat->explicit_target && other.seat->part == target_id;
            };
            if (std::none_of(agents.begin(), agents.end(), target_is_occupied))
                state->request_object_seat(*avatar, target_id, target->sit_target_position);
        };
        host.unsit = [weak](Uuid id, Uuid target) {
            const auto state = weak.lock();
            if (!state || !state->viewers)
                return;
            const auto avatar = state->viewers->agent(target);
            const auto part = state->world.object(id);
            if (!avatar || !avatar->root || (!avatar->seat && !avatar->ground_sitting) || !part)
                return;
            const auto root = state->world.object(state->world.link_key(id, 1));
            const bool same_linkset = avatar->seat && root && avatar->seat->parent_local == root->local_id;
            if (!same_linkset) {
                const auto parcel = state->policies.script_parcel_at
                    ? state->policies.script_parcel_at(avatar->position) : nullptr;
                if (!parcel || (part->owner != parcel->owner &&
                                !(parcel->group_owned && part->owner == part->group &&
                                  part->group == parcel->group) &&
                                state->god_rights_level(part->owner) < 200))
                    return;
            }
            if (avatar->seat)
                state->stand_from_seat(*avatar);
            else
                state->change_ground_sit(*avatar, false);
        };
        host.object_permission = [weak](Uuid id, std::int32_t selector) -> std::optional<std::uint32_t> {
            if (const auto state = weak.lock())
                if (const auto part = state->world.object(id)) {
                    switch (selector) {
                    case 0: return part->base_mask;
                    case 1: return part->owner_mask;
                    case 2: return part->group_mask;
                    case 3: return part->everyone_mask;
                    case 4: return part->next_mask;
                    }
                }
            return std::nullopt;
        };
        host.region_fps = [weak] {
            if (const auto state = weak.lock())
                return state->reported_fps.load();
            return 0.f;
        };
        host.geometric_center = [weak](Uuid id) {
            if (const auto state = weak.lock())
                return state->world.geometric_center(id);
            return Vector3{};
        };
        host.link_number = [weak](Uuid id) {
            if (const auto state = weak.lock())
                return state->world.link_number(id);
            return std::int32_t{0};
        };
        host.attached_list = [weak](Uuid id) {
            script::LslList result;
            const auto state = weak.lock();
            const auto avatar = state && state->viewers ? state->viewers->agent(id) : std::nullopt;
            if (!avatar) {
                result.items.emplace_back(std::string("NOT_FOUND"));
                return result;
            }
            {
                std::lock_guard lock(state->teleport_mutex);
                if (!avatar->root || std::any_of(state->pending_teleports.begin(),
                                                 state->pending_teleports.end(), [&](const auto& transfer) {
                                                     return transfer.session == avatar->circuit->session_id;
                                                 })) {
                    result.items.emplace_back(std::string("NOT_ON_REGION"));
                    return result;
                }
            }
            for (const auto& object : state->world.attachments(avatar->local_id))
                if (!is_hud_attachment(object))
                    result.items.emplace_back(object.id.str());
            return result;
        };
        host.region_flags = [weak] {
            if (const auto state = weak.lock())
                return std::bit_cast<std::int32_t>(state->region_flags());
            return std::int32_t{67108864};
        };
        host.parcel_flags = [weak](Vector3 position) {
            const auto state = weak.lock();
            if (!state || !state->policies.script_parcel_at)
                return std::int32_t{0};
            const auto parcel = state->policies.script_parcel_at(position);
            return parcel ? std::bit_cast<std::int32_t>(parcel->flags) : std::int32_t{0};
        };
        host.parcel_owner = [weak](Vector3 position) -> std::optional<Uuid> {
            if (const auto state = weak.lock())
                if (const auto parcel = state->parcel_at_truncated_position(position))
                    return parcel->owner;
            return {};
        };
        host.entity_position = [weak](Uuid id) -> std::optional<Vector3> {
            const auto state = weak.lock();
            if (!state)
                return {};
            if (state->viewers)
                if (const auto avatar = state->viewers->agent(id))
                    return avatar->position;
            if (const auto object = state->world.object(id))
                return state->script_world_position(*object);
            return {};
        };
        host.edit_parcel_list = [weak](Uuid object, script::LslParcelListAction action, bool bans,
                                      Uuid agent, double hours) {
            const auto state = weak.lock();
            return !state || state->edit_script_parcel_list(object, action, bans, agent, hours);
        };
        host.script_danger = [weak](Vector3 position, Uuid object_group) {
            const auto state = weak.lock();
            if (!state || !state->policies.script_parcel_at)
                return true;
            const auto parcel = state->policies.script_parcel_at(position);
            return !parcel || parcel_script_danger(*parcel, object_group);
        };
        host.parcel_details = [weak](Vector3 position, Uuid object_group, const script::LslList& selectors) {
            const auto state = weak.lock();
            if (!state || !state->policies.script_parcel_at)
                return script::LslList{{std::int32_t{0}}};
            const auto parcel = state->policies.script_parcel_at(position);
            if (!parcel)
                return script::LslList{{std::int32_t{0}}};
            script::LslList result;
            for (const auto& selector : selectors.items) {
                const auto* code = std::get_if<std::int32_t>(&selector);
                if (!code)
                    return script::LslList{{std::int32_t{0}}};
                switch (*code) {
                case 0:
                    result.items.emplace_back(parcel->name);
                    break;
                case 1:
                    result.items.emplace_back(parcel->description);
                    break;
                case 2:
                    result.items.emplace_back(parcel->owner.str());
                    break;
                case 3:
                    result.items.emplace_back(parcel->group.str());
                    break;
                case 4:
                    result.items.emplace_back(parcel->area);
                    break;
                case 5:
                    result.items.emplace_back(parcel->id.str());
                    break;
                case 6:
                    result.items.emplace_back(std::int32_t(parcel->see_avatars));
                    break;
                case 7:
                    result.items.emplace_back(state->policies.script_parcel_capacity
                                                  ? state->policies.script_parcel_capacity(parcel->area)
                                                  : std::int32_t{0});
                    break;
                case 8: {
                    constexpr std::uint32_t temporary_on_rez = 0x20000000u;
                    std::int32_t count = 0;
                    for (const auto& object : state->world.objects()) {
                        if (object.is_attachment || (object.flags & temporary_on_rez))
                            continue;
                        const auto location = state->policies.script_parcel_at(object.position);
                        if (location && location->id == parcel->id)
                            ++count;
                    }
                    result.items.emplace_back(count);
                    break;
                }
                case 9:
                    result.items.emplace_back(parcel->landing_point);
                    break;
                case 10:
                    result.items.emplace_back(parcel->landing_look_at);
                    break;
                case 11:
                    result.items.emplace_back(parcel->landing_type);
                    break;
                case 12:
                    result.items.emplace_back(std::bit_cast<std::int32_t>(parcel->flags));
                    break;
                case 13:
                    result.items.emplace_back(std::int32_t(parcel_script_danger(*parcel, object_group)));
                    break;
                case 64:
                    result.items.emplace_back(parcel->dwell);
                    break;
                case 65:
                    result.items.emplace_back(parcel->claim_date);
                    break;
                case 66:
                    result.items.emplace_back(parcel->center);
                    break;
                default:
                    return script::LslList{{std::int32_t{0}}};
                }
            }
            return result;
        };
        host.region_agents = [weak](std::int32_t scope, Vector3 position) {
            std::vector<Uuid> result;
            const auto state = weak.lock();
            if (!state || !state->viewers)
                return result;
            const auto& parcel_at = state->policies.script_parcel_at;
            const auto source =
                scope != 4 && parcel_at ? parcel_at(position) : std::shared_ptr<const ScriptParcel>{};
            if (scope != 4 && !source)
                return result;
            for (const auto& agent : state->viewers->agents()) {
                if (!agent.root)
                    continue;
                if (source) {
                    const auto parcel = parcel_at(agent.position);
                    if (!parcel || (scope == 1 ? parcel->id != source->id : parcel->owner != source->owner))
                        continue;
                }
                const auto id = agent.circuit->agent_id;
                if (!state->policies.viewer_ui_god || !state->policies.viewer_ui_god(id))
                    result.push_back(id);
            }
            return result;
        };
        host.region_agent_names = [weak] {
            const auto state = weak.lock();
            return state && state->viewers ? state->viewers->root_agent_names() : std::vector<std::string>{};
        };
        host.inventory_names = [weak](Uuid id, std::int32_t type) {
            std::vector<std::string> names;
            if (const auto state = weak.lock()) {
                const auto part = state->task_parts.find(id);
                if (part != state->task_parts.end() && part->second.inventory)
                    for (const auto& item : part->second.inventory->items())
                        if (type == -1 || item->type == type)
                            names.push_back(item->name.value_or(""));
            }
            return names;
        };
        const auto inventory_item =
            [weak](Uuid id, const std::string& name,
                   std::optional<std::int32_t> type = {}) -> TaskInventoryRegistry::Item {
            if (const auto state = weak.lock())
                return state->script_inventory_item(id, name, type);
            return {};
        };
        host.request_inventory_data = [weak](Uuid part, Uuid item, std::string name) {
            if (const auto state = weak.lock()) {
                const auto query = Uuid::random();
                state->dataserver_requests.push_back({part, item, {}, query, nullptr, {}, {}, std::move(name)});
                return query;
            }
            return Uuid{};
        };
        host.request_simulator_data = [weak](Uuid part, Uuid item, std::string name, std::int32_t selector) {
            const auto state = weak.lock();
            if (!state)
                return Uuid{};
            DataserverRequest request{part, item, {}, Uuid::random(), nullptr, {}, {}, {}};
            const auto query = request.query;
            if (state->descriptor.name == name) {
                auto region = state->descriptor;
                region.maturity = state->region_maturity;
                const auto reply = simulator_data(region, selector, true);
                if (!reply)
                    return Uuid{};
                state->post_dataserver_reply(request, *reply);
            } else {
                request.deferred_lookup = [resolve = state->policies.script_grid_region,
                                           name = std::move(name), selector]() -> std::optional<std::string> {
                    const auto region = resolve ? resolve(name, selector == 5) : std::nullopt;
                    if (!region)
                        return "unknown";
                    return simulator_data(*region, selector, false);
                };
                state->dataserver_requests.push_back(std::move(request));
            }
            return query;
        };
        using Clock = std::chrono::steady_clock;
        using OnlineCache = std::map<Uuid, std::pair<bool, Clock::time_point>>;
        host.request_agent_data = [weak, online_cache = std::make_shared<OnlineCache>()](
            Uuid part, Uuid item, Uuid agent, std::int32_t selector) {
            const auto state = weak.lock();
            if (!state)
                return Uuid{};
            DataserverRequest request{part, item, {}, Uuid::random(), nullptr, {}, {}, {}};
            const auto query = request.query;
            const auto avatar = state->viewers ? state->viewers->agent(agent) : std::nullopt;
            if (avatar && (selector == 1 || selector == 2 || selector == 4)) {
                state->post_dataserver_reply(request, selector == 1 ? "1" :
                    selector == 2 ? avatar->circuit->name() : "0,0,0,0,0,0");
                return query;
            }
            request.deferred_lookup = [resolve = state->policies.script_account, presence = &state->sessions,
                                       online_cache, agent, selector,
                                       foreign = avatar && avatar->circuit->verified_foreign_login]()
                -> std::optional<std::string> {
                if (foreign)
                    return selector == 7 ? std::optional<std::string>("0") : std::nullopt;
                const auto account = resolve ? resolve(agent) : std::nullopt;
                if (!account)
                    return std::nullopt;
                switch (selector) {
                case 1: {
                    const auto now = Clock::now();
                    if (const auto entry = online_cache->find(agent);
                        entry != online_cache->end() && entry->second.second > now)
                        return entry->second.first ? "1" : "0";
                    const auto agents = presence->agents({agent.str()});
                    const bool online = std::any_of(agents.begin(), agents.end(),
                        [](const auto& info) { return !info.region_id.is_zero(); });
                    std::erase_if(*online_cache, [now](const auto& entry) { return entry.second.second <= now; });
                    (*online_cache)[agent] = {online, Clock::now() + std::chrono::seconds(20)};
                    return online ? "1" : "0";
                }
                case 2:
                    return account->name();
                case 3:
                    return script::lsl_date_from_unix(account->created);
                case 4:
                    return "0,0,0,0,0,0";
                case 7:
                    return std::to_string(account->user_level);
                case 8:
                    return std::to_string((static_cast<std::uint32_t>(account->user_flags) >> 2) & 3u);
                default:
                    return std::nullopt;
                }
            };
            state->dataserver_requests.push_back(std::move(request));
            return query;
        };
        host.inventory_type = [inventory_item](Uuid id, const std::string& name) {
            const auto item = inventory_item(id, name);
            return item ? item->type : -1;
        };
        host.notecard_asset = [inventory_item](Uuid id, const std::string& name, bool typed) -> std::optional<Uuid> {
            constexpr auto notecard = static_cast<std::int32_t>(AssetType::Notecard);
            const auto item = inventory_item(id, name, typed ? std::optional(notecard) : std::nullopt);
            if (item && item->type == notecard)
                return item->asset_id;
            return {};
        };
        host.cached_notecard_line = [](Uuid asset, std::int32_t line) {
            if (const auto cached = cached_notecard(asset))
                return notecard_reply(*cached, line, 1024);
            return std::string("\n\x15\n");
        };
        host.request_notecard = [weak](Uuid part, Uuid item, Uuid asset, std::optional<std::int32_t> line) {
            const auto state = weak.lock();
            if (!state)
                return Uuid{};
            DataserverRequest request{part, item, {}, Uuid::random(), nullptr, {}, {}};
            const auto query = request.query;
            const auto limit = state->policies.notecard_line_chars;
            if (const auto cached = cached_notecard(asset)) {
                state->post_dataserver_reply(request, notecard_reply(*cached, line, limit));
            } else {
                request.deferred_lookup = [load = state->policies.load_asset, asset, line, limit] {
                    auto cached = cached_notecard(asset);
                    if (!cached) {
                        const auto card = load ? load(asset) : nullptr;
                        if (!card || card->type() != static_cast<std::int8_t>(AssetType::Notecard))
                            return std::string{};
                        cached = cached_notecard(asset,
                            std::make_shared<const std::vector<std::string>>(script::notecard_lines(card->data)));
                    }
                    return notecard_reply(*cached, line, limit);
                };
                state->dataserver_requests.push_back(std::move(request));
            }
            return query;
        };
        host.inventory_metadata =
            [inventory_item](Uuid id,
                             const std::string& name) -> std::optional<script::LslInventoryMetadata> {
            const auto item = inventory_item(id, name);
            if (!item)
                return std::nullopt;
            return script::LslInventoryMetadata{item->description.value_or(""), item->creator_id,
                                                item->creation_date};
        };
        host.animation_override = [weak](Uuid item, std::string_view movement) -> std::string {
            const auto state = weak.lock();
            if (!state)
                return {};
            std::lock_guard lock(state->task_data_mutex);
            const auto found = state->scripts.find(item);
            if (found == state->scripts.end() || !found->second.task)
                return {};
            const auto& task = *found->second.task;
            const auto avatar = state->viewers->agent(task.permissions_granter);
            if (task.permissions_granter.is_zero() || !(task.permissions_mask & (0x8000 | 0x10)) || !avatar ||
                !avatar->root)
                return {};
            const auto original = state->movement_animation_id(movement);
            if (original.is_zero())
                return {};
            const auto active = state->avatar_script_animations.find(task.permissions_granter);
            if (active == state->avatar_script_animations.end() ||
                active->second.session != avatar->circuit->session_id)
                return std::string(movement);
            const auto replacement = active->second.overrides.find(original);
            if (replacement == active->second.overrides.end())
                return std::string(movement);
            if (state->policies.animations) {
                const auto& names = state->policies.animations->by_id;
                if (const auto builtin = names.find(replacement->second); builtin != names.end()) {
                    auto name = builtin->second;
                    for (auto& c : name)
                        if (c >= 'A' && c <= 'Z')
                            c += 'a' - 'A';
                    return name;
                }
            }
            const auto part = state->task_parts.find(found->second.item.parent);
            if (part != state->task_parts.end() && part->second.inventory)
                for (const auto& entry : part->second.inventory->items())
                    if (entry->asset_id == replacement->second)
                        return entry->name.value_or("");
            return replacement->second.str();
        };
        host.change_animation_override = [weak](Uuid part, Uuid item, std::string_view movement,
                                                const auto& animation) {
            if (const auto state = weak.lock())
                state->change_animation_override(part, item, movement, animation);
        };
        host.avatar_movement_state = [weak](Uuid agent) -> std::string {
            const auto state = weak.lock();
            if (!state)
                return {};
            const auto avatar = state->viewers->agent(agent);
            if (!avatar || !avatar->root)
                return {};
            if (avatar->seat)
                return "Sitting";
            if (avatar->ground_sitting)
                return "Sitting on Ground";
            const auto physics = state->viewer_physics.load();
            if (!physics)
                return "Standing";
            if (!state->policies.animations)
                return {};
            const auto animation = physics->movement_animation(agent);
            const auto& catalog = *state->policies.animations;
            if (catalog.name(animation) == "FALLDOWN")
                return "Falling Down";
            const auto movement = catalog.states.find(animation);
            return movement == catalog.states.end() ? std::string{} : movement->second;
        };
        host.avatar_animations = [weak](Uuid agent) {
            std::vector<Uuid> result;
            if (const auto state = weak.lock()) {
                std::lock_guard lock(state->task_data_mutex);
                const auto avatar = state->viewers->agent(agent);
                if (!avatar || !avatar->root)
                    return result;
                const auto found = state->avatar_script_animations.find(agent);
                const auto movement = state->default_avatar_animation(agent);
                if (!movement.is_zero())
                    result.push_back(movement);
                if (found != state->avatar_script_animations.end() &&
                    found->second.session == avatar->circuit->session_id)
                    for (const auto& entry : found->second.entries)
                        if (entry.animation != movement)
                            result.push_back(entry.animation);
            }
            return result;
        };
        host.script_animation = [weak](Uuid part, Uuid script, const std::string& name, bool start) {
            if (const auto state = weak.lock())
                state->change_script_animation(part, script, name, start);
        };
        host.ossl_avatar_animation = [weak](Uuid part, Uuid avatar, const std::string& name, bool start) {
            if (const auto state = weak.lock())
                state->change_ossl_avatar_animation(part, avatar, name, start);
        };
        host.request_permissions = [weak](Uuid part, Uuid script, std::string_view agent, std::int32_t mask) {
            if (const auto state = weak.lock())
                state->request_script_permissions(part, script, agent, mask);
        };
        host.clear_script_permissions = [weak](Uuid item) {
            if (const auto state = weak.lock()) {
                std::lock_guard lock(state->task_data_mutex);
                state->cancel_dataserver_requests(item);
                state->revoke_script_urls(item);
                const auto found = state->scripts.find(item);
                if (found == state->scripts.end() || !found->second.task)
                    return;
                auto& script = found->second;
                script.question.reset();
                script.task->permissions_granter = {};
                script.task->permissions_mask = 0;
            }
        };
        host.release_script_urls = [weak](Uuid item) {
            if (const auto state = weak.lock())
                state->revoke_script_urls(item, std::nullopt, false);
        };
        host.find_script = [inventory_item](Uuid part, const std::string& name) {
            const auto row = inventory_item(part, name);
            return row && row->type == 10 ? row->item_id : Uuid{};
        };
        host.script_running = [weak](Uuid item) {
            if (const auto state = weak.lock()) {
                const auto entry = state->scripts.find(item);
                return entry != state->scripts.end() && entry->second.instance &&
                       entry->second.instance->running();
            }
            return false;
        };
        host.set_script_running = [weak](Uuid item, bool running) {
            if (const auto state = weak.lock()) {
                const auto entry = state->scripts.find(item);
                if (entry != state->scripts.end() && entry->second.instance) {
                    entry->second.instance->running(running);
                    if (entry->second.task)
                        entry->second.task->script_running = running;
                }
            }
        };
        host.reset_other_script = [weak](Uuid item) {
            if (const auto state = weak.lock()) {
                const auto entry = state->scripts.find(item);
                if (entry != state->scripts.end() && entry->second.instance) {
                    auto& target = entry->second;
                    target.instance->reset();
                    state->script_events(target.item.parent).set(item, target.instance->event_mask());
                }
            }
        };
        host.script_permissions = [weak](Uuid part, Uuid script) {
            std::pair<Uuid, std::int32_t> result;
            if (const auto state = weak.lock()) {
                std::lock_guard lock(state->task_data_mutex);
                const auto found = state->task_parts.find(part);
                if (found != state->task_parts.end() && found->second.inventory)
                    if (const auto item = found->second.inventory->find(script))
                        result = {item->permissions_granter, item->permissions_mask};
                if (state->policies.automatic_link_permission)
                    result.second |= 0x80;
            }
            return result;
        };
        host.inventory_permission = [inventory_item](Uuid id, const std::string& name,
                                                     std::int32_t selector) -> std::optional<std::uint32_t> {
            const auto item = inventory_item(id, name);
            if (!item)
                return {};
            switch (selector) {
            case 0:
                return item->base_permissions;
            case 1:
                return item->current_permissions;
            case 2:
                return item->group_permissions;
            case 3:
                return item->everyone_permissions;
            case 4:
                return item->next_permissions;
            default:
                return {};
            }
        };
        host.inventory_key = [inventory_item](Uuid id, const std::string& name) {
            constexpr std::uint32_t copy_modify_transfer = 0xe000;
            const auto item = inventory_item(id, name);
            return item && (item->current_permissions & copy_modify_transfer) == copy_modify_transfer
                       ? item->asset_id
                       : Uuid{};
        };
        host.set_click_action = [weak](Uuid id, std::uint8_t action) {
            if (auto state = weak.lock())
                state->world.update_click_action(id, action, true);
        };
        host.set_pass_touches = [weak](Uuid id, bool pass) {
            if (const auto state = weak.lock())
                state->world.set_pass_touches(id, pass);
        };
        host.set_sit_target = [weak](Uuid id, std::int32_t link, framework::Vector3d offset,
                                     script::LslRotation rotation) {
            if (const auto state = weak.lock()) {
                const auto part = state->world.link_key(id, link);
                if (!part.is_zero())
                    state->world.set_sit_target(part, {float(offset.x), float(offset.y), float(offset.z)},
                        {float(rotation.x), float(rotation.y), float(rotation.z), float(rotation.w)});
            }
        };
        host.sit_active_range = [weak](Uuid id, std::int32_t link) {
            if (const auto state = weak.lock())
                if (const auto part = state->world.object(state->world.link_key(id, link)))
                    return part->sit_active_range;
            return static_cast<float>(std::numeric_limits<std::int32_t>::min());
        };
        host.stand_target = [weak](Uuid id, std::int32_t link) -> framework::Vector3d {
            if (const auto state = weak.lock())
                if (const auto part = state->world.object(state->world.link_key(id, link))) {
                    const auto& offset = part->stand_target;
                    return {offset.x, offset.y, offset.z};
                }
            return {};
        };
        host.set_stand_target = [weak](Uuid id, std::int32_t link, framework::Vector3d offset) {
            if (const auto state = weak.lock())
                state->world.set_stand_target(state->world.link_key(id, link),
                                             {float(offset.x), float(offset.y), float(offset.z)});
        };
        host.set_seat_camera = [weak](Uuid id, std::int32_t link, std::optional<framework::Vector3d> eye,
                                      std::optional<framework::Vector3d> at, std::optional<bool> mouselook) {
            const auto convert = [](const auto& offset) -> std::optional<Vector3> {
                if (!offset)
                    return std::nullopt;
                return Vector3{float(offset->x), float(offset->y), float(offset->z)};
            };
            if (const auto state = weak.lock())
                state->world.set_seat_camera(state->world.link_key(id, link), convert(eye), convert(at), mouselook);
        };
        host.set_sit_active_range = [weak](Uuid id, std::int32_t link, float range) {
            const auto state = weak.lock();
            if (!state)
                return;
            if (link >= 0 || link == -4) {
                state->world.set_sit_active_range(state->world.link_key(id, link), range);
                return;
            }
            if (link < -3)
                return;
            const auto root = state->world.object(state->world.link_key(id, 1));
            if (!root)
                return;
            for (const auto& part : state->world.objects())
                if ((part.id == root->id || (!part.is_attachment && part.parent_id == root->local_id)) &&
                    (link != -2 || part.id != id) && (link != -3 || part.id != root->id))
                    state->world.set_sit_active_range(part.id, range);
        };
        host.allow_inventory_drop = [weak](Uuid id, bool allowed) {
            if (const auto state = weak.lock())
                if (const auto part = state->world.object(id)) {
                    const auto root = state->inventory_drop_root(*part).id;
                    state->world.set_allowed_drop(root, allowed);
                    state->script_events(root).aggregate();
                }
        };
        host.set_object_scale = [weak](Uuid id, Vector3 requested) -> std::optional<Vector3> {
            const auto state = weak.lock();
            if (!state)
                return {};
            const auto object = state->world.object(id);
            if (!object)
                return {};
            const auto scale = state->clamp_prim_scale(requested);
            if (scale != object->scale && !state->world.patch_transform(id, {}, {}, scale))
                return {};
            return scale;
        };
        host.set_object_text = [weak](Uuid id, bool description, const std::string& value) {
            if (auto state = weak.lock())
                return state->world.update_object_text(id, description, value);
            return false;
        };
        host.set_sound_queueing = [weak](Uuid id, bool queue) {
            if (const auto state = weak.lock(); state && state->policies.sound_max_distance)
                state->world.set_sound_queueing(id, queue);
        };
        host.set_sound_radius = [weak](Uuid id, float radius) {
            if (const auto state = weak.lock())
                state->world.set_sound_radius(id, radius);
        };
        host.adjust_sound_gain = [weak](Uuid id, float gain) {
            const auto state = weak.lock();
            if (!state || !state->viewers)
                return;
            auto packet = lludp::make_packet("AttachedSoundGainChange");
            packet.block("DataBlock")["ObjectID"] = id;
            packet.block("DataBlock")["Gain"] = static_cast<double>(gain);
            for (const auto& viewer : state->viewers->agents())
                if (viewer.root)
                    state->viewers->transport().enqueue(viewer.circuit->agent_id, packet,
                                                        ThrottleOutPacketType::Task);
        };
        const auto resolve_sound = [inventory_item](Uuid id, const std::string& name,
                                                    std::optional<std::int32_t> type = 1) {
            Uuid sound;
            if (!framework::try_parse_sdk_uuid(name, sound))
                if (const auto item = inventory_item(id, name, type))
                    sound = item->asset_id;
            return sound;
        };
        host.set_pay_prices = [weak](Uuid id, const std::array<std::int32_t, 5>& prices) {
            if (const auto state = weak.lock())
                state->world.set_pay_prices(id, prices);
        };
        host.set_collision_sound = [weak, resolve_sound](Uuid id, const std::string& name, float volume) {
            if (const auto state = weak.lock()) {
                const auto asset = resolve_sound(id, name);
                const auto object = state->world.object(id);
                if (object)
                    state->world.set_collision_sound(
                        id, asset.is_zero() ? Uuid("ffffffff-ffff-ffff-ffff-ffffffffffff") : asset,
                        !name.empty() && asset.is_zero() ? object->collision_sound_volume : volume);
            }
        };
        host.play_sound = [weak, resolve_sound](Uuid id, const std::string& name, double gain,
                                                std::uint8_t flags, bool triggered,
                                                const std::optional<script::LslHost::SoundBounds>& bounds) {
            const auto state = weak.lock();
            if (!state || !state->policies.sound_max_distance)
                return;
            const auto sound =
                resolve_sound(id, name, bounds ? std::nullopt : std::optional<std::int32_t>{1});
            if (sound.is_zero())
                return;
            const auto object =
                triggered ? state->world.object(id)
                          : state->world.prepare_attached_sound(id, *state->policies.sound_max_distance);
            if (!object)
                return;
            auto packet = lludp::make_packet(triggered ? "SoundTrigger" : "AttachedSound");
            auto& block = packet.block(triggered ? "SoundData" : "DataBlock");
            block["ObjectID"] = id;
            block["OwnerID"] = object->owner;
            block["SoundID"] = sound;
            block["Gain"] = std::clamp(gain, 0., 1.);
            const auto position = state->script_world_position(*object);
            const auto radius =
                object->sound_radius == 0 ? *state->policies.sound_max_distance : object->sound_radius;
            if (triggered) {
                const auto root = object->parent_id && !object->is_attachment
                                      ? state->world.object(object->parent_id)
                                      : object;
                block["ParentID"] = root ? root->id : id;
                block["Handle"] = state->descriptor.handle();
                block["Position"] = std::vector<double>{position.x, position.y, position.z};
            } else
                block["Flags"] = std::uint64_t(flags | (object->sound_queueing ? 16 : 0));
            if (object->is_attachment && !bounds) {
                const auto wearer = state->attachment_wearer(*object);
                if (!wearer)
                    return;
                if (!triggered && object->shape.state >= 31 && object->shape.state <= 38) {
                    state->viewers->transport().enqueue(wearer->circuit->agent_id, packet,
                                                        ThrottleOutPacketType::Task);
                    return;
                }
                if (!state->avatar_sound_allowed(*wearer))
                    return;
            }
            if (triggered && !bounds && object->sound_radius == 0)
                state->world.set_sound_radius(id, radius);
            for (const auto& viewer : state->viewers->agents()) {
                if (!viewer.root)
                    continue;
                const auto x = viewer.position.x - position.x, y = viewer.position.y - position.y,
                           z = viewer.position.z - position.z;
                const auto distance_squared = x * x + y * y + z * z;
                if (bounds) {
                    const auto& [min, max] = *bounds;
                    const auto& p = viewer.position;
                    const auto distance = std::sqrt(static_cast<double>(distance_squared));
                    const auto limit = *state->policies.sound_max_distance;
                    if (distance > limit || !(p.x >= min.x && p.y >= min.y && p.z >= min.z && p.x <= max.x &&
                                              p.y <= max.y && p.z <= max.z))
                        continue;
                    block["Gain"] = gain * ((limit - distance) / limit);
                } else if (triggered && distance_squared > radius * radius)
                    continue;
                state->viewers->transport().enqueue(viewer.circuit->agent_id, packet,
                                                    ThrottleOutPacketType::Task);
            }
        };
        host.preload_sound = [weak, resolve_sound](Uuid id, const std::string& name) {
            const auto state = weak.lock();
            if (!state || !state->policies.sound_max_distance)
                return false;
            const auto sound = resolve_sound(id, name);
            const auto object = state->world.object(id);
            if (sound.is_zero() || !object)
                return false;
            const auto radius =
                object->sound_radius == 0 ? *state->policies.sound_max_distance : object->sound_radius;
            if (object->sound_radius == 0)
                state->world.set_sound_radius(id, radius);
            const auto position = state->script_world_position(*object);
            auto packet = lludp::make_packet("PreloadSound");
            packet.zerocoded = true;
            auto block = lludp::make_block(packet.spec->blocks.front());
            block["ObjectID"] = id;
            block["OwnerID"] = object->owner;
            block["SoundID"] = sound;
            packet.blocks["DataBlock"].push_back(std::move(block));
            for (const auto& viewer : state->viewers->agents()) {
                const auto x = viewer.position.x - position.x, y = viewer.position.y - position.y,
                           z = viewer.position.z - position.z;
                if (viewer.root && x * x + y * y + z * z < 4.f * radius * radius)
                    state->viewers->transport().enqueue(viewer.circuit->agent_id, packet,
                                                        ThrottleOutPacketType::Task);
            }
            return true;
        };
        host.loop_sound = [weak, resolve_sound](Uuid id, const std::optional<std::string>& name, double gain,
                                                std::uint8_t sync_flags) {
            const auto state = weak.lock();
            if (!state || !state->policies.sound_max_distance)
                return;
            Uuid sound;
            if (name) {
                sound = resolve_sound(id, *name);
                if (sound.is_zero())
                    return;
            }
            state->world.set_looped_sound(id, sound, static_cast<float>(gain),
                                          *state->policies.sound_max_distance, sync_flags);
        };
        host.set_floating_text = [weak](Uuid id, std::string text, std::uint32_t argb) {
            if (auto state = weak.lock())
                state->world.set_floating_text(id, std::move(text), argb);
        };
        host.set_interaction_label = [weak](Uuid id, bool sit, std::string text) {
            if (auto state = weak.lock())
                state->world.set_interaction_label(id, sit, std::move(text));
        };
        host.clock_ticks = []() {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                           .count() /
                       100 +
                   621355968000000000LL;
        };
        return std::make_unique<script::LslInstance>(std::move(program), context, host,
                                                     start_running);
    }
    bool can_edit_inventory_asset(Uuid key, Uuid object, Uuid agent, bool script) {
        if (script && object.is_zero() && policies.can_edit_user_inventory_script)
            return policies.can_edit_user_inventory_script(key, agent);
        if (!script && policies.can_edit_inventory_notecard)
            return policies.can_edit_inventory_notecard(key, object, agent);
        if (policies.bypass_inventory_permissions)
            return true;
        if (object.is_zero()) {
            auto item =
                policies.inventory_service ? policies.inventory_service->item(agent, key) : std::nullopt;
            return item && item->owner == agent &&
                   (static_cast<std::uint32_t>(item->current_permissions) & 0xc000u) == 0xc000u;
        }
        if (!can_edit_inventory(object, agent))
            return false;
        auto root = world.object(object);
        if (!root || (root->owner == agent && !(root->owner_mask & 0x4000u)))
            return false;
        auto part = task_parts.find(object);
        if (part == task_parts.end() || !part->second.inventory)
            return false;
        auto item = part->second.inventory->find(key);
        return item && item->owner_id == agent && (item->current_permissions & 0xc000u) == 0xc000u;
    }
    std::shared_ptr<SceneAssetClient> asset_client(const ViewerAgent& actor) {
        auto client = std::make_shared<SceneAssetClient>();
        client->agent = actor.circuit->agent_id;
        client->agent_alert = [this, actor](const auto& text, bool modal) {
            if (inventory_active(actor))
                viewers->transport().enqueue_if_session(actor.circuit->agent_id, actor.circuit->session_id,
                                                        agent_alert(actor.circuit->agent_id, text, modal),
                                                        ThrottleOutPacketType::Task);
        };
        client->alert = [this, actor](const auto& text) {
            if (!inventory_active(actor))
                return;
            auto packet = lludp::make_packet("AlertMessage");
            std::vector<std::uint8_t> bytes(text.begin(), text.end());
            bytes.push_back(0);
            packet.block("AlertData")["Message"] = std::move(bytes);
            viewers->transport().enqueue_if_session(actor.circuit->agent_id, actor.circuit->session_id,
                                                    std::move(packet), ThrottleOutPacketType::Task);
        };
        return client;
    }
    InventoryAssetUpdateBackend inventory_asset_backend(std::shared_ptr<SceneAssetClient> client) {
        InventoryAssetUpdateBackend b;
        b.item = [this](auto agent, auto key) {
            auto item = policies.inventory_service->item(agent, key);
            return item ? std::make_shared<InventoryItem>(*item) : nullptr;
        };
        b.edit_notecard = [this](auto item, auto object, auto agent) {
            return can_edit_inventory_asset(item, object, agent, false);
        };
        b.edit_script = [this](auto item, auto object, auto agent) {
            return can_edit_inventory_asset(item, object, agent, true);
        };
        b.asset_permissions = [this](auto agent, auto asset) {
            return policies.inventory_service->asset_permissions(agent, asset);
        };
        b.create_asset = create_inventory_asset;
        b.store = [this](auto asset) {
            if (!policies.store_inventory_asset)
                throw std::runtime_error("Inventory asset update requires a result-returning asset service");
            return policies.store_inventory_asset(std::move(asset));
        };
        b.update = [this](auto item) { return policies.inventory_service->update_item(*item); };
        b.agent_alert = [client](auto, const auto& text, bool modal) {
            if (!client)
                throw std::runtime_error("Null inventory client");
            client->agent_alert(text, modal);
        };
        b.alert = [client](auto, const auto& text) {
            if (!client)
                throw std::runtime_error("Null inventory client");
            client->alert(text);
        };
        b.error = b.warn = [this](const auto& text) { error(text); };
        return b;
    }
    SceneInventoryAssetBackend scene_asset_backend() {
        SceneInventoryAssetBackend b;
        b.presence = [this](auto agent) -> std::optional<std::shared_ptr<SceneAssetPresence>> {
            auto actor = viewers->agent(agent);
            if (!actor || actor->logout_requested)
                return {};
            auto presence = std::make_shared<SceneAssetPresence>();
            presence->client = [this, actor = *actor] { return asset_client(actor); };
            return presence;
        };
        b.inventory_access = [this]() -> std::optional<SceneInventoryAccessUpdate> {
            if (!policies.inventory_service)
                return {};
            return [this](auto client, auto item, auto data) {
                if (!client)
                    throw std::runtime_error("Null inventory controlling client");
                return InventoryAssetUpdateController(inventory_asset_backend(client))
                    .caps_update(client->agent, item, std::move(data));
            };
        };
        b.part = [this](auto object) -> std::shared_ptr<SceneAssetPart> {
            if (!world.object(object))
                return {};
            auto adapter = std::make_shared<SceneAssetPart>();
            adapter->group_deleted = [this, object] { return !world.object(object); };
            adapter->item = [this, object](auto key) { return task_inventory(object).find(key); };
            adapter->update = [this, object](auto item) { task_inventory(object).update(std::move(item)); };
            return adapter;
        };
        b.edit_inventory = [this](auto object, auto agent) { return can_edit_inventory(object, agent); };
        b.edit_notecard = [this](auto item, auto object, auto agent) {
            return can_edit_inventory_asset(item, object, agent, false);
        };
        b.asset_permissions = [this](auto agent, auto asset) {
            if (!policies.inventory_service)
                throw std::runtime_error("Inventory asset permissions require an InventoryService");
            return policies.inventory_service->asset_permissions(agent, asset);
        };
        b.create_asset = create_scene_inventory_asset;
        b.store = [this](auto asset) {
            if (!policies.store_asset)
                throw std::runtime_error("Task asset update requires an asset service");
            policies.store_asset(std::move(asset));
        };
        b.error = [this](const auto& text) { error(text); };
        return b;
    }
    ScriptTaskUpdateBackend script_update_backend() {
        ScriptTaskUpdateBackend backend;
        backend.can_edit_script = [this](Uuid item, Uuid object, Uuid agent) {
            return can_edit_script(item, object, agent);
        };
        backend.find_client = [this](Uuid agent) -> std::optional<ScriptTaskUpdateClient> {
            auto client = viewers->agent(agent);
            if (!client || client->logout_requested)
                return {};
            return ScriptTaskUpdateClient{agent, [this, client = *client](const auto& text, bool) {
                                              if (inventory_active(client))
                                                  alert(client.circuit->agent_id, text);
                                          }};
        };
        backend.store = [this](auto asset) {
            if (!policies.store_asset)
                throw std::runtime_error("Script update requires an asset service");
            policies.store_asset(std::move(asset));
        };
        backend.find_part = [this](Uuid object) -> std::optional<ScriptTaskUpdatePart> {
            auto part = world.object(object);
            if (!part)
                return {};
            ScriptTaskUpdatePart adapter;
            adapter.local_id = part->local_id;
            adapter.get_item = [this, object](std::uint32_t local,
                                              Uuid item) -> std::optional<ScriptTaskUpdateItem> {
                auto part = world.object(object);
                if (!part || part->local_id != local)
                    return {};
                auto p = task_parts.find(object);
                if (p == task_parts.end() || !p->second.inventory)
                    return {};
                auto row = p->second.inventory->find(item);
                if (!row)
                    return {};
                ScriptTaskUpdateItem result;
                result.id = item;
                result.name = row->name.value_or("");
                result.description = row->description.value_or("");
                result.set_running = [row](bool value) { row->script_running = value; };
                result.set_asset = [this, row](Uuid asset) {
                    row->asset_id = asset;
                    auto it = scripts.find(row->item_id);
                    if (it != scripts.end())
                        it->second.item.asset_id = asset;
                };
                result.asset = [row] { return row->asset_id; };
                return result;
            };
            adapter.remove_instance = [this, object](Uuid item, bool) {
                auto p = task_parts.find(object);
                if (p == task_parts.end() || !p->second.inventory || !p->second.inventory->find(item))
                    return;
                script_events(object).remove(item);
                auto it = scripts.find(item);
                if (it != scripts.end())
                    replace_script_instance(it->second);
                refresh_script_flags(object);
            };
            adapter.update_inventory = [this, object](const auto& item) {
                auto& inventory = task_inventory(object);
                if (auto row = inventory.find(item.id))
                    inventory.update(row);
            };
            // This profile evaluates represented owner permissions directly; no aggregate
            // permission cache exists to invalidate yet.
            adapter.invalidate_permissions = [] {};
            adapter.send_properties = [this, object](const auto& client) {
                send_part_properties(object, client.agent);
            };
            adapter.compile = [this, object](Uuid item, int, bool, const std::string&,
                                             int) -> ScriptTaskErrors { return compile_task(object, item); };
            adapter.resume_scripts = [] {};
            return adapter;
        };
        backend.updated = [this](auto agent, auto item, auto object, bool running, auto asset) {
            if (policies.script_updated)
                policies.script_updated(agent, item, object, running, asset);
        };
        backend.log = [this](const auto& text) { error(text); };
        backend.default_engine = "NativeLSL";
        return backend;
    }
    std::vector<std::string> compile_task_item(Uuid object,
                                               std::shared_ptr<framework::TaskInventoryItem> input,
                                               std::optional<std::string_view> fallback = {},
                                               int state_source = 1) {
        auto part = world.object(object);
        if (!part)
            return {"Task part not found"};
        if (input) {
            const auto entry = scripts.find(input->item_id);
            if (entry != scripts.end())
                entry->second.deferred_default_rez = false;
        }
        std::optional<std::vector<std::string>> diagnostics;
        ScriptInstanceCreateBackend backend;
        backend.can_run = [this, part](auto item) {
            return item && (!policies.can_run_script || policies.can_run_script(*item, *part));
        };
        backend.mark_scripted = [this, object] { mark_scripted(object); };
        backend.scripts_disabled = [this] { return policies.scripts_disabled; };
        backend.trust_binaries = [] { return false; };
        backend.has_saved_state = [] { return false; };
        backend.find_item =
            [this, object](Uuid item) -> std::optional<std::shared_ptr<framework::TaskInventoryItem>> {
            auto p = task_parts.find(object);
            if (p == task_parts.end() || !p->second.inventory)
                return {};
            auto task = p->second.inventory->find(item);
            if (task)
                return task;
            return {};
        };
        backend.load_asset = [this, input, fallback](Uuid id) -> std::shared_ptr<framework::AssetBase> {
            if (policies.load_asset)
                return policies.load_asset(id);
            if (!fallback || id != input->asset_id)
                return {};
            auto asset = std::make_shared<framework::AssetBase>(id, input->name, std::int8_t(10),
                                                                input->creator_id.str());
            asset->data = std::make_shared<std::vector<std::uint8_t>>(fallback->begin(), fallback->end());
            return asset;
        };
        backend.local_id = [part] { return part->local_id; };
        backend.trigger_rez = [this, object, &diagnostics](std::uint32_t, Uuid item,
                                                           const std::string& source, int, bool,
                                                           const std::string&, int) {
            auto part = world.object(object);
            auto task = task_inventory(object).find(item);
            if (!part || !task)
                throw std::runtime_error("Script item disappeared during compilation");
            auto& entry = scripts[item];
            entry.item = service_item(*task);
            entry.task = task;
            auto compiled = script::compile_lsl(source);
            diagnostics = std::vector<std::string>{};
            if (!compiled) {
                for (const auto& d : compiled.diagnostics)
                    diagnostics->push_back("(" + std::to_string(d.line) + "," + std::to_string(d.column) +
                                           "): " + d.message);
                replace_script_instance(entry);
            } else {
                replace_script_instance(
                    entry, create_script_instance(compiled.program, entry.item, *part, task->script_running));
                script_events(object).set(item, entry.instance->event_mask());
            }
            refresh_script_flags(object);
        };
        backend.store_errors = [&diagnostics](Uuid, std::optional<std::vector<std::string>> errors) {
            if (errors)
                diagnostics = std::move(*errors);
            else if (!diagnostics)
                throw std::runtime_error("Script compiler did not publish diagnostics");
        };
        backend.active_scripts = [](int) {};
        backend.schedule_full = [this, object] { refresh_script_flags(object); };
        backend.error = [this](const auto& text) { error(text); };
        ScriptInstanceCreateController controller(std::move(backend));
        controller.create(std::move(input), 0, false, "NativeLSL", state_source);
        if (!diagnostics)
            return {"timedout waiting for errors"};
        return std::move(*diagnostics);
    }
    std::vector<std::string> compile_task(Uuid object, Uuid item,
                                          std::optional<std::string_view> fallback = {},
                                          int state_source = 1) {
        auto p = task_parts.find(object);
        if (p == task_parts.end() || !p->second.inventory)
            return {"Task part not found"};
        auto task = p->second.inventory->find(item);
        if (!task)
            return {"TaskItem ID " + item.str() + " could not be found"};
        return compile_task_item(object, std::move(task), fallback, state_source);
    }
    InventoryScriptRezBackend inventory_script_backend() {
        InventoryScriptRezBackend backend;
        backend.unix_time = [] {
            return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                                  std::chrono::system_clock::now().time_since_epoch())
                                                  .count());
        };
        backend.inventory_item = [this](Uuid agent, Uuid item) {
            auto value =
                policies.inventory ? policies.inventory->item(agent, item) : std::optional<InventoryItem>{};
            if (value)
                value->creator_id = value->creator_id.substr(0, value->creator_id.find(';'));
            return value;
        };
        backend.library_item = [this](Uuid item) {
            auto value = policies.library_inventory_item ? policies.library_inventory_item(item)
                                                         : std::optional<InventoryItem>{};
            if (value)
                value->creator_id = value->creator_id.substr(0, value->creator_id.find(';'));
            return value;
        };
        backend.scene_part = [this](std::uint32_t local) -> std::optional<InventoryScriptPart> {
            auto part = world.object(local);
            if (part)
                return InventoryScriptPart{part->id, part->owner};
            return {};
        };
        backend.group_part = [this](Uuid context, std::uint32_t local) -> std::optional<InventoryScriptPart> {
            auto part = world.object(local);
            if (part && part->id == context)
                return InventoryScriptPart{part->id, part->owner};
            return {};
        };
        backend.can_edit_inventory = [this](Uuid object, Uuid agent) {
            return can_edit_inventory(object, agent);
        };
        backend.propagate_permissions = [this] { return policies.propagate_permissions; };
        backend.root_flags = [this](const auto& part) {
            auto object = world.object(part.id);
            return object ? inventory_drop_root(*object).flags : 0u;
        };
        backend.insert_task = [this](const auto& part, auto transferred, bool drop) {
            std::lock_guard lock(task_data_mutex);
            auto task = task_model(transferred->item, part.id);
            task->script_running = transferred->running;
            if (task_inventory(part.id).add(task, drop))
                scripts.emplace(task->item_id, RunningScript{service_item(*task), {}, task});
        };
        backend.invalidate_deep_permissions = [](const auto&) {};
        backend.compile = [this](const auto& part, Uuid item, int, bool, const std::string&, int state) {
            auto errors = compile_task(part.id, item, {}, state);
            for (const auto& text : errors)
                error("Native script " + item.str() + ": " + text);
        };
        backend.new_script = [this](Uuid agent, const auto& part, Uuid item) {
            if (policies.new_script)
                policies.new_script(agent, part.id, item);
        };
        backend.resume = [](const auto&) {};
        backend.properties = [this](const auto& part, Uuid agent) { send_part_properties(part.id, agent); };
        backend.default_engine = "NativeLSL";
        backend.log = [this](const auto& text) { error(text); };
        return backend;
    }
    void run_inventory_scripts() {
        std::vector<InventoryScriptCommand> commands;
        {
            std::lock_guard lock(commands_mutex);
            commands.swap(inventory_script_commands);
        }
        for (const auto& command : commands)
            if (inventory_active(command.actor))
                try {
                    std::lock_guard lock(task_data_mutex);
                    InventoryScriptRezController controller(inventory_script_backend());
                    InventoryItem wire;
                    wire.id = command.item;
                    controller.rez(command.actor.circuit->agent_id, wire, {}, command.local);
                } catch (const std::exception& e) {
                    error(std::string("Inventory script rez: ") + e.what());
                }
    }
    void run_script_updates() {
        std::deque<std::shared_ptr<ScriptUpdateJob>> jobs;
        std::deque<std::shared_ptr<InventoryAssetJob>> assets;
        {
            std::lock_guard lock(commands_mutex);
            jobs.swap(script_update_jobs);
            assets.swap(inventory_asset_jobs);
        }
        for (auto& job : jobs)
            try {
                std::lock_guard lock(task_data_mutex);
                ScriptTaskUpdateController controller(script_update_backend());
                auto result = controller.update(job->agent, job->item, job->object, job->running, job->data);
                job->result.set_value(result.value());
            } catch (...) {
                job->result.set_exception(std::current_exception());
            }
        for (auto& job : assets)
            try {
                std::lock_guard lock(task_data_mutex);
                job->result.set_value(SceneInventoryAssetController(scene_asset_backend())
                                          .update(job->agent, job->item, job->object, job->data));
            } catch (...) {
                job->result.set_exception(std::current_exception());
            }
    }
    void quiesce_updates() {
        std::deque<std::shared_ptr<ScriptUpdateJob>> jobs;
        std::deque<std::shared_ptr<InventoryAssetJob>> assets;
        {
            std::lock_guard lock(commands_mutex);
            script_updates_open = false;
            jobs.swap(script_update_jobs);
            assets.swap(inventory_asset_jobs);
        }
        for (auto& job : jobs)
            job->result.set_exception(
                std::make_exception_ptr(std::runtime_error("Region script updates stopped")));
        for (auto& job : assets)
            job->result.set_exception(
                std::make_exception_ptr(std::runtime_error("Region inventory asset updates stopped")));
    }
    bool deliver_instant_message(const RegionInstantMessage& message,
                                 std::optional<Uuid> expected_session = {}) {
        if (!running || !viewers)
            return false;
        const auto viewer = viewers->agent(message.recipient);
        if (!viewer || !viewer->root || viewer->logout_requested)
            return false;
        if (expected_session && viewer->circuit->session_id != *expected_session)
            return false;
        if (policies.can_instant_message && !policies.can_instant_message(message.sender, message.recipient))
            return false;
        auto packet = lludp::make_packet("ImprovedInstantMessage");
        auto& sender = packet.block("AgentData");
        sender["AgentID"] = message.sender;
        sender["SessionID"] = Uuid{};
        auto& data = packet.block("MessageBlock");
        data["FromGroup"] = message.from_group;
        data["ToAgentID"] = message.recipient;
        data["ParentEstateID"] = std::uint64_t(message.estate);
        data["RegionID"] = message.region;
        data["Position"] = std::vector<double>{message.position.x, message.position.y, message.position.z};
        data["Offline"] = std::uint64_t(message.offline);
        data["Dialog"] = std::uint64_t(message.dialog);
        auto session = message.session;
        if (session.is_zero()) {
            auto bytes = message.sender.bytes();
            for (std::size_t i = 0; i < bytes.size(); ++i)
                bytes[i] ^= message.recipient.bytes()[i];
            session = Uuid::from_bytes(bytes);
        }
        data["ID"] = session;
        data["Timestamp"] = std::uint64_t(message.timestamp);
        data["FromAgentName"] = profile_string_bytes(message.sender_name, 255);
        data["Message"] = profile_string_bytes(message.message, 1024);
        data["BinaryBucket"] = message.bucket;
        return viewers->transport().enqueue_if_session(message.recipient, viewer->circuit->session_id,
                                                       std::move(packet), ThrottleOutPacketType::Task);
    }
    bool notify_friend_rights(Uuid recipient, Uuid principal, std::int32_t previous, std::int32_t granted) {
        if (!running || !viewers)
            return false;
        const auto viewer = viewers->agent(recipient);
        if (!viewer || !viewer->root || viewer->logout_requested)
            return false;
        if (granted == previous)
            return true;
        if ((granted ^ previous) & 1) {
            auto status = lludp::make_packet(granted & 1 ? "OnlineNotification" : "OfflineNotification");
            auto agent = lludp::make_block(status.spec->blocks[0]);
            agent["AgentID"] = principal;
            status.blocks["AgentBlock"].push_back(std::move(agent));
            viewers->transport().enqueue_if_session(recipient, viewer->circuit->session_id, std::move(status),
                                                    ThrottleOutPacketType::Task);
        }
        auto reply = lludp::make_packet("ChangeUserRights");
        reply.block("AgentData")["AgentID"] = principal;
        auto block = lludp::make_block(reply.spec->blocks[1]);
        block["AgentRelated"] = recipient;
        block["RelatedRights"] = std::int64_t(granted);
        reply.blocks["Rights"].push_back(std::move(block));
        return viewers->transport().enqueue_if_session(recipient, viewer->circuit->session_id,
                                                       std::move(reply), ThrottleOutPacketType::Task);
    }
    bool notify_friend_termination(Uuid recipient, Uuid former_friend) {
        if (!running || !viewers)
            return false;
        const auto viewer = viewers->agent(recipient);
        if (!viewer || !viewer->root)
            return false;
        auto packet = lludp::make_packet("TerminateFriendship");
        packet.block("AgentData")["AgentID"] = recipient;
        packet.block("AgentData")["SessionID"] = viewer->circuit->session_id;
        packet.block("ExBlock")["OtherID"] = former_friend;
        return viewers->transport().enqueue_if_session(recipient, viewer->circuit->session_id,
                                                       std::move(packet), ThrottleOutPacketType::Task);
    }
    void detach_attachment(const ViewerAgent& viewer, const SceneObject& object) {
        std::lock_guard lock(appearance_mutex);
        if (!object.is_attachment || object.parent_id != viewer.local_id)
            return;
        const auto item = object.from_inventory_item;
        if (!item.is_zero()) {
            const auto current = viewers->agent(viewer.circuit->agent_id);
            if (!current || current->circuit->session_id != viewer.circuit->session_id)
                return;
            const auto appearance = current->appearance();
            if (!appearance)
                return;
            auto detached = std::make_shared<AvatarAppearance>(*appearance);
            const bool found = detached->detach_attachment(item);
            if (!viewer.circuit->verified_foreign_login) {
                if (object.changed || !object.inventory_empty || !policies.save_appearance)
                    return;
                if (!found || !policies.save_appearance(viewer.circuit->agent_id, *detached))
                    return;
            }
            if (found && !viewers->update_appearance(viewer.circuit->agent_id, viewer.circuit->session_id,
                                                     std::move(detached)))
                return;
        }
        world.remove(object.id);
    }
    void send_estate_details(const ViewerAgent& viewer, Uuid invoice, unsigned lists = 15,
                             bool include_details = true) {
        const auto& circuit = *viewer.circuit;
        const auto make_reply = [&](std::u16string_view method) {
            auto reply = lludp::make_packet("EstateOwnerMessage");
            reply.block("AgentData")["TransactionID"] = Uuid::random();
            reply.block("MethodData")["Invoice"] = invoice;
            reply.block("MethodData")["Method"] = profile_string_bytes(method, 255);
            return reply;
        };
        const auto add_text = [](lludp::Packet& packet, const std::string& value) {
            packet.blocks["ParamList"].push_back(
                {{"Parameter", profile_string_bytes(framework::utf16(value), 255)}});
        };
        const auto send = [&](lludp::Packet reply) {
            viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id, std::move(reply),
                                                    ThrottleOutPacketType::Task);
        };
        auto details = make_reply(u"estateupdateinfo");
        Uuid covenant;
        std::uint32_t timestamp;
        {
            std::lock_guard lock(estate_covenant_mutex);
            covenant = estate_covenant;
            timestamp = estate_covenant_timestamp;
        }
        const auto flags = policies.estate_menu_flags | effective_estate_handshake_flags() |
                           policies.estate_detail_only_flags;
        for (const auto& value :
             {policies.estate_name, descriptor.estate_owner.str(), std::to_string(policies.estate_id),
              std::to_string(flags), std::string("0"), std::to_string(policies.parent_estate_id),
              covenant.str(), std::to_string(timestamp), std::string("1"), policies.estate_abuse_email})
            add_text(details, value);
        if (include_details)
            send(std::move(details));
        const auto send_list = [&](int code, const std::vector<Uuid>& entries) {
            std::size_t first = 0;
            do {
                const auto count = std::min<std::size_t>(63, entries.size() - first);
                auto reply = make_reply(u"setaccess");
                reply.reliable = true;
                reply.block("AgentData")["AgentID"] = circuit.agent_id;
                reply.block("AgentData")["SessionID"] = circuit.session_id;
                add_text(reply, std::to_string(policies.estate_id));
                add_text(reply, std::to_string(code));
                for (const int flag : {1, 2, 4, 8})
                    add_text(reply, std::to_string(flag == code ? count : 0));
                for (std::size_t i = 0; i < count; ++i) {
                    const auto& bytes = entries[first + i].bytes();
                    reply.blocks["ParamList"].push_back(
                        {{"Parameter", std::vector<std::uint8_t>(bytes.begin(), bytes.end())}});
                }
                send(std::move(reply));
                first += count;
            } while (first < entries.size());
        };
        std::lock_guard lock(estate_access_mutex);
        if (lists & 8)
            send_list(8, policies.estate_managers);
        if (lists & 1)
            send_list(1, policies.estate_users);
        if (lists & 2)
            send_list(2, policies.estate_groups);
        if (lists & 4)
            send_list(4, policies.estate_bans);
    }
    void edit_estate_access(const ViewerAgent& viewer, Uuid invoice, int flags, Uuid target) {
        if (target.is_zero())
            return;
        const auto actor = viewer.circuit->agent_id;
        const bool can_edit_managers = policies.bypass_estate_permissions ||
                                       actor == descriptor.estate_owner ||
                                       moderation_level(actor, &viewer) >= 200;
        const auto target_viewer = viewers->agent(target);
        const bool protected_target =
            target == descriptor.estate_owner ||
            moderation_level(target, target_viewer ? &*target_viewer : nullptr) >= 200;
        bool newly_banned = false;
        unsigned replies = 0;
        try {
            std::lock_guard lock(estate_access_mutex);
            auto users = policies.estate_users, groups = policies.estate_groups, bans = policies.estate_bans;
            auto managers = policies.estate_managers;
            const auto contains = [&](const auto& list) {
                return std::find(list.begin(), list.end(), target) != list.end();
            };
            const auto add = [&](auto& list, std::size_t limit, const char* full) {
                if (list.size() >= limit) {
                    alert(viewer.circuit->agent_id, full);
                    return false;
                }
                if (!contains(list))
                    list.push_back(target);
                return true;
            };
            if ((flags & 4) && add(users, 500, "Estate Allowed users list is full")) {
                std::erase(bans, target);
                replies |= 5;
            }
            if (flags & 8) {
                std::erase(users, target);
                replies |= 5;
            }
            if ((flags & 16) && add(groups, 63, "Estate Allowed groups list is full"))
                replies |= 2;
            if (flags & 32) {
                std::erase(groups, target);
                replies |= 2;
            }
            if (flags & 64) {
                if (protected_target)
                    alert(actor, "Cannot ban a Administrator");
                else if (bans.size() >= 500)
                    alert(actor, "Estate Ban list is full");
                else {
                    if (contains(bans))
                        alert(actor, "User is already on the region ban list");
                    else {
                        bans.push_back(target);
                        std::erase(users, target);
                    }
                    replies |= 5;
                }
            }
            if (flags & 128) {
                if (!std::erase(bans, target))
                    alert(actor, "User is not on the region ban list");
                replies |= 5;
            }
            if (flags & (256 | 512)) {
                if (can_edit_managers) {
                    if ((flags & 256) && add(managers, 20, "Estate Managers list is full"))
                        replies |= 8;
                    if (flags & 512) {
                        std::erase(managers, target);
                        replies |= 8;
                    }
                } else
                    alert(actor, "Method EstateAccess Failed, you don't have permissions");
            }
            const auto changed = [&](const auto& before, const auto& after) -> std::optional<bool> {
                const bool present = contains(after);
                if (present == contains(before))
                    return {};
                return present;
            };
            const auto user = changed(policies.estate_users, users),
                       group = changed(policies.estate_groups, groups);
            const auto manager = changed(policies.estate_managers, managers);
            const auto ban = changed(policies.estate_bans, bans);
            if ((user || group || manager || ban) && policies.persist_estate_access)
                policies.persist_estate_access(target, actor, user, group, manager, ban);
            newly_banned = ban.value_or(false);
            policies.estate_users.swap(users);
            policies.estate_groups.swap(groups);
            policies.estate_bans.swap(bans);
            policies.estate_managers.swap(managers);
        } catch (const std::exception& e) {
            error(std::string("Estate access update failed: ") + e.what());
            return;
        }
        if (newly_banned && target_viewer && target_viewer->root) {
            bool transferred = false;
            try {
                transferred = send_viewer_home(*target_viewer, true);
            } catch (const std::exception& e) {
                error(std::string("Estate ban teleport failed: ") + e.what());
            }
            if (!transferred)
                queue_kick(
                    *target_viewer,
                    u"Your access to the region was revoked and TP home failed - you have been logged out.");
        }
        if (flags & 1024)
            replies &= ~5u;
        if (replies)
            send_estate_details(viewer, invoice, replies, false);
    }
    void send_teleport_failure(const ViewerAgent& viewer, std::string reason) {
        auto failure = lludp::make_packet("TeleportFailed");
        auto& info = failure.block("Info");
        info["AgentID"] = viewer.circuit->agent_id;
        reason.resize(std::min<std::size_t>(reason.size(), 254));
        std::vector<std::uint8_t> bytes(reason.begin(), reason.end());
        bytes.push_back(0);
        info["Reason"] = std::move(bytes);
        viewers->transport().enqueue_if_session(viewer.circuit->agent_id, viewer.circuit->session_id,
                                                std::move(failure), ThrottleOutPacketType::Unknown);
    }
    bool send_viewer_home(const ViewerAgent& viewer, bool banned) {
        auto request = lludp::make_packet("TeleportLocationRequest");
        auto& info = request.block("Info");
        info["RequestedFlags"] = std::uint64_t{34};
        const auto reason = policies.resolve_landmark ? policies.resolve_landmark(viewer, Uuid{}, request)
                                                      : std::string("Your home region not found");
        if (!reason.empty()) {
            send_teleport_failure(viewer, reason);
            return false;
        }
        if (std::get<std::uint64_t>(info.at("RegionHandle")) == descriptor.handle()) {
            if (banned) {
                send_teleport_failure(viewer, "You have been banned from that region.");
                return false;
            }
            return viewers->teleport_local(viewer.circuit->agent_id, viewer.circuit->session_id, request);
        }
        return begin_remote_teleport(viewer, request, banned);
    }
    void estate_send_home(const ViewerAgent& viewer, bool from_peer, bool all) {
        const auto& circuit = *viewer.circuit;
        {
            std::lock_guard lock(teleport_mutex);
            if (std::any_of(pending_teleports.begin(), pending_teleports.end(),
                            [&](const auto& transfer) { return transfer.session == circuit.session_id; }))
                return;
        }
        if (from_peer || policies.estate_communications) {
            auto start = lludp::make_packet("TeleportStart");
            start.block("Info")["TeleportFlags"] = std::uint64_t{16};
            viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id, std::move(start),
                                                    ThrottleOutPacketType::Unknown);
        }
        bool sent = false;
        try {
            sent = send_viewer_home(viewer, false);
        } catch (const std::exception& failure) {
            error(std::string("Estate home teleport failed: ") + failure.what());
        }
        if (!sent && !from_peer)
            queue_kick(viewer, all
                ? u"You were teleported home by the region owner, but the TP failed - you have been logged out."
                : u"You were teleported home by the region owner, but the TP failed");
    }
    bool begin_remote_teleport(const ViewerAgent& a, const lludp::Packet& p, bool estate_ejection = false) {
        const auto requested = std::get<std::uint64_t>(p.block("Info").at("RegionHandle"));
        bool pending;
        {
            std::lock_guard lock(teleport_mutex);
            pending =
                std::any_of(pending_teleports.begin(), pending_teleports.end(),
                            [&](const auto& transfer) { return transfer.session == a.circuit->session_id; });
        }
        auto destination = !pending && policies.teleport_destination
                               ? policies.teleport_destination(requested)
                               : std::nullopt;
        std::string reason =
            destination ? "Region transfer unavailable" : "The region you tried to teleport to was not found";
        if (pending)
            reason = "Previous teleport process incomplete.  Please retry shortly.";
        else if (destination && policies.teleport_access) {
            const auto& position = std::get<std::vector<double>>(p.block("Info").at("Position"));
            const Vector3 target{float(position[0]), float(position[1]), float(position[2])};
            auto gatekeeper = *destination;
            LoginTransferContext context;
            auto denial = policies.teleport_access(*destination, a, target, context);
            if (!denial.empty())
                reason = std::move(denial);
            else if (policies.teleport_create) {
                const auto token = Uuid::random();
                const auto& request_info = p.block("Info");
                const auto& look = std::get<std::vector<double>>(request_info.at("LookAt"));
                const Vector3 look_at{float(look[0]), float(look[1]), float(look[2])};
                const auto flags = request_info.contains("RequestedFlags")
                                       ? std::get<std::uint64_t>(request_info.at("RequestedFlags"))
                                       : 16u;
                auto finish =
                    policies.teleport_create(descriptor, gatekeeper, *destination, a, target, look_at,
                                             context, token, static_cast<std::uint32_t>(flags), reason);
                if (finish) {
                    {
                        std::lock_guard lock(teleport_mutex);
                        pending_teleports.push_back(
                            {a.circuit->agent_id, a.circuit->session_id, *destination, token,
                             std::chrono::steady_clock::now() + std::chrono::seconds(30), false,
                             (gatekeeper.flags.value_or(0) & services::Hyperlink) != 0, estate_ejection});
                    }
                    auto start = lludp::make_packet("TeleportStart");
                    start.block("Info")["TeleportFlags"] = finish->block("Info").at("TeleportFlags");
                    viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id,
                                                            std::move(start), ThrottleOutPacketType::Unknown);
                    viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id,
                                                            std::move(*finish),
                                                            ThrottleOutPacketType::Unknown);
                    return true;
                }
            }
        }
        send_teleport_failure(a, std::move(reason));
        return false;
    }
    void finish_failed_estate_ejection(const PendingTeleport& transfer) {
        if (!transfer.estate_ejection)
            return;
        const auto viewer = viewers->agent(transfer.agent);
        std::string reason;
        if (viewer && viewer->circuit->session_id == transfer.session &&
            deny_estate_ban(transfer.agent, reason))
            queue_kick(
                *viewer,
                u"Your access to the region was revoked and TP home failed - you have been logged out.");
    }
    struct AssetUpload {
        struct TaskUpdate {
            Uuid object, item, previous_asset;
            int type;
        };
        std::shared_ptr<AssetBase> asset;
        std::uint64_t xfer = 0;
        std::uint32_t next = 0, size = 0;
        std::optional<std::uint64_t> final_packet;
        bool complete = false;
        bool success = false;
        bool inventory_attempted = false;
        std::optional<InventoryNewRequest> creation;
        std::optional<InventoryItem> update;
        std::optional<TaskUpdate> task_update;
    };
    // Upload and inventory commands share the scene poll thread.
    std::map<Uuid, std::map<Uuid, AssetUpload>> asset_uploads;
    std::vector<Uuid> closed_asset_upload_sessions;
    bool upload_references_allowed(const std::shared_ptr<AssetBase>& asset, Uuid agent, Uuid previous = {}) {
        if (asset->type() == 96) {
            AnimationSet animations(asset->data);
            return animations.validate([&](Uuid id) {
                    const auto mask = policies.inventory_service->asset_permissions(agent, id);
                    return (mask & 0xa000) == 0xa000 ? static_cast<std::uint32_t>(mask) : 0u;
                }) != 0;
        }
        if (asset->type() == static_cast<std::int8_t>(AssetType::Clothing) ||
            asset->type() == static_cast<std::int8_t>(AssetType::Bodypart)) {
            static const std::set<Uuid> defaults{
                Uuid("5748decc-f629-461c-9a36-a35a221fe21f"), Uuid("7ca39b4c-bd19-4699-aff7-f93fd03d3e7b"),
                Uuid("6522e74d-1660-4e7f-b601-6f48c1659a77"), Uuid("c228d1cf-4b5d-4ba8-84f4-899a0796aa97"),
                Uuid("8dcd4a48-2d37-4909-9f78-f7a9eb4ef903"), Uuid("00000000-0000-1111-9999-000000000010"),
                Uuid("00000000-0000-1111-9999-000000000011"), Uuid("00000000-0000-1111-9999-000000000012"),
                Uuid("3a367d1c-bef1-6d43-7595-e88c1e3aadb3"), Uuid("1578a2b1-5179-4b53-b618-fe00ca5a5594")};
            const auto read_textures = [](const auto& bytes, auto visit) {
                std::istringstream input(std::string(bytes.begin(), bytes.end()));
                std::string line;
                int textures = 0;
                while (std::getline(input, line)) {
                    if (line.starts_with("textures ")) {
                        std::istringstream count(line.substr(9));
                        count >> textures;
                    } else if (textures > 0) {
                        std::istringstream row(line);
                        int index;
                        std::string text;
                        Uuid texture;
                        if (!(row >> index >> text) || !try_parse_sdk_uuid(text, texture))
                            continue;
                        const auto result = visit(index, texture);
                        if (result < 0)
                            return false;
                        textures -= result;
                    }
                }
                return true;
            };
            std::map<int, Uuid> old_textures;
            if (!previous.is_zero() && policies.load_asset) {
                if (const auto old = policies.load_asset(previous); old && old->data)
                    read_textures(*old->data, [&](int index, Uuid texture) {
                        old_textures[index] = texture;
                        return 1;
                    });
            }
            return read_textures(*asset->data, [&](int index, Uuid texture) {
                const auto old = old_textures.find(index);
                if (texture.is_zero() || defaults.contains(texture) ||
                    (old != old_textures.end() && old->second == texture))
                    return 0;
                return (policies.inventory_service->asset_permissions(agent, texture) & 0xe000) == 0xe000 ? 1 : -1;
            });
        }
        return true;
    }
    bool create_from_asset_upload(const ViewerAgent& viewer, AssetUpload& upload) {
        const auto request = *upload.creation;
        upload.inventory_attempted = true;
        upload.creation.reset();
        auto& asset = upload.asset;
        if (request.asset_type != asset->type())
            return false;
        if (!upload_references_allowed(asset, request.agent)) {
            alert(request.agent, "Not enough permissions on asset(s) referenced by item '" +
                                    request.name.value_or("") + "', creation failed");
            return false;
        }
        asset->name(request.name);
        asset->description(request.description);
        if (!policies.store_inventory_asset(asset))
            return false;
        auto created = std::make_shared<TaskInventoryAgentItem>();
        auto& item = created->item;
        item.id = Uuid::random();
        item.owner = request.agent;
        item.creator_id = request.agent.str();
        item.asset_id = asset->full_id();
        item.name = request.name.value_or("");
        item.description = request.description.value_or("");
        item.asset_type = request.asset_type;
        item.inventory_type = request.inventory_type;
        item.parent = request.folder;
        item.flags = request.subtype;
        item.base_permissions = item.current_permissions = 0x9e000;
        item.next_permissions = std::bit_cast<std::int32_t>(request.next_permissions);
        item.creation_date = user_inventory_unix_time();
        if (item.inventory_type == 96)
            animation_set_enforce_permissions(created.get(), true);
        if (!agent_inventory_add().add(created)) {
            alert(request.agent, "Unable to create inventory item");
            return false;
        }
        viewers->transport().enqueue_if_session(
            request.agent, viewer.circuit->session_id,
            inventory_item_create(request.agent, item, request.transaction, request.callback),
            ThrottleOutPacketType::Asset);
        return true;
    }
    bool update_from_asset_upload(const ViewerAgent& viewer, Uuid transaction, AssetUpload& upload) {
        auto item = std::move(*upload.update);
        upload.update.reset();
        upload.inventory_attempted = true;
        const auto current = policies.inventory_service->item(item.owner, item.id);
        if (!current || current->owner != item.owner || current->asset_id != item.asset_id ||
            item.asset_type != upload.asset->type())
            return false;
        if (!upload_references_allowed(upload.asset, item.owner, item.asset_id)) {
            alert(item.owner, "Not enough permissions on asset(s) referenced by item '" + item.name +
                                  "', update failed");
            if (capability_runtime && capability_runtime->event_queue())
                capability_runtime->event_queue()->bulk_inventory(&*current, item.owner);
            return false;
        }
        upload.asset->name(item.name);
        upload.asset->description(item.description);
        if (!policies.store_inventory_asset(upload.asset))
            return false;
        item.asset_id = upload.asset->full_id();
        if (!policies.inventory_service->update_item(item))
            return false;
        viewers->transport().enqueue_if_session(
            item.owner, viewer.circuit->session_id, inventory_item_create(item.owner, item, transaction),
            ThrottleOutPacketType::Asset);
        if (policies.inventory_upload_complete)
            policies.inventory_upload_complete(item, 0);
        return true;
    }
    bool apply_task_asset_upload(const ViewerAgent& viewer, AssetUpload& upload, TaskInventoryItem& item) {
        const auto request = *upload.task_update;
        upload.task_update.reset();
        upload.inventory_attempted = true;
        const auto agent = viewer.circuit->agent_id;
        if (item.item_id != request.item || item.type != request.type || item.type != upload.asset->type() ||
            !can_edit_inventory(request.object, agent))
            return false;
        if (!upload_references_allowed(upload.asset, agent, request.previous_asset)) {
            alert(agent, "Not enough permissions on asset(s) referenced by task item '" +
                             item.name.value_or("") + "', update failed");
            return false;
        }
        upload.asset->name(item.name);
        upload.asset->description(item.description);
        if (!policies.store_inventory_asset(upload.asset))
            return false;
        item.asset_id = upload.asset->full_id();
        return true;
    }
    bool update_task_from_asset_upload(const ViewerAgent& viewer, AssetUpload& upload) {
        const auto request = *upload.task_update;
        std::lock_guard lock(task_data_mutex);
        const auto part = task_parts.find(request.object);
        if (part == task_parts.end() || !part->second.inventory || !world.object(request.object))
            return false;
        const auto current = part->second.inventory->find(request.item);
        if (!current || current->asset_id != request.previous_asset)
            return false;
        auto item = std::make_shared<TaskInventoryItem>(current->clone());
        if (!apply_task_asset_upload(viewer, upload, *item))
            return false;
        return part->second.inventory->update(std::move(item));
    }
    void complete_asset_upload(const ViewerAgent& viewer, Uuid transaction, AssetUpload& upload, bool success) {
        upload.complete = true;
        if (success && (upload.creation || upload.update || upload.task_update || upload.asset->local())) {
            try {
                if (upload.creation)
                    success = create_from_asset_upload(viewer, upload);
                else if (upload.update)
                    success = update_from_asset_upload(viewer, transaction, upload);
                else if (upload.task_update)
                    success = update_task_from_asset_upload(viewer, upload);
                else
                    success = policies.store_inventory_asset(upload.asset).has_value();
            } catch (const std::exception& e) {
                error("UDP asset upload: " + std::string(e.what()));
                success = false;
            }
        }
        upload.success = success;
        auto reply = lludp::make_packet("AssetUploadComplete");
        auto& fields = reply.block("AssetBlock");
        fields["UUID"] = upload.asset->full_id();
        fields["Type"] = std::int64_t{upload.asset->type()};
        fields["Success"] = success;
        send_upload_packet(viewer, std::move(reply));
    }
    void start_asset_upload(const ViewerAgent& viewer, const lludp::Packet& packet) {
        if (!policies.store_inventory_asset)
            return;
        const auto& fields = packet.block("AssetBlock");
        const auto type = static_cast<std::int8_t>(std::get<std::int64_t>(fields.at("Type")));
        const auto temporary = std::get<bool>(fields.at("Tempfile"));
        const auto& circuit = *viewer.circuit;
        const auto asset_type = static_cast<AssetType>(type);
        if (!temporary && (asset_type == AssetType::Texture || asset_type == AssetType::Sound ||
                           asset_type == AssetType::TextureTGA || asset_type == AssetType::Animation) &&
            (policies.asset_upload_user_level ? policies.asset_upload_user_level(circuit.agent_id) : 0) <
                policies.minimum_asset_upload_level) {
            viewers->transport().enqueue_if_session(
                circuit.agent_id, circuit.session_id,
                agent_alert(circuit.agent_id, "Unable to upload asset. Insufficient permissions.", false),
                ThrottleOutPacketType::Task);
            return;
        }
        const auto transaction = std::get<Uuid>(fields.at("TransactionID"));
        auto& transactions = asset_uploads[circuit.session_id];
        auto& upload = transactions[transaction];
        if (upload.asset)
            return;
        const auto& secure = circuit.secure_session_id.bytes();
        const auto& id = transaction.bytes();
        std::array<std::uint8_t, 32> combined;
        std::copy(id.begin(), id.end(), combined.begin());
        std::copy(secure.begin(), secure.end(), combined.begin() + id.size());
        const Uuid asset_id(md5({reinterpret_cast<const char*>(combined.data()), combined.size()}));
        auto asset = std::make_shared<AssetBase>(asset_id, "", type, circuit.agent_id.str());
        asset->local(std::get<bool>(fields.at("StoreLocal")));
        asset->temporary(temporary);
        asset->data = std::make_shared<std::vector<std::uint8_t>>(
            std::get<std::vector<std::uint8_t>>(fields.at("AssetData")));
        upload.asset = std::move(asset);
        if (upload.asset->data->size() > 2) {
            complete_asset_upload(viewer, transaction, upload, true);
            return;
        }
        upload.asset->data->clear();
        upload.xfer = ++next_upload_xfer;
        auto request = lludp::make_packet("RequestXfer");
        auto& request_fields = request.block("XferID");
        request_fields["ID"] = upload.xfer;
        request_fields["VFileID"] = asset_id;
        request_fields["VFileType"] = std::int64_t{type};
        send_upload_packet(viewer, std::move(request));
    }
    bool receive_asset_upload(const ViewerAgent& viewer, const lludp::Packet& packet) {
        const auto session = asset_uploads.find(viewer.circuit->session_id);
        if (session == asset_uploads.end())
            return false;
        const auto& fields = packet.block("XferID");
        const auto id = std::get<std::uint64_t>(fields.at("ID"));
        if (id == 0)
            return false;
        const auto entry = std::find_if(session->second.begin(), session->second.end(),
                                       [&](const auto& item) { return item.second.xfer == id; });
        if (entry == session->second.end())
            return false;
        auto& upload = entry->second;
        if (packet.spec->name == "AbortXfer") {
            if (!upload.complete)
                session->second.erase(entry);
            return true;
        }
        const auto number = std::get<std::uint64_t>(fields.at("Packet"));
        const auto index = number & 0x7fffffffu;
        if (upload.complete) {
            if (upload.final_packet == number)
                confirm_upload_packet(viewer, id, number);
            return true;
        }
        if (index < upload.next) {
            confirm_upload_packet(viewer, id, number);
            return true;
        }
        if (index != upload.next)
            return true;
        const auto& bytes = std::get<std::vector<std::uint8_t>>(packet.block("DataPacket").at("Data"));
        const std::size_t prefix = index == 0 ? 4 : 0;
        const bool final = (number & 0x80000000u) != 0;
        if (bytes.size() < prefix) {
            complete_asset_upload(viewer, entry->first, upload, false);
            return true;
        }
        if (prefix)
            for (unsigned i = 0; i < 4; ++i)
                upload.size |= std::uint32_t{bytes[i]} << (8 * i);
        auto& data = *upload.asset->data;
        const auto received = data.size() + bytes.size() - prefix;
        if (upload.size > 64u * 1024 * 1024 || received > upload.size || (final && received != upload.size)) {
            complete_asset_upload(viewer, entry->first, upload, false);
            return true;
        }
        data.insert(data.end(), bytes.begin() + prefix, bytes.end());
        ++upload.next;
        confirm_upload_packet(viewer, id, number);
        if (final) {
            upload.final_packet = number;
            complete_asset_upload(viewer, entry->first, upload, true);
        }
        return true;
    }
    bool inventory_transfer_allowed(const AgentCircuitData& circuit, Uuid object, Uuid item_id, Uuid asset) {
        if (policies.bypass_inventory_permissions)
            return true;
        constexpr std::uint32_t modify = 0x4000, copy = 0x8000, transfer = 0x2000;
        constexpr auto script = static_cast<int>(AssetType::LSLText);
        constexpr auto notecard = static_cast<int>(AssetType::Notecard);
        const auto agent = circuit.agent_id;
        if (object.is_zero()) {
            auto item = policies.inventory ? policies.inventory->item(agent, item_id) : std::nullopt;
            const bool library_item = !item;
            if (!item && policies.library_inventory_item)
                item = policies.library_inventory_item(item_id);
            if (!item || item->asset_id != asset)
                return false;
            const auto type = item->asset_type;
            if (library_item || (type != script && type != notecard) ||
                (policies.inventory_is_god && policies.inventory_is_god(agent)))
                return true;
            const auto required = type == script ? modify | copy : copy;
            if ((static_cast<std::uint32_t>(item->current_permissions) & required) == required)
                return true;
            viewers->transport().enqueue_if_session(
                agent, circuit.session_id,
                agent_alert(agent, type == script ? "Insufficient permissions to view script" :
                                                    "Insufficient permissions to view notecard", false),
                ThrottleOutPacketType::Task);
            return false;
        }

        const auto part = world.object(object);
        if (!part)
            return false;
        std::optional<TaskInventoryItem> item;
        {
            std::lock_guard lock(task_data_mutex);
            const auto entry = task_parts.find(object);
            if (entry != task_parts.end() && entry->second.inventory)
                if (const auto found = entry->second.inventory->find(item_id))
                    item = found->clone();
        }
        if (!item || item->asset_id != asset)
            return false;
        if (item->type == script)
            return can_edit_script(item_id, object, agent);
        if (item->type == notecard) {
            if (!can_edit_inventory(object, agent))
                return false;
            constexpr auto required = modify | copy;
            if (item->owner_id == agent)
                return (item->current_permissions & required) == required;
            if (item->group_id.is_zero() || !policies.inventory_group_member ||
                !policies.inventory_group_member(agent, item->group_id))
                return false;
            constexpr std::uint64_t object_manipulate = 0x4000000000ull;
            if (item->owner_id == item->group_id && policies.inventory_group_powers &&
                (policies.inventory_group_powers(agent, item->group_id) & object_manipulate) != 0 &&
                (item->current_permissions & required) == required)
                return true;
            return (item->group_permissions & required) == required;
        }
        constexpr auto required = modify | copy | transfer;
        return part->owner == agent && (part->owner_mask & modify) != 0 &&
               item->owner_id == agent && (item->current_permissions & required) == required;
    }
    void request_asset_transfer(const ViewerAgent& viewer, const lludp::Packet& packet) {
        constexpr std::int64_t direct_source = 2;
        constexpr std::int64_t inventory_source = 3;
        constexpr std::int64_t estate_source = 4;
        const auto& request = packet.block("TransferInfo");
        const auto source = std::get<std::int64_t>(request.at("SourceType"));
        const auto& params = std::get<std::vector<std::uint8_t>>(request.at("Params"));
        Uuid asset_id;
        Uuid object_id, item_id;
        if (source == direct_source && params.size() >= 16) {
            asset_id = Uuid::from_bytes(std::span(params).first(16));
        } else if (source == inventory_source && params.size() >= 96) {
            object_id = Uuid::from_bytes(std::span(params).subspan(48, 16));
            item_id = Uuid::from_bytes(std::span(params).subspan(64, 16));
            asset_id = Uuid::from_bytes(std::span(params).subspan(80, 16));
        } else if (source == estate_source) {
            std::lock_guard lock(estate_covenant_mutex);
            asset_id = estate_covenant;
        } else {
            return;
        }
        const auto transfer_id = std::get<Uuid>(request.at("TransferID"));
        auto fetch = [weak = weak_from_this(), circuit = viewer.circuit, asset_id, transfer_id, source, params,
                      object_id, item_id] {
            const auto state = weak.lock();
            if (!state || !state->running)
                return;
            if (source == inventory_source && !state->inventory_transfer_allowed(*circuit, object_id, item_id, asset_id))
                return;
            const auto& services = state->policies;
            auto asset = services.load_asset ? services.load_asset(asset_id) : nullptr;
            if (!asset && services.load_foreign_asset)
                asset = services.load_foreign_asset(*circuit, asset_id);
            if (asset && (!asset->data || asset->data->size() > std::numeric_limits<std::int32_t>::max() ||
                          (source == direct_source && asset->type() == static_cast<std::int8_t>(AssetType::LSLText))))
                return;

            auto& transport = state->viewers->transport();
            const auto enqueue = [&](lludp::Packet reply) {
                return transport.enqueue_if_session(circuit->agent_id, circuit->session_id, std::move(reply),
                                                    ThrottleOutPacketType::Asset);
            };
            auto info = lludp::make_packet("TransferInfo");
            info.zerocoded = true;
            auto& header = info.block("TransferInfo");
            header["TransferID"] = transfer_id;
            header["ChannelType"] = std::int64_t{2};
            header["Status"] = std::int64_t{asset ? 0 : -2};
            if (!asset) {
                header["Params"] = params;
                enqueue(std::move(info));
                return;
            }
            auto& response = std::get<std::vector<std::uint8_t>>(header.at("Params"));
            if (source == inventory_source) {
                response = params;
            } else {
                const auto response_id = source == direct_source ? asset_id : Uuid{};
                response.assign(response_id.bytes().begin(), response_id.bytes().end());
                const auto asset_type = static_cast<std::uint32_t>(asset->type());
                for (unsigned i = 0; i < 4; ++i)
                    response.push_back(static_cast<std::uint8_t>(asset_type >> (8 * i)));
            }
            const auto& data = *asset->data;
            header["Size"] = static_cast<std::int64_t>(data.size());
            if (!enqueue(std::move(info)))
                return;

            constexpr std::size_t max_chunk_size = 600;
            for (std::size_t offset = 0; offset < data.size(); offset += max_chunk_size) {
                const auto end = std::min(offset + max_chunk_size, data.size());
                auto chunk = lludp::make_packet("TransferPacket");
                chunk.zerocoded = true;
                auto& block = chunk.block("TransferData");
                block["TransferID"] = transfer_id;
                block["ChannelType"] = std::int64_t{2};
                block["Packet"] = static_cast<std::int64_t>(offset / max_chunk_size);
                block["Status"] = std::int64_t{end == data.size() ? 1 : 0};
                block["Data"] = std::vector<std::uint8_t>(data.begin() + offset, data.begin() + end);
                if (!enqueue(std::move(chunk)))
                    break;
            }
        };
        if (policies.schedule_asset_fetch)
            policies.schedule_asset_fetch(std::move(fetch));
        else
            fetch();
    }
    void request_textures(const ViewerAgent& viewer, const lludp::Packet& packet) {
        const auto session = viewer.circuit->session_id;
        for (const auto& row : packet.blocks.at("RequestImage")) {
            const auto image = std::get<Uuid>(row.at("Image"));
            const auto discard = std::get<std::int64_t>(row.at("DiscardLevel"));
            auto priority = static_cast<float>(std::get<double>(row.at("DownloadPriority")));
            if (std::get<std::uint64_t>(row.at("Type")) == 1)
                priority *= 2.f;
            if (std::isnan(priority))
                continue;
            const auto start = static_cast<std::uint32_t>(
                std::max<std::uint64_t>(1, std::get<std::uint64_t>(row.at("Packet"))));
            std::uint64_t fetch_id;
            {
                std::lock_guard lock(texture_downloads_mutex);
                auto& queue = texture_downloads[session];
                const auto entry = std::find_if(queue.begin(), queue.end(),
                                                [&](const auto& item) { return item.image == image; });
                if (discard == -1 && priority == 0.f) {
                    if (entry != queue.end())
                        queue.erase(entry);
                    if (queue.empty())
                        texture_downloads.erase(session);
                    continue;
                }
                if (entry != queue.end()) {
                    if (packet.sequence > entry->sequence) {
                        entry->sequence = packet.sequence;
                        if (discard >= 0 || !entry->sent_header) {
                            entry->next_packet = start;
                            entry->discard_level = discard;
                        }
                        entry->priority = priority;
                    }
                    continue;
                }
                fetch_id = ++next_texture_fetch;
                queue.push_back(
                    {image, nullptr, packet.sequence, start, priority, discard, false, false, fetch_id, {}});
            }
            auto fetch = [weak = weak_from_this(), circuit = viewer.circuit, image, fetch_id] {
                const auto state = weak.lock();
                if (!state)
                    return;
                {
                    std::lock_guard lock(state->texture_downloads_mutex);
                    const auto queue = state->texture_downloads.find(circuit->session_id);
                    if (queue == state->texture_downloads.end() ||
                        std::none_of(queue->second.begin(), queue->second.end(),
                                     [&](const auto& item) { return item.fetch_id == fetch_id; }))
                        return;
                }
                AssetBytes bytes;
                std::vector<std::int32_t> layer_ends;
                try {
                    const auto& services = state->policies;
                    auto asset = services.load_asset ? services.load_asset(image) : nullptr;
                    if (!asset && services.load_foreign_asset)
                        asset = services.load_foreign_asset(*circuit, image);
                    if (asset && asset->type() == static_cast<std::int8_t>(AssetType::Texture))
                        bytes = asset->data;
                    if (!bytes && services.load_asset) {
                        static const Uuid missing_image("5748decc-f629-461c-9a36-a35a221fe21f");
                        if (const auto fallback = services.load_asset(missing_image))
                            bytes = fallback->data;
                    }
                    if (bytes && services.decode_texture_layers)
                        layer_ends = services.decode_texture_layers(image, *bytes);
                } catch (const std::exception& error) {
                    state->error("Viewer texture fetch: " + std::string(error.what()));
                }
                std::lock_guard lock(state->texture_downloads_mutex);
                const auto queue = state->texture_downloads.find(circuit->session_id);
                if (queue == state->texture_downloads.end())
                    return;
                const auto entry = std::find_if(queue->second.begin(), queue->second.end(),
                    [&](const auto& item) { return item.fetch_id == fetch_id; });
                if (entry != queue->second.end()) {
                    entry->bytes = std::move(bytes);
                    entry->layer_ends = std::move(layer_ends);
                    entry->ready = true;
                }
            };
            if (!policies.schedule_asset_fetch) {
                fetch();
                continue;
            }
            try {
                policies.schedule_asset_fetch(std::move(fetch));
            } catch (...) {
                std::lock_guard lock(texture_downloads_mutex);
                const auto queue = texture_downloads.find(session);
                if (queue != texture_downloads.end()) {
                    std::erase_if(queue->second, [&](const auto& item) { return item.fetch_id == fetch_id; });
                    if (queue->second.empty())
                        texture_downloads.erase(queue);
                }
                throw;
            }
        }
    }
    static constexpr std::size_t texture_header_bytes = 600;
    static constexpr std::size_t texture_body_bytes = 1000;
    static std::size_t texture_stop_packet(std::size_t size, std::size_t count, std::int64_t discard,
                                           std::span<const std::int32_t> decoded_ends) {
        if (count == 1)
            return count;

        // Match the legacy decoder's fallback for unavailable layer boundaries.
        std::array<std::int32_t, 5> fallback{};
        if (decoded_ends.empty()) {
            constexpr std::array layer_starts{.02f, .05f, .20f, .50f};
            for (std::size_t i = 0; i < layer_starts.size(); ++i)
                fallback[i] = static_cast<std::int32_t>(static_cast<float>(size) * layer_starts[i]) - 1;
            fallback.back() = static_cast<std::int32_t>(size);
            decoded_ends = fallback;
        }
        const auto max_discard = decoded_ends.size() - 1;
        const auto level = discard < 0 ? max_discard : std::min<std::size_t>(discard, max_discard);
        const auto end = static_cast<std::int64_t>(decoded_ends[max_discard - level]);
        const auto packet = (end - static_cast<std::int64_t>(texture_header_bytes) +
                             static_cast<std::int64_t>(texture_body_bytes) - 1) /
                                static_cast<std::int64_t>(texture_body_bytes) +
                            1;
        auto stop = static_cast<std::size_t>(packet);
        const auto first_layer_end = decoded_ends.front();
        if (count == stop + 1)
            return count;
        if (stop == 1 && first_layer_end > static_cast<std::int64_t>(texture_header_bytes))
            ++stop;
        return std::min(stop, count);
    }
    void send_textures() {
        for (const auto& viewer : viewers->agents()) {
            const auto& circuit = *viewer.circuit;
            if (!viewers->transport().take_texture_refill(circuit.agent_id, circuit.session_id))
                continue;
            std::vector<lludp::Packet> outgoing;
            {
                std::lock_guard lock(texture_downloads_mutex);
                const auto found = texture_downloads.find(circuit.session_id);
                if (found == texture_downloads.end())
                    continue;
                auto& queue = found->second;
                for (int sent = 0; sent < policies.texture_send_limit && !queue.empty();) {
                    const auto entry = std::max_element(queue.begin(), queue.end(),
                        [](const auto& a, const auto& b) { return a.priority < b.priority; });
                    if (!entry->ready)
                        break;
                    const auto size = entry->bytes ? entry->bytes->size() : 0;
                    const auto count = size <= texture_header_bytes
                        ? 1 : 2 + (size - texture_header_bytes - 1) / texture_body_bytes;
                    if (!entry->bytes || count > 65535) {
                        auto reply = lludp::make_packet("ImageNotInDatabase");
                        reply.block("ImageID")["ID"] = entry->image;
                        outgoing.push_back(std::move(reply));
                        ++sent;
                        queue.erase(entry);
                        continue;
                    }
                    const auto stop = texture_stop_packet(size, count, entry->discard_level, entry->layer_ends);
                    const auto next = entry->sent_header ? std::max(entry->next_packet, 2u) : entry->next_packet;
                    if (next > stop) {
                        queue.erase(entry);
                        continue;
                    }
                    const bool header = !entry->sent_header;
                    auto reply = lludp::make_packet(header ? "ImageData" : "ImagePacket");
                    auto& identity = reply.block("ImageID");
                    identity["ID"] = entry->image;
                    std::size_t offset = 0;
                    std::size_t length = std::min(size, texture_header_bytes);
                    if (header) {
                        identity["Codec"] = std::uint64_t{2}; // JPEG 2000
                        identity["Size"] = static_cast<std::uint64_t>(size);
                        identity["Packets"] = static_cast<std::uint64_t>(count);
                        reply.zerocoded = true;
                        entry->sent_header = true;
                    } else {
                        identity["Packet"] = static_cast<std::uint64_t>(next - 1);
                        offset = texture_header_bytes + (next - 2) * texture_body_bytes;
                        length = std::min(texture_body_bytes, size - offset);
                    }
                    reply.block("ImageData")["Data"] = std::vector<std::uint8_t>(
                        entry->bytes->begin() + offset, entry->bytes->begin() + offset + length);
                    outgoing.push_back(std::move(reply));
                    ++sent;
                    entry->next_packet = next + 1;
                    if (entry->next_packet > stop)
                        queue.erase(entry);
                }
                if (queue.empty())
                    texture_downloads.erase(found);
            }
            for (auto& reply : outgoing)
                viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id, std::move(reply),
                                                        ThrottleOutPacketType::Texture);
        }
    }
    void send_mute_list(const ViewerAgent& viewer, std::uint32_t crc) {
        const auto& circuit = *viewer.circuit;
        const auto active = xfer && policies.request_mute_list && !circuit.verified_foreign_login;
        auto bytes = active ? policies.request_mute_list(circuit.agent_id, crc) : std::nullopt;
        const bool cached = crc != 0 && (!bytes || bytes->size() == 1);
        lludp::Packet reply;
        if (cached) {
            reply = lludp::make_packet("UseCachedMuteList");
            reply.block("AgentData")["AgentID"] = circuit.agent_id;
        } else if (!bytes || bytes->size() <= 1) {
            reply = lludp::make_packet("GenericMessage");
            auto& identity = reply.block("AgentData");
            identity["AgentID"] = circuit.agent_id;
            identity["SessionID"] = circuit.session_id;
            reply.block("MethodData")["Method"] = profile_string_bytes(u"emptymutelist", 255);
            reply.blocks["ParamList"].push_back(lludp::make_block(reply.spec->blocks.at(2)));
        } else {
            const auto filename = "mutes" + circuit.agent_id.str();
            xfer->add(filename, std::make_shared<std::vector<std::uint8_t>>(std::move(*bytes)));
            reply = lludp::make_packet("MuteListUpdate");
            auto& fields = reply.block("MuteData");
            fields["AgentID"] = circuit.agent_id;
            fields["Filename"] = profile_string_bytes(framework::utf16(filename), 255);
        }
        viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id, std::move(reply),
                                                ThrottleOutPacketType::Task);
    }
    void set_viewer_home(const ViewerAgent& viewer, const lludp::Packet& request) {
        const auto& circuit = *viewer.circuit;
        const auto agent = circuit.agent_id;
        const auto alert = [&](const std::string& message) {
            viewers->transport().enqueue_if_session(agent, circuit.session_id, agent_alert(agent, message, false),
                                                     ThrottleOutPacketType::Task);
        };
        const auto& location = request.block("StartLocationData");
        const auto vector = [&](const char* field) {
            const auto& value = std::get<std::vector<double>>(location.at(field));
            return Vector3{static_cast<float>(value.at(0)), static_cast<float>(value.at(1)),
                           static_cast<float>(value.at(2))};
        };
        auto position = vector("LocationPos");
        const auto look = vector("LocationLookAt");
        if (position.x == 255.5f)
            position.x = viewer.position.x;
        if (position.y == 255.5f)
            position.y = viewer.position.y;
        const auto parcel = parcel_at_truncated_position(position);
        if (!parcel || !policies.set_home) {
            alert("Set Home request failed.");
            return;
        }

        constexpr std::uint64_t allow_set_home = 1ULL << 28;
        const bool local = !circuit.verified_foreign_login && !agent.is_zero() &&
                           (!policies.is_local_grid_user || policies.is_local_grid_user(agent));
        bool allowed = local && (policies.bypass_object_permissions || god_rights_level(agent) >= 200 ||
                                 agent_god_level(agent, circuit.session_id) >= 200 || parcel->owner == agent);
        if (local && !allowed && !parcel->group.is_zero() && policies.inventory_group_powers)
            allowed = (policies.inventory_group_powers(agent, parcel->group) & allow_set_home) != 0;
        if (local && !allowed) {
            Uuid telehub;
            {
                std::lock_guard lock(telehub_mutex);
                telehub = region_telehub;
            }
            const auto hub = telehub.is_zero() ? std::nullopt : world.object(telehub);
            if (hub && !hub->parent_id && !hub->is_attachment) {
                const auto hub_parcel = parcel_at_truncated_position(hub->position);
                allowed = hub_parcel && hub_parcel->id == parcel->id;
            }
        }
        if (!allowed) {
            alert("You are not allowed to set your home location in this parcel.");
            return;
        }
        alert(policies.set_home(agent, descriptor.id, position, look)
                  ? "Home position set." : "Set Home request failed.");
    }
    bool allows_direct_teleport() const {
        constexpr std::uint32_t allow_direct_teleport = 0x100000u;
        return (policies.estate_handshake_only_flags & allow_direct_teleport) != 0;
    }
    std::optional<SceneObject> live_telehub() {
        auto hub = region_telehub.is_zero() ? std::nullopt : world.object(region_telehub);
        if (hub && (hub->parent_id || hub->is_attachment))
            return std::nullopt;
        return hub;
    }
    static Vector3 spawn_offset(const RegionSpawnPoint& point) {
        const auto horizontal = std::cos(point.pitch) * point.distance;
        return {std::cos(point.yaw) * horizontal, std::sin(point.yaw) * horizontal,
                -std::sin(point.pitch) * point.distance};
    }
    static Vector3 spawn_location(const SceneObject& hub, const RegionSpawnPoint& point) {
        const auto offset = framework::sdk_rotate_vector(spawn_offset(point), hub.rotation);
        return {hub.position.x + offset.x, hub.position.y + offset.y, hub.position.z + offset.z};
    }
    std::optional<bool> spawn_parcel_access(Uuid agent, bool foreign, const Vector3& position) const {
        if (!policies.script_parcel_at) {
            if (position.x >= 0 && position.x < descriptor.size_x &&
                position.y >= 0 && position.y < descriptor.size_y)
                return true;
            return std::nullopt;
        }
        const auto parcel = policies.script_parcel_at(position);
        if (!parcel)
            return std::nullopt;
        return parcel_allows_arrival(*parcel, agent, foreign);
    }
    bool parcel_allows_arrival(const ScriptParcel& parcel, Uuid agent, bool foreign) const {
        constexpr std::uint32_t allow_parcel_access_override = 0x20;
        if (!(policies.estate_handshake_only_flags & allow_parcel_access_override) || parcel.owner == agent ||
            (!foreign && god_rights_level(agent) >= 200) || is_estate_manager(agent))
            return true;

        constexpr std::uint32_t use_access_group = 1u << 8;
        constexpr std::uint32_t use_access_list = 1u << 9;
        constexpr std::uint32_t use_ban_list = 1u << 10;
        const auto listed = [&](std::uint32_t flags) {
            const auto entry = std::ranges::find_if(parcel.access_entries, [&](const auto& candidate) {
                return candidate.agent == agent && candidate.flags == flags;
            });
            return entry != parcel.access_entries.end() &&
                   (entry->expires == 0 || entry->expires > std::time(nullptr));
        };
        if ((parcel.flags & use_ban_list) && listed(2))
            return false;
        if (!(parcel.flags & use_access_list))
            return profile_allows_arrival(agent, foreign, parcel.flags);
        if ((parcel.flags & use_access_group) && !parcel.group.is_zero() &&
            policies.inventory_group_member && policies.inventory_group_member(agent, parcel.group))
            return true;
        return listed(1);
    }
    bool move_toward_parcel_center(const ScriptParcel& parcel, Vector3& position) const {
        constexpr float land_unit = 4.f;
        const auto width = descriptor.size_x / 4;
        const auto height = descriptor.size_y / 4;
        if (parcel.land_bitmap.size() != std::size_t(width) * height / 8)
            return false;
        const auto contains = [&](int x, int y) {
            const auto cell = y * width + x;
            return (parcel.land_bitmap[cell / 8] & (1u << (cell % 8))) != 0;
        };
        float x = position.x / land_unit;
        float y = position.y / land_unit;
        if (x < 0 || x >= width || y < 0 || y >= height)
            return false;
        if (contains(static_cast<int>(x), static_cast<int>(y)))
            return true;
        float dx = static_cast<float>(parcel.center.x) - position.x;
        float dy = static_cast<float>(parcel.center.y) - position.y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        if (distance == 0)
            return false;
        dx /= distance;
        dy /= distance;

        int min_x = width, min_y = height, max_x = 0, max_y = 0;
        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                if (!contains(col, row))
                    continue;
                min_x = std::min(min_x, col);
                min_y = std::min(min_y, row);
                max_x = std::max(max_x, col + 1);
                max_y = std::max(max_y, row + 1);
            }
        }
        const auto enter_bounds = [](float& coordinate, float direction, int min, int max) {
            if (min >= max || (coordinate < min && direction <= 0) ||
                (coordinate >= max && direction >= 0))
                return false;
            if (coordinate < min)
                coordinate = static_cast<float>(min);
            else if (coordinate >= max)
                coordinate = static_cast<float>(max - 1);
            return true;
        };
        if (!enter_bounds(x, dx, min_x, max_x) || !enter_bounds(y, dy, min_y, max_y))
            return false;
        while (!contains(static_cast<int>(x), static_cast<int>(y))) {
            x += dx;
            y += dy;
            if (x < min_x || x >= max_x || y < min_y || y >= max_y)
                return false;
        }

        x *= land_unit;
        y *= land_unit;
        if (std::abs(dx) > std::abs(dy)) {
            x += dx < 0 ? land_unit - .5f : .5f;
            y = std::clamp(position.y + std::abs((x - position.x) / dx) * dy,
                           y + .5f, y + land_unit - .5f);
        } else {
            y += dy < 0 ? land_unit - .5f : .5f;
            x = std::clamp(position.x + std::abs((y - position.y) / dy) * dx,
                           x + .5f, x + land_unit - .5f);
        }
        position.x = x;
        position.y = y;
        return true;
    }
    bool adjust_parcel_access(Uuid agent, bool foreign, Vector3& position) const {
        if (!policies.script_parcel_at)
            return true;
        if (position.x < 0 || position.x >= descriptor.size_x)
            position.x = position.x < 0 ? 0.f : descriptor.size_x - .5f;
        if (position.y < 0 || position.y >= descriptor.size_y)
            position.y = position.y < 0 ? 0.f : descriptor.size_y - .5f;
        if (!foreign && god_rights_level(agent) >= 200)
            return true;
        const auto access = spawn_parcel_access(agent, foreign, position);
        if (!access || *access || !policies.list_parcels)
            return access.value_or(false);
        std::shared_ptr<const ScriptParcel> nearest;
        double best_distance = std::numeric_limits<float>::max();
        for (const auto& parcel : policies.list_parcels()) {
            const auto dx = parcel->center.x - position.x;
            const auto dy = parcel->center.y - position.y;
            const auto distance = dx * dx + dy * dy;
            if (distance < best_distance && parcel_allows_arrival(*parcel, agent, foreign)) {
                nearest = parcel;
                best_distance = distance;
            }
        }
        return nearest && move_toward_parcel_center(*nearest, position);
    }
    enum class TelehubArrival { no_hub, skipped, placed };
    TelehubArrival route_telehub_arrival(ViewerAgent& agent) {
        constexpr std::uint32_t via_landmark = 1u << 3;
        constexpr std::uint32_t telehub_flags = (1u << 4) | (1u << 7) | (1u << 30);
        const auto flags = agent.circuit->teleport_flags;
        const auto agent_id = agent.circuit->agent_id;
        const bool foreign = agent.circuit->verified_foreign_login;
        if (allows_direct_teleport())
            return TelehubArrival::no_hub;

        std::lock_guard lock(telehub_mutex);
        const auto hub = live_telehub();
        if (!hub)
            return TelehubArrival::no_hub;
        if (!(flags & telehub_flags) && (policies.telehub_allow_landmarks || !(flags & via_landmark)))
            return TelehubArrival::skipped;
        if (region_spawn_points.empty()) {
            const auto access = spawn_parcel_access(agent_id, foreign, hub->position);
            if (!access)
                return TelehubArrival::skipped;
            agent.position = hub->position;
            return *access ? TelehubArrival::placed : TelehubArrival::skipped;
        }

        bool valid_parcel = false;
        const auto can_land = [&](const Vector3& position) {
            const auto parcel_access = spawn_parcel_access(agent_id, foreign, position);
            if (!parcel_access)
                return false;
            valid_parcel = true;
            std::string reason;
            return *parcel_access && policies.access(agent_id, position, reason);
        };
        const auto count = region_spawn_points.size();
        if (policies.spawn_point_routing == "random" && count >= 3) {
            static thread_local std::mt19937 generator(std::random_device{}());
            std::uniform_int_distribution<std::size_t> choose(0, count - 2);
            for (std::size_t attempt = 0; attempt < count; ++attempt) {
                const auto position = spawn_location(*hub, region_spawn_points[choose(generator)]);
                if (can_land(position)) {
                    agent.position = position;
                    return TelehubArrival::placed;
                }
            }
        }
        if (policies.spawn_point_routing == "sequence" || policies.spawn_point_routing == "random") {
            valid_parcel = false;
            Vector3 position;
            bool selected = false;
            for (std::size_t attempt = 0; attempt < count; ++attempt) {
                if (next_spawn_point >= count)
                    next_spawn_point = 0;
                position = spawn_location(*hub, region_spawn_points[next_spawn_point++]);
                if (can_land(position)) {
                    selected = true;
                    break;
                }
            }
            if (valid_parcel)
                agent.position = position;
            return selected ? TelehubArrival::placed : TelehubArrival::skipped;
        }

        float shortest_distance = std::numeric_limits<float>::max();
        std::optional<Vector3> closest;
        for (const auto& point : region_spawn_points) {
            const auto position = spawn_location(*hub, point);
            if (!can_land(position))
                continue;
            const auto dx = position.x - agent.position.x;
            const auto dy = position.y - agent.position.y;
            const auto dz = position.z - agent.position.z;
            const auto distance = dx * dx + dy * dy + dz * dz;
            if (distance < shortest_distance) {
                shortest_distance = distance;
                closest = position;
            }
        }
        if (valid_parcel)
            agent.position = closest.value_or(spawn_location(*hub, region_spawn_points.front()));
        return closest ? TelehubArrival::placed : TelehubArrival::skipped;
    }
    void route_root_arrival(ViewerAgent& agent) {
        const auto& circuit = *agent.circuit;
        if (policies.use_sl_landing_behavior &&
            agent_god_level(circuit.agent_id, circuit.session_id) >= 200)
            return;
        if (!policies.use_sl_landing_behavior &&
            !adjust_parcel_access(circuit.agent_id, circuit.verified_foreign_login, agent.position))
            return;
        const auto telehub = route_telehub_arrival(agent);
        if (telehub == TelehubArrival::placed ||
            (!policies.use_sl_landing_behavior && telehub == TelehubArrival::skipped) ||
            !policies.script_parcel_at)
            return;
        if (policies.use_sl_landing_behavior &&
            !adjust_parcel_access(circuit.agent_id, circuit.verified_foreign_login, agent.position))
            return;

        constexpr std::uint32_t via_login_and_region = (1u << 7) | (1u << 12);
        constexpr std::uint32_t parcel_landing_flags = (1u << 3) | (1u << 4) | (1u << 30);
        const auto flags = circuit.teleport_flags;
        if ((flags & via_login_and_region) != via_login_and_region && !(flags & parcel_landing_flags))
            return;
        const auto parcel = policies.script_parcel_at(agent.position);
        constexpr int landing_point = 1;
        if (!parcel || parcel->landing_type != landing_point || parcel->landing_point == Vector3d{})
            return;
        if (!policies.use_sl_landing_behavior) {
            constexpr std::uint32_t via_location_or_hg = (1u << 4) | (1u << 30);
            const bool privileged = parcel->owner == circuit.agent_id || is_estate_manager(circuit.agent_id) ||
                                    (!circuit.verified_foreign_login && god_rights_level(circuit.agent_id) >= 200);
            if (viewer_is_god(circuit.agent_id, circuit.session_id) ||
                (privileged && !(flags & via_location_or_hg)))
                return;
        }
        const auto to_vector = [](const Vector3d& value) {
            return Vector3{static_cast<float>(value.x), static_cast<float>(value.y),
                           static_cast<float>(value.z)};
        };
        agent.position = to_vector(parcel->landing_point);
        if (policies.use_sl_landing_behavior && parcel->landing_look_at != Vector3d{})
            agent.look_at = to_vector(parcel->landing_look_at);
    }
    void estate_telehub(const ViewerAgent& viewer, std::u16string_view command, std::uint32_t parameter) {
        std::lock_guard lock(telehub_mutex);
        auto telehub = region_telehub;
        auto points = region_spawn_points;
        bool changed = false;
        if (command == u"connect" || command == u"spawnpoint add") {
            const auto part = world.object(parameter);
            if (!part)
                return;
            if (command == u"connect") {
                const auto root = part->parent_id ? world.object(part->parent_id) : part;
                if (!root || root->is_attachment)
                    return;
                telehub = root->id;
                points.clear();
                changed = true;
            } else if (const auto hub = telehub.is_zero() ? std::nullopt : world.object(telehub)) {
                const auto position = script_world_position(*part);
                const Vector3 relative{position.x - hub->position.x, position.y - hub->position.y,
                                       position.z - hub->position.z};
                const auto distance =
                    std::sqrt(relative.x * relative.x + relative.y * relative.y + relative.z * relative.z);
                auto inverse = hub->rotation;
                inverse.w = -inverse.w;
                const auto offset = framework::sdk_rotate_vector(relative, inverse);
                points.push_back({std::atan2(offset.y, offset.x),
                                  -std::atan2(offset.z, std::sqrt(offset.x * offset.x + offset.y * offset.y)),
                                  distance});
                changed = true;
            }
        } else if (command == u"delete" && !telehub.is_zero()) {
            telehub = {};
            points.clear();
            changed = true;
        } else if (command == u"spawnpoint remove" && !telehub.is_zero()) {
            if (parameter >= points.size())
                return;
            points.erase(points.begin() + parameter);
            changed = true;
        }
        if (changed) {
            if (policies.persist_telehub)
                policies.persist_telehub(telehub, points);
            region_telehub = telehub;
            region_spawn_points = points;
        }

        const auto hub = telehub.is_zero() ? std::nullopt : world.object(telehub);
        auto reply = lludp::make_packet("TelehubInfo");
        auto& data = reply.block("TelehubBlock");
        const auto position = hub ? hub->position : Vector3{};
        const auto rotation = hub ? hub->rotation : framework::Quaternion{0, 0, 0, 1};
        data["ObjectID"] = hub ? hub->id : Uuid{};
        data["ObjectName"] = hub ? profile_string_bytes(framework::utf16_replacing(hub->name), 255)
                                 : std::vector<std::uint8_t>{};
        data["TelehubPos"] = std::vector<double>{position.x, position.y, position.z};
        data["TelehubRot"] = std::vector<double>{rotation.x, rotation.y, rotation.z, rotation.w};
        if (hub) {
            for (const auto& point : points) {
                const auto offset = spawn_offset(point);
                auto spawn = lludp::make_block(reply.spec->blocks.at(1));
                spawn["SpawnPointPos"] = std::vector<double>{offset.x, offset.y, offset.z};
                reply.blocks["SpawnPointBlock"].push_back(std::move(spawn));
            }
        }
        viewers->transport().enqueue_if_session(viewer.circuit->agent_id, viewer.circuit->session_id,
                                                std::move(reply), ThrottleOutPacketType::Task);
    }
    std::shared_ptr<const ScriptParcel> parcel_by_local_id(std::int64_t local_id) const {
        const auto parcels = policies.list_parcels ? policies.list_parcels() :
                                                    std::vector<std::shared_ptr<const ScriptParcel>>{};
        std::shared_ptr<const ScriptParcel> parcel;
        if (parcels.empty() && policies.script_parcel_at) {
            parcel = policies.script_parcel_at({0, 0, 0});
        } else {
            const auto match = std::ranges::find_if(parcels, [local_id](const auto& candidate) {
                return candidate->local_id == local_id;
            });
            if (match != parcels.end())
                parcel = *match;
        }
        return parcel && parcel->local_id == local_id ? parcel : nullptr;
    }
    void send_parcel_access_lists(const ViewerAgent& viewer, std::int64_t local_id) {
        const auto parcel = parcel_by_local_id(local_id);
        if (!parcel)
            return;

        const auto& circuit = *viewer.circuit;
        const auto now = std::time(nullptr);
        const auto send = [&](std::uint64_t flag) {
            auto reply = lludp::make_packet("ParcelAccessListReply");
            auto& data = reply.block("Data");
            data["AgentID"] = circuit.agent_id;
            data["SequenceID"] = std::int64_t{0};
            data["Flags"] = flag;
            data["LocalID"] = local_id;
            auto& entries = reply.blocks["List"];
            const auto append = [&](Uuid agent, std::int64_t expires) {
                auto entry = lludp::make_block(reply.spec->blocks.at(1));
                entry["ID"] = agent;
                entry["Time"] = expires;
                entry["Flags"] = flag;
                entries.push_back(std::move(entry));
            };
            for (const auto& entry : parcel->access_entries)
                if (entry.flags == flag && (entry.expires == 0 || entry.expires > now))
                    append(entry.agent, entry.expires);
            if (entries.empty())
                append({}, 0);
            viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id,
                                                    std::move(reply), ThrottleOutPacketType::Task);
        };
        send(1);
        send(2);
    }
    std::mutex parcel_access_updates_mutex;
    std::map<std::pair<Uuid, std::uint32_t>, Uuid> parcel_access_transactions;
    bool can_edit_parcel_access(const ScriptParcel& parcel, Uuid agent, bool foreign,
                                std::uint64_t required_powers) const {
        return policies.bypass_object_permissions || parcel.owner == agent ||
               (!foreign && god_rights_level(agent) >= 200) ||
               (parcel.group_owned && policies.inventory_group_powers &&
                (policies.inventory_group_powers(agent, parcel.group) & required_powers) == required_powers);
    }
    bool edit_script_parcel_list(Uuid object_id, script::LslParcelListAction action, bool bans,
                                 Uuid agent, double hours) {
        if (!policies.update_parcel_access || !policies.script_parcel_at)
            return true;
        const auto object = world.object(object_id);
        if (!object)
            return true;
        const auto position = script_world_position(*object);
        std::lock_guard lock(parcel_access_updates_mutex);
        const auto parcel = policies.script_parcel_at(position);
        constexpr std::uint64_t manage_bans = 1ull << 30, manage_passes = 1ull << 31;
        if (!parcel || !can_edit_parcel_access(*parcel, object->owner, false, bans ? manage_bans : manage_passes))
            return true;

        auto updated = std::make_shared<ScriptParcel>(*parcel);
        const auto flags = bans ? 2u : 1u;
        constexpr std::uint32_t use_ban_list = 1u << 10;
        const auto entry = std::ranges::find_if(updated->access_entries, [&](const auto& value) {
            return value.flags == flags && value.agent == agent;
        });
        bool sleep = true;
        switch (action) {
        case script::LslParcelListAction::add: {
            const auto seconds = std::trunc(hours * 3600.);
            const auto duration = seconds >= std::numeric_limits<std::int32_t>::min() &&
                                  seconds <= std::numeric_limits<std::int32_t>::max()
                                      ? static_cast<std::int32_t>(seconds)
                                      : std::numeric_limits<std::int32_t>::min();
            const auto expires = hours == 0 ? 0 : std::bit_cast<std::int32_t>(
                static_cast<std::uint32_t>(std::time(nullptr)) + static_cast<std::uint32_t>(duration));
            if (entry != updated->access_entries.end()) {
                if (entry->expires == 0 || expires <= entry->expires)
                    return false;
                entry->expires = expires;
                sleep = false;
            } else {
                updated->access_entries.push_back({agent, flags, expires});
                if (bans)
                    updated->flags |= use_ban_list;
            }
            break;
        }
        case script::LslParcelListAction::remove:
            if (entry == updated->access_entries.end())
                return true;
            updated->access_entries.erase(entry);
            break;
        case script::LslParcelListAction::reset:
            std::erase_if(updated->access_entries, [&](const auto& value) { return value.flags == flags; });
            if (bans)
                updated->flags &= ~use_ban_list;
            break;
        }
        try {
            // The original script reset changes the live list without raising LandObjectUpdated.
            policies.update_parcel_access(updated, action != script::LslParcelListAction::reset);
        } catch (const std::exception& failure) {
            error(std::string("Script parcel access update failed: ") + failure.what());
        }
        return sleep;
    }
    void update_parcel_access_lists(const ViewerAgent& viewer, const lludp::Packet& packet) {
        if (!policies.update_parcel_access || !(policies.estate_handshake_only_flags & 0x20u))
            return;
        const auto& data = packet.block("Data");
        const auto flags = static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("Flags")) & 3u);
        if (flags == 0)
            return;
        const auto local_id = std::get<std::int64_t>(data.at("LocalID"));
        const auto transaction = std::get<Uuid>(data.at("TransactionID"));
        const auto& circuit = *viewer.circuit;
        const auto agent = circuit.agent_id;
        std::lock_guard lock(parcel_access_updates_mutex);
        const auto parcel = parcel_by_local_id(local_id);
        if (!parcel)
            return;
        const auto required_powers = std::uint64_t(flags) << 29;
        if (!can_edit_parcel_access(*parcel, agent, circuit.verified_foreign_login, required_powers))
            return;

        const auto key = std::pair{parcel->id, flags};
        const auto previous = parcel_access_transactions.find(key);
        const bool first = previous == parcel_access_transactions.end() || previous->second != transaction;
        auto updated = std::make_shared<ScriptParcel>(*parcel);
        if (first)
            std::erase_if(updated->access_entries, [&](const auto& entry) { return (entry.flags & flags) != 0; });
        const auto& entries = packet.blocks.at("List");
        const bool clear = first && entries.size() == 1 && std::get<Uuid>(entries.front().at("ID")).is_zero();
        const auto land_flags = flags << 9;
        if (clear) {
            updated->flags &= ~land_flags;
        } else {
            updated->flags |= land_flags;
            for (const auto& entry : entries)
                updated->access_entries.push_back({std::get<Uuid>(entry.at("ID")), flags,
                    static_cast<std::int32_t>(std::get<std::int64_t>(entry.at("Time")))});
        }
        try {
            policies.update_parcel_access(updated, true);
        } catch (const std::exception& failure) {
            error(std::string("Parcel access update failed: ") + failure.what());
            return;
        }
        if (clear)
            parcel_access_transactions.erase(key);
        else
            parcel_access_transactions.insert_or_assign(key, transaction);
    }
    bool can_edit_estate_region(const ViewerAgent& viewer) const {
        const auto id = viewer.circuit->agent_id;
        return policies.bypass_estate_permissions || is_estate_manager(id) ||
               (!viewer.circuit->verified_foreign_login && god_rights_level(id) >= 200);
    }

    std::int32_t estate_agent_limit(std::int32_t requested) const {
        const auto capacity = static_cast<std::int32_t>(policies.region_agent_capacity);
        return requested > 1 && requested <= capacity ? requested : capacity;
    }

    void forward_region_covenant(Uuid covenant) {
        try {
            policies.forward_estate_covenant(covenant);
        } catch (const std::exception& failure) {
            error(std::string("Estate region notification failed: ") + failure.what());
        }
    }

    void notify_estate_region_change() {
        if (!policies.forward_estate_covenant)
            return;
        Uuid covenant;
        {
            std::lock_guard lock(estate_covenant_mutex);
            estate_region_change_due = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            covenant = estate_covenant;
        }
        forward_region_covenant(covenant);
    }

    void poll_estate_region_change() {
        if (!policies.forward_estate_covenant)
            return;
        Uuid covenant;
        {
            std::lock_guard lock(estate_covenant_mutex);
            if (!estate_region_change_due || std::chrono::steady_clock::now() < *estate_region_change_due)
                return;
            estate_region_change_due.reset();
            covenant = estate_covenant;
        }
        forward_region_covenant(covenant);
    }

    void edit_estate_region_settings(const std::function<void(EstateRegionSettings&)>& edit,
                                     bool broadcast = true) {
        std::lock_guard lock(estate_region_settings_mutex);
        EstateRegionSettings settings{region_flags(), region_agent_limit.load(), region_maturity.load(),
                                      region_object_bonus.load()};
        edit(settings);
        if (policies.persist_estate_region_settings)
            policies.persist_estate_region_settings(settings);
        constexpr std::uint32_t mask =
            0x40u | 0x80000u | 0x1u | 0x80u | 0x400000u | 0x4000000u | 0x20000000u | 0x7000u;
        auto previous = region_setting_flags.load();
        while (!region_setting_flags.compare_exchange_weak(
            previous, (previous & ~mask) | (settings.flags & mask & ~0x40u))) {
        }
        block_terraform = (settings.flags & 0x40u) != 0;
        region_agent_limit = settings.agent_limit;
        region_object_bonus = settings.object_bonus;
        update_maturity(settings.maturity);
        notify_estate_region_change();
        if (broadcast)
            for (const auto& viewer : viewers->agents())
                send_region_info(viewer);
    }

    struct TerrainLimits {
        float raise;
        float lower;
    };
    void update_region_terrain_settings(float water, std::optional<TerrainLimits> new_limits = {}) {
        try {
            std::lock_guard lock(terrain_edit_mutex);
            const auto limits = new_limits.value_or(TerrainLimits{terrain_raise_limit, terrain_lower_limit});
            if (policies.persist_region_terrain_settings)
                policies.persist_region_terrain_settings(water, limits.raise, limits.lower);
            if (water_height.exchange(water) != water)
                if (const auto physics = viewer_physics.load())
                    physics->set_water_height(water);
            terrain_raise_limit = limits.raise;
            terrain_lower_limit = limits.lower;
        } catch (const std::exception& failure) {
            error(std::string("Region terrain settings update failed: ") + failure.what());
            return;
        }
        for (const auto& viewer : viewers->agents())
            send_region_info(viewer);
        notify_estate_region_change();
    }
    void send_region_info(const ViewerAgent& a) {
        std::lock_guard lock(terrain_edit_mutex);
        const auto& circuit = *a.circuit;
        auto reply = lludp::make_packet("RegionInfo");
        auto& identity = reply.block("AgentData");
        identity["AgentID"] = circuit.agent_id;
        identity["SessionID"] = circuit.session_id;
        auto& info = reply.block("RegionInfo");
        info["SimName"] = profile_string_bytes(framework::utf16(descriptor.name.value_or("")), 255);
        info["EstateID"] = std::uint64_t(policies.estate_id);
        info["ParentEstateID"] = std::uint64_t(policies.parent_estate_id);
        info["RegionFlags"] = std::uint64_t((region_flags() & ~0x10u) | policies.estate_menu_flags);
        info["SimAccess"] = std::uint64_t(region_access.load());
        info["MaxAgents"] = std::uint64_t(static_cast<std::uint8_t>(region_agent_limit));
        info["BillableFactor"] = double(policies.estate_billable_factor);
        info["ObjectBonusFactor"] = double(region_object_bonus);
        info["WaterHeight"] = double(water_height);
        info["TerrainRaiseLimit"] = double(terrain_raise_limit);
        info["TerrainLowerLimit"] = double(terrain_lower_limit);
        info["PricePerMeter"] = std::int64_t(policies.estate_price_per_meter);
        info["RedirectGridX"] = std::int64_t(policies.estate_redirect_x);
        info["RedirectGridY"] = std::int64_t(policies.estate_redirect_y);
        auto& capacity = reply.block("RegionInfo2");
        capacity["ProductName"] = profile_string_bytes(framework::utf16(policies.region_type), 255);
        capacity["MaxAgents32"] = std::uint64_t(static_cast<std::uint32_t>(region_agent_limit));
        capacity["HardMaxAgents"] = std::uint64_t(policies.region_agent_capacity);
        capacity["HardMaxObjects"] = std::uint64_t(policies.region_object_capacity);
        viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id, std::move(reply),
                                                ThrottleOutPacketType::Task);
    }
    void packet(const ViewerAgent& a, const lludp::Packet& p) {
        if (p.spec->name == "ParcelAccessListRequest") {
            send_parcel_access_lists(a, std::get<std::int64_t>(p.block("Data").at("LocalID")));
            return;
        }
        if (p.spec->name == "ParcelAccessListUpdate") {
            update_parcel_access_lists(a, p);
            return;
        }
        if (p.spec->name == "SetStartLocationRequest") {
            set_viewer_home(a, p);
            return;
        }
        if (p.spec->name == "MuteListRequest") {
            send_mute_list(a, static_cast<std::uint32_t>(
                std::get<std::uint64_t>(p.block("MuteData").at("MuteCRC"))));
            return;
        }
        if (p.spec->name == "UpdateMuteListEntry" || p.spec->name == "RemoveMuteListEntry") {
            if (a.circuit->verified_foreign_login || !policies.store_mute || !policies.remove_mute)
                return;
            const auto& fields = p.block("MuteData");
            const auto agent = a.circuit->agent_id;
            const auto id = std::get<Uuid>(fields.at("MuteID"));
            const auto name = framework::utf8(framework::sdk_bytes_to_string(
                std::get<std::vector<std::uint8_t>>(fields.at("MuteName"))));
            if (p.spec->name == "RemoveMuteListEntry") {
                policies.remove_mute(agent, id, name);
            } else {
                const auto type = static_cast<int>(std::get<std::int64_t>(fields.at("MuteType")));
                if (type == 1 && id == agent)
                    return;
                if (type == 1 && !id.is_zero() &&
                    (policies.bypass_object_permissions || god_rights_level(id) >= 200)) {
                    send_mute_list(a, 0);
                    return;
                }
                const auto flags = static_cast<std::uint32_t>(std::get<std::uint64_t>(fields.at("MuteFlags")));
                policies.store_mute(agent, id, name, type, std::bit_cast<std::int32_t>(flags));
            }
            return;
        }
        if (p.spec->name == "TransferRequest") {
            request_asset_transfer(a, p);
            return;
        }
        if (p.spec->name == "RequestImage") {
            request_textures(a, p);
            return;
        }
        if (p.spec->name == "AgentDataUpdateRequest") {
            update_viewer_active_group(a, {}, false);
            send_viewer_group_memberships(a);
            return;
        }
        if (p.spec->name == "ActivateGroup") {
            update_viewer_active_group(a, std::get<Uuid>(p.block("AgentData").at("GroupID")), true);
            send_viewer_group_memberships(a);
            return;
        }
        if (p.spec->name == "SetGroupAcceptNotices") {
            if (!a.root || !policies.set_viewer_group_preferences)
                return;
            const auto& data = p.block("Data");
            try {
                policies.set_viewer_group_preferences(a.circuit->agent_id, std::get<Uuid>(data.at("GroupID")),
                                                      std::get<bool>(data.at("AcceptNotices")),
                                                      std::get<bool>(p.block("NewData").at("ListInProfile")));
            } catch (const std::exception& e) {
                error(std::string("Group preferences update failed: ") + e.what());
            }
            return;
        }
        if (p.spec->name == "GroupTitleUpdate" || p.spec->name == "GroupTitlesRequest") {
            if (!a.root)
                return;
            const auto& request = p.block("AgentData");
            const auto group = std::get<Uuid>(request.at("GroupID"));
            const auto& circuit = *a.circuit;
            try {
                if (p.spec->name == "GroupTitleUpdate") {
                    if (!policies.select_viewer_group_title || !policies.viewer_group_titles)
                        return;
                    const auto role = std::get<Uuid>(request.at("TitleRoleID"));
                    const auto assigned = policies.viewer_group_titles(circuit.agent_id, group);
                    if (std::none_of(assigned.begin(), assigned.end(),
                                     [&](const auto& title) { return title.role == role; }))
                        return;
                    policies.select_viewer_group_title(circuit.agent_id, group, role);
                    update_viewer_active_group(a, {}, true);
                } else if (policies.viewer_group_titles) {
                    const auto titles = policies.viewer_group_titles(circuit.agent_id, group);
                    auto reply = lludp::make_packet("GroupTitlesReply");
                    auto& identity = reply.block("AgentData");
                    identity["AgentID"] = circuit.agent_id;
                    identity["GroupID"] = group;
                    identity["RequestID"] = request.at("RequestID");
                    for (const auto& title : titles)
                        reply.blocks["GroupData"].push_back(
                            {{"RoleID", title.role},
                             {"Selected", title.selected},
                             {"Title", profile_string_bytes(framework::utf16(title.name), 255)}});
                    viewers->transport().enqueue_if_session(circuit.agent_id, circuit.session_id,
                                                            std::move(reply), ThrottleOutPacketType::Task);
                }
            } catch (const std::exception& e) {
                error(std::string("Group title request failed: ") + e.what());
            }
            return;
        }
        if (p.spec->name == "EstateCovenantRequest") {
            auto reply = lludp::make_packet("EstateCovenantReply");
            auto& data = reply.block("Data");
            {
                std::lock_guard lock(estate_covenant_mutex);
                data["CovenantID"] = estate_covenant;
                data["CovenantTimestamp"] = std::uint64_t(estate_covenant_timestamp);
            }
            data["EstateOwnerID"] = descriptor.estate_owner;
            data["EstateName"] = profile_string_bytes(framework::utf16(policies.estate_name), 255);
            viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id,
                                                    std::move(reply), ThrottleOutPacketType::Task);
            return;
        }
        if (p.spec->name == "RequestRegionInfo") {
            send_region_info(a);
            return;
        }
        if (p.spec->name == "GodKickUser") {
            god_kick_user(a, p);
            return;
        }
        if (p.spec->name == "RequestGodlikePowers") {
            request_god_mode(a, std::get<bool>(p.block("RequestBlock").at("Godlike")));
            return;
        }
        if ((p.spec->name == "SendXferPacket" || p.spec->name == "AbortXfer") && receive_terrain_upload(a, p))
            return;
        if (policies.advanced_profiles) {
            auto client = profile_client(a);
            if (!client)
                return;
            if (policies.advanced_profiles->dispatch_viewer_packet(p, client))
                return;
        }
        if (p.spec->name == "UserInfoRequest") {
            const auto& circuit = *a.circuit;
            viewers->transport().enqueue_if_session(
                circuit.agent_id, circuit.session_id,
                user_profile_info_packet(circuit.agent_id, false, true, u""), ThrottleOutPacketType::Task);
            return;
        }
        if (p.spec->name == "AvatarPropertiesRequest") {
            if (basic_profiles)
                basic_profiles->request(a.circuit->agent_id, a.circuit->session_id,
                                        std::get<Uuid>(p.block("AgentData").at("AvatarID")));
            return;
        }
        if (p.spec->name == "ObjectBuy" || p.spec->name == "RezObject" || p.spec->name == "ScriptAnswerYes" ||
            p.spec->name == "AgentAnimation" || p.spec->name == "AgentSetAppearance" ||
            p.spec->name == "AgentIsNowWearing" || p.spec->name == "AgentCachedTexture") {
            std::lock_guard lock(commands_mutex);
            task_commands.push_back({a, p, 0});
            return;
        }
        if (p.spec->name == "CreateInventoryItem" || p.spec->name == "LinkInventoryItem" ||
            p.spec->name == "CopyInventoryItem" || p.spec->name == "UpdateInventoryItem" ||
            p.spec->name == "MoveInventoryItem" || p.spec->name == "RemoveInventoryItem" ||
            p.spec->name == "RemoveInventoryFolder" || p.spec->name == "RemoveInventoryObjects" ||
            p.spec->name == "UpdateTaskInventory" || p.spec->name == "MoveTaskInventory" ||
            p.spec->name == "RemoveTaskInventory" || p.spec->name == "SetScriptRunning" ||
            p.spec->name == "ScriptReset" || p.spec->name == "GetScriptRunning" ||
            p.spec->name == "RequestTaskInventory" || p.spec->name == "RequestXfer" ||
            p.spec->name == "ConfirmXferPacket" || p.spec->name == "AbortXfer" ||
            p.spec->name == "AssetUploadRequest" || p.spec->name == "SendXferPacket") {
            std::lock_guard lock(commands_mutex);
            task_commands.push_back(
                {a, p, p.spec->name == "CreateInventoryItem" ? user_inventory_unix_time() : 0});
            return;
        }
        if (inventory_packet(a, p))
            return;
        if (p.spec->name == "AgentWearablesRequest") {
            std::lock_guard lock(commands_mutex);
            pending_wearables.push_back({std::chrono::steady_clock::now() + std::chrono::seconds(4),
                                         a.circuit->agent_id, a.circuit->session_id});
            return;
        }
        if (!a.root)
            return;
        if (p.spec->name == "ObjectSaleInfo") {
            const auto& identity = p.block("AgentData");
            if (std::get<Uuid>(identity.at("AgentID")) != a.circuit->agent_id ||
                std::get<Uuid>(identity.at("SessionID")) != a.circuit->session_id)
                return;
            const auto blocks = p.blocks.find("ObjectData");
            if (blocks == p.blocks.end())
                return;
            for (const auto& entry : blocks->second) {
                const auto local = static_cast<std::uint32_t>(std::get<std::uint64_t>(entry.at("LocalID")));
                auto root = world.object(local);
                if (root && !root->is_attachment && root->parent_id)
                    root = world.object(root->parent_id);
                if (!root)
                    continue;
                const auto type = static_cast<std::uint8_t>(std::get<std::uint64_t>(entry.at("SaleType")));
                constexpr std::uint32_t transfer = 1u << 13, copy = 1u << 15;
                const auto required = transfer | (type == 2 ? copy : 0u);
                const bool bypass = policies.bypass_sale_permissions;
                const bool god = !root->is_attachment && policies.inventory_is_god &&
                                 policies.inventory_is_god(a.circuit->agent_id);
                std::uint32_t effective = root->owner_mask;
                if (!bypass && !god && !root->is_attachment && root->owner == a.circuit->agent_id) {
                    const auto parts = world.objects();
                    std::lock_guard lock(task_data_mutex);
                    for (const auto& part : parts) {
                        if (part.id != root->id &&
                            (part.is_attachment || !root->local_id || part.parent_id != root->local_id))
                            continue;
                        const auto task = task_parts.find(part.id);
                        if (task == task_parts.end() || !task->second.inventory)
                            continue;
                        constexpr std::int32_t landmark_inventory_type = 3;
                        for (const auto& item : task->second.inventory->items())
                            if (item && item->inventory_type != landmark_inventory_type)
                                effective &= item->current_permissions;
                    }
                    if (!(effective & (copy | transfer)))
                        effective |= transfer;
                    effective &= root->base_mask;
                }
                if (!bypass && !god &&
                    (root->is_attachment || root->owner != a.circuit->agent_id ||
                     (effective & required) != required)) {
                    viewers->transport().enqueue_if_session(
                        a.circuit->agent_id, a.circuit->session_id,
                        agent_alert(a.circuit->agent_id, "You don't have permission to set object on sale",
                                    false),
                        ThrottleOutPacketType::Task);
                    continue;
                }
                const auto price = static_cast<std::int32_t>(std::get<std::int64_t>(entry.at("SalePrice")));
                if (world.set_sale_info(root->id, type, price))
                    send_part_properties(root->id, a.circuit->agent_id);
            }
            return;
        }
        if (p.spec->name == "RequestObjectPropertiesFamily") {
            const auto& request = p.block("ObjectData");
            auto root = world.object(std::get<Uuid>(request.at("ObjectID")));
            if (root && !root->is_attachment && root->parent_id)
                root = world.object(root->parent_id);
            if (root) {
                const auto flags =
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(request.at("RequestFlags")));
                viewers->transport().enqueue_if_session(
                    a.circuit->agent_id, a.circuit->session_id,
                    object_properties_family(part_properties(*root, *root), flags),
                    ThrottleOutPacketType::Task, false);
            }
            return;
        }
        if (p.spec->name == "ObjectFlagUpdate") {
            const auto& data = p.block("AgentData");
            if (std::get<Uuid>(data.at("AgentID")) != a.circuit->agent_id ||
                std::get<Uuid>(data.at("SessionID")) != a.circuit->session_id || !policies.can_edit)
                return;
            const auto extra = p.blocks.find("ExtraPhysics");
            auto object =
                world.object(static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("ObjectLocalID"))));
            if (!object)
                return;
            const auto root =
                !object->is_attachment && object->parent_id != 0 ? world.object(object->parent_id) : object;
            if (!root || !policies.can_edit(a.circuit->agent_id, *root))
                return;
            if (extra != p.blocks.end() && !extra->second.empty()) {
                const auto& properties = extra->second.front();
                const auto shape =
                    static_cast<std::uint8_t>(std::get<std::uint64_t>(properties.at("PhysicsShapeType")));
                if (shape != 255) {
                    const auto read = [&](const char* field) {
                        return static_cast<float>(std::get<double>(properties.at(field)));
                    };
                    const auto updated = world.update_extra_physics(
                        object->id, {shape, read("Density"), read("Friction"), read("Restitution"),
                                     read("GravityMultiplier")});
                    if (updated && capability_runtime && capability_runtime->event_queue())
                        capability_runtime->event_queue()->physics_properties(
                            object->local_id, updated->shape, updated->density, updated->friction,
                            updated->restitution, updated->gravity, a.circuit->agent_id);
                    return;
                }
            }
            world.set_group_flags(root->id, std::get<bool>(data.at("UsePhysics")),
                                  std::get<bool>(data.at("IsTemporary")),
                                  std::get<bool>(data.at("IsPhantom")));
            return;
        }
        if (p.spec->name == "ObjectDelink" || p.spec->name == "ObjectLink") {
            const auto& identity = p.block("AgentData");
            if (std::get<Uuid>(identity.at("AgentID")) != a.circuit->agent_id ||
                std::get<Uuid>(identity.at("SessionID")) != a.circuit->session_id || !policies.can_edit)
                return;
            const auto blocks = p.blocks.find("ObjectData");
            if (blocks == p.blocks.end() || blocks->second.empty())
                return;
            const auto part = [&](const auto& block) {
                return world.object(
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID"))));
            };
            if (p.spec->name == "ObjectLink") {
                const auto link_root = [&](std::optional<SceneObject> object) {
                    if (object && !object->is_attachment && object->parent_id != 0)
                        return world.object(object->parent_id);
                    return object;
                };
                const auto root = link_root(part(blocks->second.front()));
                if (!root || !policies.can_edit(a.circuit->agent_id, *root))
                    return;
                std::optional<Uuid> child_owner;
                std::vector<Uuid> children;
                for (std::size_t i = 1; i < blocks->second.size(); ++i) {
                    auto selected = part(blocks->second[i]);
                    if (!selected)
                        continue;
                    if (child_owner && *child_owner != selected->owner)
                        return;
                    child_owner = selected->owner;
                    if (const auto child = link_root(std::move(selected));
                        child && policies.can_edit(a.circuit->agent_id, *child))
                        children.push_back(child->id);
                }
                bool linked = false;
                for (const auto& child : children)
                    linked |= link_prim_with_data(child, root->id);
                if (linked)
                    send_part_properties(root->id, a.circuit->agent_id);
                return;
            }
            std::vector<Uuid> selected_roots;
            for (const auto& block : blocks->second) {
                const auto child = part(block);
                if (!child || child->is_attachment)
                    continue;
                if (child->parent_id == 0) {
                    if (policies.can_edit(a.circuit->agent_id, *child))
                        selected_roots.push_back(child->id);
                    continue;
                }
                const auto root = world.object(child->parent_id);
                if (root && policies.can_edit(a.circuit->agent_id, *root))
                    world.delink_child(child->id, root->id);
            }
            for (const auto& root : selected_roots)
                world.delink_root(root);
            return;
        }
        if (p.spec->name == "ObjectDetach") {
            const auto& identity = p.block("AgentData");
            if (std::get<Uuid>(identity.at("AgentID")) != a.circuit->agent_id ||
                std::get<Uuid>(identity.at("SessionID")) != a.circuit->session_id)
                return;
            const auto blocks = p.blocks.find("ObjectData");
            if (blocks == p.blocks.end())
                return;
            for (const auto& block : blocks->second) {
                const auto local =
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID")));
                const auto object = world.object(local);
                if (object)
                    detach_attachment(a, *object);
            }
            return;
        }
        if (p.spec->name == "DetachAttachmentIntoInv") {
            const auto& request = p.block("ObjectData");
            if (std::get<Uuid>(request.at("AgentID")) != a.circuit->agent_id)
                return;
            const auto item = std::get<Uuid>(request.at("ItemID"));
            if (item.is_zero())
                return;
            for (const auto& object : world.attachments(a.local_id)) {
                if (object.from_inventory_item == item) {
                    detach_attachment(a, object);
                    break;
                }
            }
            return;
        }
        if (p.spec->name == "GrantUserRights") {
            if (!policies.grant_friend_rights || p.blocks.at("Rights").empty())
                return;
            const auto& rights = p.block("Rights");
            const auto principal = a.circuit->agent_id;
            const auto other = std::get<Uuid>(rights.at("AgentRelated"));
            const auto granted =
                static_cast<std::int32_t>(std::get<std::int64_t>(rights.at("RelatedRights")));
            const auto previous = policies.grant_friend_rights(principal, other, granted);
            if (!previous)
                return;
            auto reply = lludp::make_packet("ChangeUserRights");
            reply.block("AgentData")["AgentID"] = principal;
            auto block = lludp::make_block(reply.spec->blocks[1]);
            block["AgentRelated"] = other;
            block["RelatedRights"] = std::int64_t(granted);
            reply.blocks["Rights"].push_back(std::move(block));
            viewers->transport().enqueue_if_session(principal, a.circuit->session_id, reply,
                                                    ThrottleOutPacketType::Task);
            notify_friend_rights(other, principal, *previous, granted);
            return;
        }
        if (p.spec->name == "TerminateFriendship") {
            const auto principal = a.circuit->agent_id;
            const auto other = std::get<Uuid>(p.block("ExBlock").at("OtherID"));
            if (policies.terminate_friendship) {
                if (!policies.terminate_friendship(principal, other))
                    viewers->transport().enqueue_if_session(
                        principal, a.circuit->session_id,
                        agent_alert(principal, "Unable to terminate friendship on this sim.", false),
                        ThrottleOutPacketType::Task);
                notify_friend_termination(principal, other);
                notify_friend_termination(other, principal);
            }
            return;
        }
        if (p.spec->name == "AcceptFriendship" || p.spec->name == "DeclineFriendship") {
            if (policies.answer_friendship)
                policies.answer_friendship(a.circuit->agent_id,
                                           std::get<Uuid>(p.block("TransactionBlock").at("TransactionID")),
                                           p.spec->name == "AcceptFriendship");
            return;
        }
        if (p.spec->name == "RetrieveInstantMessages") {
            if (policies.retrieve_offline_messages) {
                for (auto& message : policies.retrieve_offline_messages(a.circuit->agent_id)) {
                    if (message.recipient != a.circuit->agent_id)
                        continue;
                    constexpr std::uint8_t inventory_offered = 4;
                    if (message.dialog != inventory_offered)
                        message.offline = 1;
                    deliver_instant_message(message, a.circuit->session_id);
                }
            }
            return;
        }
        if (p.spec->name == "ImprovedInstantMessage") {
            const auto& data = p.block("MessageBlock");
            const auto dialog = std::get<std::uint64_t>(data.at("Dialog"));
            if (dialog != 0 && (dialog != 38 || !policies.offer_friendship))
                return;
            RegionInstantMessage message;
            message.sender = a.circuit->agent_id;
            message.recipient = std::get<Uuid>(data.at("ToAgentID"));
            message.session = std::get<Uuid>(data.at("ID"));
            message.region = descriptor.id;
            message.from_group = std::get<bool>(data.at("FromGroup"));
            message.offline = std::get<std::uint64_t>(data.at("Offline")) != 0;
            const auto& position = std::get<std::vector<double>>(data.at("Position"));
            message.position = {static_cast<float>(position.at(0)), static_cast<float>(position.at(1)),
                                static_cast<float>(position.at(2))};
            message.timestamp =
                static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count());
            message.sender_name = framework::utf16_replacing(
                framework::field_to_string(std::get<std::vector<std::uint8_t>>(data.at("FromAgentName"))));
            message.message =
                framework::sdk_bytes_to_string(std::get<std::vector<std::uint8_t>>(data.at("Message")));
            message.bucket = std::get<std::vector<std::uint8_t>>(data.at("BinaryBucket"));
            if (dialog == 38) {
                message.dialog = 38;
                policies.offer_friendship(message);
                return;
            }
            if (!viewers->agent(message.recipient) && policies.forward_instant_message &&
                (!policies.can_instant_message ||
                 policies.can_instant_message(message.sender, message.recipient)))
                policies.forward_instant_message(message);
            else
                deliver_instant_message(message);
            return;
        }

        if (p.spec->name == "RezScript") {
            auto& b = p.block("InventoryBlock");
            auto source_item = std::get<Uuid>(b.at("ItemID"));
            if (!source_item.is_zero()) {
                auto local = static_cast<std::uint32_t>(
                    std::get<std::uint64_t>(p.block("UpdateBlock").at("ObjectLocalID")));
                std::lock_guard lock(commands_mutex);
                inventory_script_commands.push_back({a, source_item, local});
                return;
            }
            auto object = world.object(std::get<Uuid>(b.at("FolderID")));
            if (!object || !policies.can_edit || !policies.can_edit(a.circuit->agent_id, *object))
                return;
            InventoryItem item;
            item.id = Uuid::random();
            item.parent = object->id;
            item.owner = std::get<Uuid>(b.at("OwnerID"));
            item.creator_id = std::get<Uuid>(b.at("CreatorID")).str();
            item.group_id = object->group;
            item.asset_id = Uuid("2074003b-8d5f-40e5-8b20-581c1c50aedb");
            item.asset_type = static_cast<int>(std::get<std::int64_t>(b.at("Type")));
            item.inventory_type = static_cast<int>(std::get<std::int64_t>(b.at("InvType")));
            auto field = [&](const char* name) {
                return framework::field_to_string(std::get<std::vector<std::uint8_t>>(b.at(name)));
            };
            item.name = field("Name");
            item.description = field("Description");
            if (framework::trim_utf8(item.description).empty())
                item.description.clear();
            item.creation_date = static_cast<int>(std::get<std::int64_t>(b.at("CreationDate")));
            item.base_permissions = static_cast<int>(std::get<std::uint64_t>(b.at("BaseMask")));
            item.current_permissions = static_cast<int>(std::get<std::uint64_t>(b.at("OwnerMask")));
            item.everyone_permissions = static_cast<int>(std::get<std::uint64_t>(b.at("EveryoneMask")));
            item.next_permissions = static_cast<int>(std::get<std::uint64_t>(b.at("NextOwnerMask")));
            item.flags = static_cast<int>(std::get<std::uint64_t>(b.at("Flags")));
            std::lock_guard lock(commands_mutex);
            script_commands.push_back({a.circuit->agent_id, object->id, std::move(item), {}});
            return;
        }
        if (p.spec->name == "MoneyBalanceRequest") {
            if (!policies.sample_economy_data)
                return;
            const auto id = a.circuit->agent_id;
            const auto& identity = p.block("AgentData");
            if (std::get<Uuid>(identity.at("AgentID")) != id ||
                std::get<Uuid>(identity.at("SessionID")) != a.circuit->session_id) {
                alert(id, "Unable to send your money balance to you!");
                return;
            }
            auto reply = lludp::make_packet("MoneyBalanceReply");
            auto& money = reply.block("MoneyData");
            money["AgentID"] = id;
            money["TransactionID"] = p.block("MoneyData").at("TransactionID");
            money["TransactionSuccess"] = true;
            viewers->transport().enqueue_if_session(id, a.circuit->session_id, std::move(reply),
                                                    ThrottleOutPacketType::Task);
            return;
        }
        if (p.spec->name == "EconomyDataRequest") {
            if (policies.sample_economy_data)
                viewers->transport().enqueue(a.circuit->agent_id, *policies.sample_economy_data,
                                             ThrottleOutPacketType::Task);
            return;
        }
        if (p.spec->name == "RequestPayPrice") {
            if (!policies.sample_economy_data)
                return;
            const auto id = std::get<Uuid>(p.block("ObjectData").at("ObjectID"));
            auto root = world.object(id);
            if (root && root->parent_id && !root->is_attachment)
                root = world.object(root->parent_id);
            if (!root || std::ranges::all_of(root->pay_prices, [](auto price) { return price == 0; }))
                return;
            auto reply = lludp::make_packet("PayPriceReply");
            reply.block("ObjectData")["ObjectID"] = id;
            reply.block("ObjectData")["DefaultPayPrice"] = std::int64_t(root->pay_prices.front());
            for (std::size_t i = 1; i < root->pay_prices.size(); ++i) {
                auto button = lludp::make_block(reply.spec->blocks.at(1));
                button["PayButton"] = std::int64_t(root->pay_prices[i]);
                reply.blocks["ButtonData"].push_back(std::move(button));
            }
            viewers->transport().enqueue(a.circuit->agent_id, std::move(reply), ThrottleOutPacketType::Task);
            return;
        }
        if (p.spec->name == "SoundTrigger") {
            if (!policies.sound_max_distance || !avatar_sound_allowed(a))
                return;
            auto packet = lludp::make_packet("SoundTrigger");
            auto& block = packet.block("SoundData");
            block = p.block("SoundData");
            const auto id = a.circuit->agent_id;
            block["OwnerID"] = block["ObjectID"] = block["ParentID"] = id;
            const auto& position = std::get<std::vector<double>>(block.at("Position"));
            const auto radius = *policies.sound_max_distance;
            for (const auto& viewer : viewers->agents()) {
                const auto x = viewer.position.x - position[0], y = viewer.position.y - position[1],
                           z = viewer.position.z - position[2];
                if (viewer.root && x * x + y * y + z * z <= radius * radius)
                    viewers->transport().enqueue(viewer.circuit->agent_id, packet,
                                                 ThrottleOutPacketType::Task);
            }
            return;
        }
        if (p.spec->name == "ObjectGrab" || p.spec->name == "ObjectDeGrab" ||
            p.spec->name == "ObjectGrabUpdate") {
            std::optional<SceneObject> object;
            auto& b = p.block("ObjectData");
            if (p.spec->name == "ObjectGrabUpdate")
                object = world.object(std::get<Uuid>(b.at("ObjectID")));
            else
                object = world.object(static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("LocalID"))));
            if (object) {
                script::LslDetector detector;
                detector.key = a.circuit->agent_id;
                detector.owner = detector.key;
                detector.name = a.circuit->name();
                detector.group = a.active_group_id();
                detector.position = {a.position.x, a.position.y, a.position.z};
                detector.velocity = {a.velocity.x, a.velocity.y, a.velocity.z};
                detector.rotation = {a.rotation.x, a.rotation.y, a.rotation.z, a.rotation.w};
                detector.type = 1; // AGENT
                if (a.velocity.x != 0 || a.velocity.y != 0 || a.velocity.z != 0)
                    detector.type |= 2; // ACTIVE
                detector.link_number = world.link_number(object->id);
                if (p.spec->name == "ObjectGrab") {
                    const auto& offset = std::get<std::vector<double>>(b.at("GrabOffset"));
                    detector.grab = {offset.at(0), offset.at(1), offset.at(2)};
                } else if (p.spec->name == "ObjectGrabUpdate") {
                    const auto& position = std::get<std::vector<double>>(b.at("GrabPosition"));
                    const auto part = script_world_position(*object);
                    detector.grab = {position.at(0) - part.x, position.at(1) - part.y, position.at(2) - part.z};
                }
                if (const auto surface = p.blocks.find("SurfaceInfo");
                    surface != p.blocks.end() && !surface->second.empty()) {
                    const auto& hit = surface->second.front();
                    const auto vector = [&](const char* field) {
                        const auto& value = std::get<std::vector<double>>(hit.at(field));
                        return framework::Vector3d{value.at(0), value.at(1), value.at(2)};
                    };
                    detector.touch_face =
                        static_cast<std::int32_t>(std::get<std::int64_t>(hit.at("FaceIndex")));
                    detector.touch_pos = vector("Position");
                    detector.touch_normal = vector("Normal");
                    detector.touch_binormal = vector("Binormal");
                    detector.touch_st = vector("STCoord");
                    detector.touch_uv = vector("UVCoord");
                }
                std::lock_guard lock(commands_mutex);
                script_commands.push_back({a.circuit->agent_id,
                                           object->id,
                                           {},
                                           p.spec->name == "ObjectGrab"     ? "touch_start"
                                           : p.spec->name == "ObjectDeGrab" ? "touch_end"
                                                                            : "touch",
                                           detector});
            }
            return;
        }
        if (p.spec->name == "AgentRequestSit") {
            const auto& target = p.block("TargetObject");
            const auto& offset = std::get<std::vector<double>>(target.at("Offset"));
            request_object_seat(a, std::get<Uuid>(target.at("TargetID")),
                                {float(offset.at(0)), float(offset.at(1)), float(offset.at(2))});
            return;
        }
        if (p.spec->name == "AgentUpdate") {
            constexpr std::uint32_t stand_up = 0x10000u;
            constexpr std::uint32_t sit_on_ground = 0x20000u;
            if (a.seat) {
                if (a.controls & stand_up)
                    stand_from_seat(a);
                return;
            }
            if (a.controls & sit_on_ground) {
                change_ground_sit(a, true);
                return;
            }
            if (a.ground_sitting) {
                if (a.controls & stand_up)
                    change_ground_sit(a, false);
                return;
            }
            if (movement_frozen(a))
                return;
            if (const auto physics = viewer_physics.load())
                physics->input(physics_agent(a));
            else
                for (auto& other : viewers->agents())
                    if (other.root)
                        send_avatar(a, other);
            return;
        }
        if (p.spec->name == "SetAlwaysRun") {
            if (const auto physics = viewer_physics.load())
                physics->running(a);
            return;
        }
        if (p.spec->name == "LandStatRequest") {
            const auto& identity = p.block("AgentData");
            if (std::get<Uuid>(identity.at("AgentID")) != a.circuit->agent_id ||
                std::get<Uuid>(identity.at("SessionID")) != a.circuit->session_id)
                return;
            const auto& data = p.block("RequestData");
            std::lock_guard lock(commands_mutex);
            land_stats_requests.push_back({
                a.circuit->agent_id, a.circuit->session_id,
                static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("ReportType"))),
                static_cast<std::uint32_t>(std::get<std::uint64_t>(data.at("RequestFlags"))),
                static_cast<std::int32_t>(std::get<std::int64_t>(data.at("ParcelLocalID"))),
                framework::utf8(framework::sdk_bytes_to_string(
                    std::get<std::vector<std::uint8_t>>(data.at("Filter"))))});
            return;
        }
        if (p.spec->name == "EstateOwnerMessage") {
            const auto& identity = p.block("AgentData");
            if (std::get<Uuid>(identity.at("AgentID")) != a.circuit->agent_id ||
                std::get<Uuid>(identity.at("SessionID")) != a.circuit->session_id)
                return;
            const auto text = [](const auto& value) {
                return framework::sdk_bytes_to_string(std::get<std::vector<std::uint8_t>>(value));
            };
            const auto parameters = p.blocks.find("ParamList");
            const auto method = text(p.block("MethodData").at("Method"));
            if (method == u"scripts" || method == u"colliders") {
                std::lock_guard lock(commands_mutex);
                land_stats_requests.push_back(
                    {a.circuit->agent_id, a.circuit->session_id, method == u"scripts" ? 0u : 1u, 0, 0, {}});
                return;
            }
            if (method == u"teleporthomeuser" || method == u"teleporthomeallusers") {
                if (!can_edit_estate_region(a))
                    return;
                if (method == u"teleporthomeuser") {
                    Uuid target;
                    if (parameters == p.blocks.end() || parameters->second.size() < 2 ||
                        !framework::try_parse_sdk_uuid(
                            framework::utf8(text(parameters->second[1].at("Parameter"))), target) || target.is_zero())
                        return;
                    const auto viewer = viewers->agent(target);
                    if (viewer && viewer->root)
                        estate_send_home(*viewer, false, false);
                    else if (policies.forward_estate_teleport_home)
                        policies.forward_estate_teleport_home(target);
                } else {
                    for (const auto& viewer : viewers->agents())
                        if (viewer.root && (policies.estate_communications ||
                                            viewer.circuit->agent_id != a.circuit->agent_id))
                            estate_send_home(viewer, false, true);
                    if (policies.forward_estate_teleport_home)
                        policies.forward_estate_teleport_home(std::nullopt);
                }
                return;
            }
            if (method == u"simulatormessage" || method == u"instantmessage") {
                const bool estate = method == u"instantmessage";
                if (!can_edit_estate_region(a) || parameters == p.blocks.end() ||
                    parameters->second.size() < (estate ? 2u : 5u))
                    return;
                const auto parameter = [&](std::size_t index) {
                    return framework::utf8(text(parameters->second[index].at("Parameter")));
                };
                const bool extended = parameters->second.size() >= 5;
                auto sender = a.circuit->agent_id;
                if (extended && !framework::try_parse_sdk_uuid(parameter(2), sender))
                    return;
                const auto name = parameter(extended ? 3 : 0);
                const auto message = parameter(extended ? 4 : 1);
                if (!estate || policies.estate_communications)
                    send_region_notice(message);
                if (estate && policies.forward_estate_message)
                    policies.forward_estate_message(sender, name, message);
                return;
            }
            if (method == u"getinfo" || method == u"estatechangecovenantid" || method == u"telehub" ||
                method == u"estateaccessdelta") {
                const auto id = a.circuit->agent_id;
                if (!a.root || (!policies.bypass_estate_permissions && !is_estate_manager(id) &&
                                moderation_level(id, &a) < 200))
                    return;
                if (method == u"getinfo") {
                    send_estate_details(a, std::get<Uuid>(p.block("MethodData").at("Invoice")));
                    return;
                }
                if (parameters == p.blocks.end() || parameters->second.empty())
                    return;
                if (method == u"telehub") {
                    const auto command = text(parameters->second.front().at("Parameter"));
                    std::uint32_t parameter = 0;
                    if (parameters->second.size() > 1)
                        framework::try_parse_unsigned_integer(
                            framework::utf8(text(parameters->second[1].at("Parameter"))), parameter);
                    try {
                        estate_telehub(a, command, parameter);
                    } catch (const std::exception& failure) {
                        error(std::string("Telehub update failed: ") + failure.what());
                    }
                    return;
                }
                if (method == u"estateaccessdelta") {
                    if (parameters->second.size() < 3)
                        return;
                    std::int32_t flags;
                    Uuid target;
                    if (services::parse_account_integer(
                            framework::utf8(text(parameters->second[1].at("Parameter"))), flags) &&
                        flags >= INT16_MIN && flags <= INT16_MAX &&
                        framework::try_parse_sdk_uuid(
                            framework::utf8(text(parameters->second[2].at("Parameter"))), target))
                        edit_estate_access(a, std::get<Uuid>(p.block("MethodData").at("Invoice")), flags,
                                           target);
                    return;
                }
                try {
                    for (const auto& parameter : parameters->second) {
                        Uuid covenant;
                        if (!framework::try_parse_sdk_uuid(framework::utf8(text(parameter.at("Parameter"))),
                                                           covenant))
                            break;
                        const auto timestamp = static_cast<std::uint32_t>(
                            std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count());
                        {
                            std::lock_guard lock(estate_covenant_mutex);
                            if (policies.persist_estate_covenant)
                                policies.persist_estate_covenant(covenant, timestamp);
                            estate_covenant = covenant;
                            estate_covenant_timestamp = timestamp;
                        }
                        notify_estate_region_change();
                    }
                } catch (const std::exception& e) {
                    error(std::string("Estate covenant update failed: ") + e.what());
                }
                return;
            }
            if ((method != u"terrain" && method != u"textureheights" && method != u"texturedetail" &&
                 method != u"setregionterrain" && method != u"setregioninfo" && method != u"setregiondebug") ||
                parameters == p.blocks.end() || parameters->second.empty())
                return;
            const auto agent = a.circuit->agent_id;
            const bool administrator = !a.circuit->verified_foreign_login && god_rights_level(agent) >= 200;
            const bool bypass = policies.bypass_estate_permissions;
            if (!bypass && !administrator && !is_estate_manager(agent))
                return;
            if (method == u"setregioninfo" || method == u"setregiondebug") {
                const bool debug = method == u"setregiondebug";
                if (parameters->second.size() < (debug ? 3u : 9u))
                    return;
                const auto parameter = [&](std::size_t index) {
                    return text(parameters->second[index].at("Parameter"));
                };
                const auto enabled = [&](std::size_t index) {
                    const auto value = framework::invariant_lower_utf16(parameter(index));
                    return value == u"1" || value == u"y" || value == u"yes" || value == u"t" || value == u"true";
                };
                if (debug) {
                    try {
                        edit_estate_region_settings([&](EstateRegionSettings& settings) {
                            settings.flags = (settings.flags & ~0x7000u) |
                                (enabled(0) ? 0x2000u : 0u) | (enabled(1) ? 0x1000u : 0u) |
                                (enabled(2) ? 0x4000u : 0u);
                        }, false);
                    } catch (const std::exception& failure) {
                        error(std::string("Estate debug settings update failed: ") + failure.what());
                    }
                    return;
                }
                double agents = 0;
                float bonus = 0;
                std::int32_t access = 0;
                if (!network::try_parse_invariant_real(framework::utf8(parameter(4)), agents) ||
                    !std::isfinite(agents) ||
                    !framework::parse_invariant_float(framework::utf8(parameter(5)), bonus) ||
                    !std::isfinite(bonus) ||
                    !services::parse_account_integer(framework::utf8(parameter(6)), access) ||
                    access < -32768 || access > 32767)
                    return;
                agents = std::nearbyint(agents);
                if (agents < -32768 || agents > 32767)
                    return;
                constexpr std::uint32_t edited = 0x40u | 0x80000u | 0x1u | 0x80u | 0x400000u | 0x4000000u;
                const std::uint32_t flags = (enabled(0) ? 0x40u : 0u) | (enabled(1) ? 0x80000u : 0u) |
                    (enabled(2) ? 0x1u : 0u) | (enabled(3) ? 0u : 0x80u) |
                    (enabled(7) ? 0x400000u : 0u) | (enabled(8) ? 0x4000000u : 0u);
                try {
                    edit_estate_region_settings([&](EstateRegionSettings& settings) {
                        settings.flags = (settings.flags & ~edited) | flags;
                        settings.agent_limit = estate_agent_limit(static_cast<std::int32_t>(agents));
                        settings.maturity = access <= 13 ? 0 : access <= 21 ? 1 : 2;
                        settings.object_bonus = bonus;
                    });
                } catch (const std::exception& failure) {
                    error(std::string("Estate region settings update failed: ") + failure.what());
                    return;
                }
                return;
            }
            if (method == u"setregionterrain") {
                if (parameters->second.size() != 9)
                    return;
                const auto parse = [&](std::size_t index, float& value) {
                    return framework::parse_invariant_float(
                        framework::utf8(text(parameters->second[index].at("Parameter"))), value) &&
                        std::isfinite(value);
                };
                float water = 0, raise = 0, lower = 0, sun = 0, estate_sun = 0;
                if (!parse(0, water) || !parse(1, raise) || !parse(2, lower) ||
                    !parse(5, sun) || !parse(8, estate_sun))
                    return;
                water = std::clamp(water, 0.f, 8000.f);
                raise = std::clamp(raise, 0.f, 200.f);
                lower = std::clamp(lower, -200.f, 0.f);
                update_region_terrain_settings(water, TerrainLimits{raise, lower});
                return;
            }
            if (method == u"textureheights" || method == u"texturedetail") {
                const bool terrain_pbr = method == u"texturedetail" && supports_terrain_pbr(a);
                for (const auto& parameter : parameters->second) {
                    const auto value = framework::utf8(text(parameter.at("Parameter")));
                    const auto first = value.find(' ');
                    if (first == value.npos)
                        continue;
                    int corner = 0;
                    const auto parsed = std::from_chars(value.data(), value.data() + first, corner);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + first ||
                        corner < 0 || corner >= 4)
                        continue;
                    const auto arguments = std::string_view(value).substr(first + 1);
                    const auto separator = arguments.find(' ');
                    try {
                        std::lock_guard lock(terrain_texture_mutex);
                        if (method == u"texturedetail") {
                            Uuid texture;
                            if (separator != arguments.npos ||
                                !framework::try_parse_sdk_uuid(arguments, texture) || texture.is_zero())
                                continue;
                            if (policies.persist_terrain_texture_asset)
                                policies.persist_terrain_texture_asset(terrain_pbr, corner, texture);
                            auto& assets = terrain_pbr ? terrain_pbr_materials : terrain_texture_assets;
                            assets[corner] = texture;
                        } else {
                            float low = 0, high = 0;
                            if (separator == arguments.npos ||
                                arguments.find(' ', separator + 1) != arguments.npos ||
                                !framework::parse_invariant_float(arguments.substr(0, separator), low) ||
                                !framework::parse_invariant_float(arguments.substr(separator + 1), high))
                                continue;
                            if (policies.persist_terrain_texture_heights)
                                policies.persist_terrain_texture_heights(corner, low, high);
                            terrain_texture_heights.low[corner] = low;
                            terrain_texture_heights.high[corner] = high;
                        }
                    } catch (const std::exception& failure) {
                        error(std::string("Terrain texture update failed: ") + failure.what());
                        continue;
                    }
                    notify_estate_region_change();
                    for (const auto& viewer : viewers->agents())
                        send_terrain_handshake(*viewer.circuit, supports_terrain_pbr(viewer));
                }
                return;
            }
            const auto operation = text(parameters->second.front().at("Parameter"));
            try {
                if (operation == u"bake") {
                    if (bypass || administrator || agent == descriptor.estate_owner)
                        bake_terrain();
                }
                else if (operation == u"upload filename" && parameters->second.size() > 1)
                    start_terrain_upload(a, framework::utf8(text(parameters->second[1].at("Parameter"))));
                else if (operation == u"download filename" && parameters->second.size() > 1) {
                    const Terrain snapshot(world.terrain());
                    const auto heights = snapshot.serialized_floats();
                    if (std::any_of(heights.begin(), heights.end(),
                                    [](float height) { return height >= 4095.f; })) {
                        alert(a.circuit->agent_id, "Terrain error");
                        return;
                    }
                    auto data = std::make_shared<const std::vector<std::uint8_t>>(snapshot.llraw());
                    const auto filename = Uuid::random().str();
                    if (!xfer->add(filename, data)) {
                        alert(a.circuit->agent_id, "Terrain error");
                        return;
                    }
                    alert(a.circuit->agent_id, "Terrain file written, starting download...");
                    auto reply = lludp::make_packet("InitiateDownload");
                    reply.block("AgentData")["AgentID"] = a.circuit->agent_id;
                    auto& files = reply.block("FileData");
                    std::vector<std::uint8_t> wire_name(filename.begin(), filename.end());
                    wire_name.push_back(0);
                    files["SimFilename"] = std::move(wire_name);
                    const auto viewer_name = framework::utf8(text(parameters->second[1].at("Parameter")));
                    std::vector<std::uint8_t> wire_viewer_name(viewer_name.begin(), viewer_name.end());
                    if (!wire_viewer_name.empty())
                        wire_viewer_name.push_back(0);
                    files["ViewerFilename"] = std::move(wire_viewer_name);
                    viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id,
                                                            std::move(reply), ThrottleOutPacketType::Asset);
                }
            } catch (const std::exception& error) {
                if (policies.log)
                    policies.log(error.what());
            }
            return;
        }
        if (p.spec->name == "ModifyLand") {
            if (!a.root)
                return;
            std::lock_guard lock(terrain_edit_mutex);
            const auto now = std::chrono::steady_clock::now();
            if (now < next_terrain_edit)
                return;
            const auto& modify = p.block("ModifyBlock");
            const auto action = std::get<std::uint64_t>(modify.at("Action"));
            if (action > 5)
                return;
            const auto strength = static_cast<float>(std::get<double>(modify.at("Seconds")));
            const auto target = static_cast<float>(std::get<double>(modify.at("Height")));
            const auto parcels = p.blocks.find("ParcelData"),
                       extensions = p.blocks.find("ModifyBlockExtended");
            if (!std::isfinite(strength) || (action == 0 && !std::isfinite(target)) ||
                (action == 5 && (strength < 0 || !terrain_baseline)) || parcels == p.blocks.end() ||
                parcels->second.empty() || extensions == p.blocks.end() || extensions->second.empty())
                return;
            auto& active_terrain = world.terrain();
            Terrain terrain(active_terrain);
            const auto agent = a.circuit->agent_id;
            const bool administrator = !a.circuit->verified_foreign_login && god_rights_level(agent) >= 200;
            const bool estate_override = policies.bypass_object_permissions || is_estate_manager(agent) ||
                                         administrator;
            std::map<Uuid, bool> parcel_permissions;
            const auto can_edit = [&](const std::shared_ptr<const ScriptParcel>& parcel) {
                if (estate_override)
                    return true;
                if (!parcel)
                    return false;
                if (const auto cached = parcel_permissions.find(parcel->id); cached != parcel_permissions.end())
                    return cached->second;
                constexpr std::uint32_t allow_terraform = 1u << 4;
                constexpr std::uint64_t allow_edit_land = 1ull << 23;
                const bool allowed = (parcel->flags & allow_terraform) || parcel->owner == agent ||
                    (parcel->group_owned && !parcel->group.is_zero() && policies.inventory_group_powers &&
                     (policies.inventory_group_powers(agent, parcel->group) & allow_edit_land));
                parcel_permissions.emplace(parcel->id, allowed);
                return allowed;
            };
            const auto blocks = std::min(parcels->second.size(), extensions->second.size());
            for (std::size_t block = 0; block < blocks; ++block) {
                const auto& parcel = parcels->second[block];
                const auto x = static_cast<float>(std::get<double>(parcel.at("West")));
                const auto y = static_cast<float>(std::get<double>(parcel.at("South")));
                const auto east = std::get<double>(parcel.at("East")),
                           north = std::get<double>(parcel.at("North"));
                const bool area = x != east || y != north;
                const auto size =
                    static_cast<float>(std::get<double>(extensions->second[block].at("BrushSize")));
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(east) || !std::isfinite(north) ||
                    (!area && (!std::isfinite(size) || size <= 0)))
                    continue;
                const auto parcel_id = static_cast<int>(std::get<std::int64_t>(parcel.at("LocalID")));
                const bool parcel_area = area && parcel_id != -1;
                if (parcel_area && (!policies.script_parcel_at || !can_edit(parcel_by_local_id(parcel_id))))
                    continue;
                const auto bound = [](double value, int max) {
                    return static_cast<int>(std::clamp(value, 0.0, double(max)));
                };
                const auto max_x = static_cast<int>(active_terrain.width()) - 1,
                           max_y = static_cast<int>(active_terrain.height()) - 1;
                if (!area && (double(x) + size + .5 < 0 || double(y) + size + .5 < 0 ||
                              double(x) - size + .5 > max_x || double(y) - size + .5 > max_y))
                    continue;
                const int start_x = bound(area ? x : double(x) - size + .5, max_x),
                          end_x = bound(area ? east : double(x) + size + .5, max_x);
                const int start_y = bound(area ? y : double(y) - size + .5, max_y),
                          end_y = bound(area ? north : double(y) + size + .5, max_y);
                const float scale = area ? 0.f : .5f * std::numbers::pi_v<float> / size;
                const int sample_radius = area ? 0 : static_cast<int>(std::min(4.0, double(size) + .5));
                const auto neighborhood = [&](int px, int py, int radius_x, int radius_y) {
                    float sum = 0;
                    int samples = 0;
                    for (int sx = std::max(0, px - radius_x); sx <= std::min(max_x, px + radius_x); ++sx)
                        for (int sy = std::max(0, py - radius_y); sy <= std::min(max_y, py + radius_y);
                             ++sy) {
                            sum += terrain.get(sx, sy);
                            ++samples;
                        }
                    return std::pair{sum, samples};
                };
                std::vector<std::tuple<int, int, float>> smoothed;
                bool allowed = false;
                for (int px = start_x; px <= end_x; ++px)
                    for (int py = start_y; py <= end_y; ++py) {
                        if (parcel_area || !estate_override) {
                            const auto land = policies.script_parcel_at
                                ? policies.script_parcel_at({static_cast<float>(px), static_cast<float>(py), 0})
                                : nullptr;
                            if (parcel_area ? !land || land->local_id != parcel_id : !can_edit(land))
                                continue;
                        }
                        allowed = true;
                        const float dx = px - x, dy = py - y;
                        const float squared = dx * dx + dy * dy;
                        const auto old = terrain.get(px, py);
                        float height;
                        if (area) {
                            switch (action) {
                            case 0: {
                                const auto blend = strength * .04f;
                                if (blend < .001f)
                                    continue;
                                height = blend >= .999f ? target : old * (1.f - blend) + target * blend;
                                break;
                            }
                            case 1:
                                height = old + strength;
                                break;
                            case 2:
                                height = std::max(0.f, old - strength);
                                break;
                            case 3: {
                                const auto blend = std::min(strength * .002f, 1.f);
                                if (blend < .0001f)
                                    continue;
                                const auto [sum, samples] =
                                    neighborhood(px, py, std::min(4, (end_x - start_x + 1) / 2),
                                                 std::min(4, (end_y - start_y + 1) / 2));
                                height = (1.f - blend) * old + blend * sum / samples;
                                break;
                            }
                            case 4: {
                                const auto blend = strength * .08f;
                                if (blend < .0001f)
                                    continue;
                                height = std::max(
                                    0.f,
                                    old + terrain_noise(px, py, terrain.width(), terrain.height()) * blend);
                                break;
                            }
                            case 5: {
                                const auto blend = strength * 2.f;
                                if (blend < .0001f)
                                    continue;
                                const auto baseline = terrain_baseline->get(px, py);
                                height = blend >= .999f ? baseline : old * (1.f - blend) + baseline * blend;
                                break;
                            }
                            default:
                                continue;
                            }
                        } else if (action == 3) {
                            const float distance = squared / (size * size);
                            if (!(distance <= 1.f))
                                continue;
                            const auto [sum, samples] = neighborhood(px, py, sample_radius, sample_radius);
                            const float blend = std::min(strength, 1.f) * (1.f - distance);
                            height = old - blend * (old - sum / samples);
                            if (height > 0.f && std::isfinite(height) && height != old)
                                smoothed.emplace_back(px, py, std::clamp(height, -100.f, 4000.f));
                            continue;
                        } else if (action == 5) {
                            const float distance = squared / size;
                            if (distance > 1.f)
                                continue;
                            const float blend = std::min(strength, 1.f) * (1.f - distance);
                            height = old * (1.f - blend) + terrain_baseline->get(px, py) * blend;
                        } else if (action == 4) {
                            const float distance = squared / (size * size);
                            if (!(distance <= 1.f))
                                continue;
                            const float blend = strength * (1.f - distance);
                            height = old + terrain_noise(px, py, terrain.width(), terrain.height()) * blend;
                        } else if (action == 0) {
                            const float distance = squared / (2.f * size * size);
                            if (distance > 1.f)
                                continue;
                            const float blend = strength * (1.f - distance);
                            height = blend >= 1.f ? target : old + (target - old) * blend;
                        } else {
                            const float factor =
                                static_cast<float>(std::cos(std::sqrt(double(squared)) * scale));
                            if (factor <= 0)
                                continue;
                            height = action == 1 ? old + factor * strength
                                                 : std::max(0.f, old - factor * strength);
                        }
                        if (!std::isfinite(height))
                            continue;
                        terrain.set(px, py, std::clamp(height, -100.f, 4000.f));
                    }
                for (const auto& [px, py, height] : smoothed)
                    terrain.set(px, py, height);
                if (allowed && terrain_baseline && !administrator) {
                    constexpr int patch = Terrain::patch_size;
                    for (int patch_x = start_x; patch_x <= end_x; patch_x += patch)
                        for (int patch_y = start_y; patch_y <= end_y; patch_y += patch) {
                            if (!terrain.tainted_at(patch_x, patch_y))
                                continue;
                            for (int px = patch_x; px <= std::min(max_x, patch_x + patch - 1); ++px)
                                for (int py = patch_y; py <= std::min(max_y, patch_y + patch - 1); ++py) {
                                    const float baked = terrain_baseline->get(px, py);
                                    const float current = terrain.get(px, py);
                                    const float delta = current - baked;
                                    if (delta > terrain_raise_limit)
                                        terrain.set(px, py, baked + terrain_raise_limit);
                                    else if (delta < terrain_lower_limit)
                                        terrain.set(px, py, baked + terrain_lower_limit);
                                }
                        }
                }
            }
            active_terrain.replace_heights(terrain);
            next_terrain_edit = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
            return;
        }
        if (p.spec->name == "TeleportCancel") {
            const auto& info = p.block("Info");
            if (policies.disable_teleport_cancellation ||
                std::get<Uuid>(info.at("AgentID")) != a.circuit->agent_id ||
                std::get<Uuid>(info.at("SessionID")) != a.circuit->session_id)
                return;
            std::optional<PendingTeleport> cancelled;
            {
                std::lock_guard lock(teleport_mutex);
                const auto transfer =
                    std::find_if(pending_teleports.begin(), pending_teleports.end(), [&](const auto& entry) {
                        return entry.agent == a.circuit->agent_id && entry.session == a.circuit->session_id;
                    });
                if (transfer == pending_teleports.end() || transfer->confirmed ||
                    (!transfer->requires_callback && policies.teleport_arrived &&
                     policies.teleport_arrived(transfer->session, transfer->destination.id)))
                    return;
                cancelled = std::move(*transfer);
                pending_teleports.erase(transfer);
            }
            if (policies.cancel_teleport_destination)
                policies.cancel_teleport_destination(cancelled->destination, cancelled->agent,
                                                     cancelled->session);
            finish_failed_estate_ejection(*cancelled);
            return;
        }
        if (p.spec->name == "TeleportLocationRequest") {
            const auto local_handle = (std::uint64_t(descriptor.x) << 32) | std::uint32_t(descriptor.y);
            const auto requested = std::get<std::uint64_t>(p.block("Info").at("RegionHandle"));
            if (requested != local_handle) {
                begin_remote_teleport(a, p);
                return;
            }
            if (const auto physics = viewer_physics.load())
                physics->teleported(physics_agent(a));
            for (auto& other : viewers->agents())
                if (other.root)
                    send_avatar(a, other);
            return;
        }
        if (p.spec->name == "ObjectAdd") {
            auto request = object_add_request(p);
            if (request.object.shape.pcode != 9 || request.object.shape.state != 0) {
                alert(a.circuit->agent_id, "This native region does not yet support this rez profile.");
                return;
            }
            auto finite = [](Vector3 v) {
                return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
            };
            if (!finite(request.ray_start) || !finite(request.ray_end) || !finite(request.object.scale) ||
                request.object.scale.x <= 0 || request.object.scale.y <= 0 || request.object.scale.z <= 0)
                return;
            RezLocationRequest ray;
            ray.ray_start = request.ray_start;
            ray.ray_end = request.ray_end;
            ray.target = request.ray_target;
            ray.rotation = request.object.rotation;
            ray.bypass_raycast = request.bypass_raycast;
            ray.ray_end_is_intersection = request.ray_end_is_intersection;
            request.object.position = get_new_rez_location(ray, rez_geometry());
            std::string reason;
            if (!policies.can_rez ||
                !policies.can_rez(a.circuit->agent_id, request.object.position, reason)) {
                alert(a.circuit->agent_id, "You cannot create objects here.");
                return;
            }
            if (policies.load_preferences) {
                const auto prefs = policies.load_preferences(a.circuit->agent_id);
                if (prefs.next_owner != 0) {
                    request.object.group_mask = static_cast<std::uint32_t>(prefs.group);
                    request.object.everyone_mask = static_cast<std::uint32_t>(prefs.everyone);
                    request.object.next_mask = static_cast<std::uint32_t>(prefs.next_owner);
                }
            }
            std::lock_guard lock(objects_mutex);
            auto created = world.add(request.object);
            if (created)
                initialize_object_update(created->id, (request.add_flags & 2u) != 0);
            return;
        }
        if (p.spec->name == "ObjectSelect" || p.spec->name == "ObjectDeselect") {
            const bool selecting = p.spec->name == "ObjectSelect";
            for (const auto& block : p.blocks.at("ObjectData")) {
                const auto object = world.object(
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID"))));
                if (!object)
                    continue;
                const auto agent = a.circuit->agent_id;
                if (!selecting) {
                    if (world.set_selected(object->id, false) && !object->is_attachment &&
                        policies.parcel_prim_count_tainted)
                        policies.parcel_prim_count_tainted();
                    if (!object->is_attachment && object->angular_velocity != Vector3{})
                        broadcast_terse(*object);
                    continue;
                }
                if (object->owner != agent)
                    send_object(*object, a);
                constexpr std::uint32_t selectable_flags = 0x100u | 0x4u | 0x10000000u;
                const auto flags =
                    policies.client_flags ? policies.client_flags(agent, *object) : object->flags;
                if ((flags & selectable_flags) && world.set_selected(object->id, true) &&
                    policies.parcel_prim_count_tainted)
                    policies.parcel_prim_count_tainted();
                send_part_properties(object->id, agent);
            }
            return;
        }
        if (p.spec->name == "ObjectImage") {
            std::lock_guard lock(texture_sequences_mutex);
            auto& ordering = texture_sequences[a.circuit->session_id];
            const auto now = std::chrono::steady_clock::now();
            if (now - ordering.last_packet > std::chrono::seconds(30))
                ordering.by_object.clear();
            ordering.last_packet = now;
            for (const auto& block : p.blocks.at("ObjectData")) {
                const auto local =
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID")));
                const auto [last, inserted] = ordering.by_object.try_emplace(local, p.sequence);
                if (!inserted && p.sequence <= last->second)
                    continue;
                last->second = p.sequence;
                const auto object = world.object(local);
                if (!object || !can_edit_linkset(a.circuit->agent_id, *object))
                    continue;
                try {
                    world.update_texture(object->id,
                                         std::get<std::vector<std::uint8_t>>(block.at("TextureEntry")));
                } catch (const std::out_of_range&) {
                    // A truncated texture entry cannot be applied.
                }
            }
            return;
        }
        if (p.spec->name == "ObjectExtraParams") {
            for (const auto& block : p.blocks.at("ObjectData")) {
                const auto object = world.object(
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID"))));
                if (!object || !can_edit_linkset(a.circuit->agent_id, *object))
                    continue;
                world.update_extra_param(
                    object->id, static_cast<std::uint16_t>(std::get<std::uint64_t>(block.at("ParamType"))),
                    std::get<bool>(block.at("ParamInUse")),
                    std::get<std::vector<std::uint8_t>>(block.at("ParamData")));
            }
            return;
        }
        if (p.spec->name == "ObjectShape") {
            for (const auto& block : p.blocks.at("ObjectData")) {
                const auto local =
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID")));
                const auto object = world.object(local);
                if (!object || !can_edit_linkset(a.circuit->agent_id, *object))
                    continue;
                PrimShape shape;
                read_prim_shape_fields(shape, block);
                world.update_shape(object->id, shape);
            }
            return;
        }
        if (p.spec->name == "ObjectMaterial") {
            for (const auto& block : p.blocks.at("ObjectData")) {
                const auto local =
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID")));
                const auto object = world.object(local);
                if (!object || !can_edit_linkset(a.circuit->agent_id, *object))
                    continue;
                const auto material = world.update_material(
                    object->id, static_cast<std::uint8_t>(std::get<std::uint64_t>(block.at("Material"))));
                if (material && capability_runtime && capability_runtime->event_queue())
                    if (const auto updated = world.object(object->id)) {
                        const auto& physics = updated->physics;
                        capability_runtime->event_queue()->physics_properties(
                            local, physics.shape, physics.density, physics.friction, physics.restitution,
                            physics.gravity, a.circuit->agent_id);
                    }
            }
            return;
        }
        if (p.spec->name == "ObjectClickAction") {
            for (const auto& block : p.blocks.at("ObjectData")) {
                const auto object = world.object(
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID"))));
                if (!object || !can_edit_linkset(a.circuit->agent_id, *object))
                    continue;
                world.update_click_action(
                    object->id, static_cast<std::uint8_t>(std::get<std::uint64_t>(block.at("ClickAction"))));
            }
            return;
        }
        if (p.spec->name == "ObjectName" || p.spec->name == "ObjectDescription") {
            const bool description = p.spec->name == "ObjectDescription";
            for (const auto& block : p.blocks.at("ObjectData")) {
                const auto object =
                    world.object(static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("LocalID"))));
                if (!object || !can_edit_linkset(a.circuit->agent_id, *object))
                    continue;
                auto value = framework::field_to_string(
                    std::get<std::vector<std::uint8_t>>(block.at(description ? "Description" : "Name")));
                const auto line_end = value.find_first_of("\r\n");
                if (line_end != std::string::npos)
                    value.resize(line_end);
                if (description && value.size() > 127)
                    value.resize(127);
                world.update_object_text(object->id, description, std::move(value));
            }
            return;
        }
        if (p.spec->name == "MultipleObjectUpdate") {
            for (const auto& block : p.blocks.at("ObjectData")) {
                const auto local =
                    static_cast<std::uint32_t>(std::get<std::uint64_t>(block.at("ObjectLocalID")));
                auto object = world.object(local);
                if (!object) {
                    auto kill = lludp::make_packet("KillObject");
                    auto missing = lludp::make_block(kill.spec->blocks.at(0));
                    missing["ID"] = std::uint64_t(local);
                    kill.blocks["ObjectData"].push_back(std::move(missing));
                    viewers->transport().enqueue_if_session(a.circuit->agent_id, a.circuit->session_id,
                                                            std::move(kill), ThrottleOutPacketType::Task);
                    continue;
                }
                const auto type = std::get<std::uint64_t>(block.at("Type"));
                switch (type) {
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 0x14:
                case 0x15:
                case 9:
                case 10:
                case 11:
                case 12:
                case 13:
                case 0x1c:
                case 0x1d:
                    break;
                default:
                    continue;
                }
                const auto group = !object->is_attachment && object->parent_id != 0
                                       ? world.object(object->parent_id)
                                       : object;
                if (!group)
                    continue;
                const auto& bytes = std::get<std::vector<std::uint8_t>>(block.at("Data"));
                const bool position = (type & 1) != 0;
                const bool rotation = (type & 2) != 0;
                const bool requested_scale = (type & 4) != 0;
                const auto actor = a.circuit->agent_id;
                const bool editable = policies.can_edit && policies.can_edit(actor, *group);
                if (!editable && (!(type & 8) || !(position || rotation) || !policies.can_move ||
                                  !policies.can_move(actor, *group)))
                    continue;
                const bool group_scale = type == 0x1c || type == 0x1d;
                if ((type >= 9 && type <= 11) || (type == 13 && !editable) || group_scale)
                    object = group;
                const bool scale = requested_scale && editable;
                if (bytes.size() < (position && (rotation || requested_scale) ? 24u : 12u))
                    continue;
                std::size_t offset = 0;
                const auto read_float = [&] {
                    std::uint32_t bits = 0;
                    for (unsigned shift = 0; shift < 32; shift += 8)
                        bits |= std::uint32_t(bytes[offset++]) << shift;
                    return std::bit_cast<float>(bits);
                };
                const auto read_vector = [&] { return Vector3{read_float(), read_float(), read_float()}; };
                const auto finite = [](Vector3 value) {
                    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
                };
                Vector3 new_position{};
                if (position) {
                    new_position = read_vector();
                    if (!finite(new_position))
                        continue;
                }
                if (rotation) {
                    const auto xyz = read_vector();
                    if (!finite(xyz))
                        continue;
                    const float remainder = 1.f - xyz.x * xyz.x - xyz.y * xyz.y - xyz.z * xyz.z;
                    object->rotation = {xyz.x, xyz.y, xyz.z, remainder > 1e-6f ? std::sqrt(remainder) : 0.f};
                }
                if (scale) {
                    object->scale = read_vector();
                    if (!finite(object->scale))
                        continue;
                    if (!group_scale)
                        object->scale = clamp_prim_scale(object->scale);
                }
                const bool group_position = (type & 8) && (type != 13 || !editable);
                const bool apply_position =
                    position && (!group_position || allow_group_position(*object, new_position));
                if (!apply_position && !rotation && !scale)
                    continue;
                if (group_scale && scale) {
                    resize_linkset(*object, object->scale,
                                   apply_position ? std::optional{new_position} : std::nullopt);
                    continue;
                }
                world.patch_transform(object->id, apply_position ? std::optional{new_position} : std::nullopt,
                                      rotation ? std::optional{object->rotation} : std::nullopt,
                                      scale ? std::optional{object->scale} : std::nullopt,
                                      !(type & 8) || ((type == 12 || type == 13) && editable));
            }
            return;
        }
        if (p.spec->name == "ObjectPosition" || p.spec->name == "ObjectScale" ||
            p.spec->name == "ObjectRotation")
            for (auto& b : p.blocks.at("ObjectData")) {
                auto object =
                    world.object(static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("ObjectLocalID"))));
                if (object && !object->is_attachment && object->parent_id != 0)
                    object = world.object(object->parent_id);
                const auto& permission = p.spec->name == "ObjectScale" || !policies.can_move
                                             ? policies.can_edit
                                             : policies.can_move;
                if (!object || !permission || !permission(a.circuit->agent_id, *object))
                    continue;
                const char* field = p.spec->name == "ObjectPosition" ? "Position"
                                    : p.spec->name == "ObjectScale"  ? "Scale"
                                                                     : "Rotation";
                auto& values = std::get<std::vector<double>>(b.at(field));
                if (!std::all_of(values.begin(), values.end(), [](double x) { return std::isfinite(x); }))
                    continue;
                if (p.spec->name == "ObjectPosition" &&
                    !allow_group_position(*object,
                                          {static_cast<float>(values.at(0)), static_cast<float>(values.at(1)),
                                           static_cast<float>(values.at(2))}))
                    continue;
                if (p.spec->name == "ObjectRotation")
                    object->rotation = {static_cast<float>(values.at(0)), static_cast<float>(values.at(1)),
                                        static_cast<float>(values.at(2)), static_cast<float>(values.at(3))};
                else {
                    Vector3 v{static_cast<float>(values.at(0)), static_cast<float>(values.at(1)),
                              static_cast<float>(values.at(2))};
                    if (p.spec->name == "ObjectScale") {
                        resize_linkset(*object, v);
                        continue;
                    }
                    object->position = v;
                }
                world.patch_transform(
                    object->id,
                    p.spec->name == "ObjectPosition" ? std::optional{object->position} : std::nullopt,
                    p.spec->name == "ObjectRotation" ? std::optional{object->rotation} : std::nullopt,
                    std::nullopt);
            }
    }
    void script_chat(const script::LslChat& chat) {
        if (chat.channel != 0 && chat.type == script::LslChatType::Say)
            return;
        auto object = world.object(chat.context.object);
        if (!object)
            return;
        ChatMessage message;
        message.message = chat.text;
        message.from_name = object->name;
        message.source = object->id;
        message.owner = object->owner;
        message.position = script_world_position(*object);
        const bool debug_channel = chat.type == script::LslChatType::Debug ||
                                   (chat.type == script::LslChatType::Owner &&
                                    chat.channel == std::numeric_limits<std::int32_t>::max());
        message.type = debug_channel ? 6 : chat.type == script::LslChatType::Owner ? 8 : 1;
        if (chat.type == script::LslChatType::Debug && !object->is_attachment && object->parent_id)
            if (const auto root = world.object(object->parent_id))
                message.position = script_world_position(*root);
        for (auto& recipient : viewers->agents())
            if (recipient.root || chat.type == script::LslChatType::Debug) {
                if (chat.type == script::LslChatType::Owner) {
                    if (recipient.circuit->agent_id != object->owner)
                        continue;
                } else if (chat.type == script::LslChatType::Say) {
                    float x = recipient.position.x - message.position.x,
                          y = recipient.position.y - message.position.y,
                          z = recipient.position.z - message.position.z;
                    if (x * x + y * y + z * z > 400.f)
                        continue;
                }
                viewers->transport().send(recipient.circuit->agent_id, chat_packet(message));
            }
    }
    void send_land_stats(const LandStatsRequest& request, std::uint32_t type) {
        constexpr std::uint32_t filter_by_parcel_id = 1u;
        constexpr std::uint32_t filter_by_owner_name = 2u;
        constexpr std::uint32_t filter_by_object_name = 4u;
        constexpr std::uint32_t filter_by_parcel_name = 8u;
        constexpr double minimum_script_time_ms = 0.001;
        constexpr std::int32_t minimum_script_heap_bytes = 1024;
        constexpr std::size_t report_limit = 100;
        struct ScriptStats {
            double time_ms = 0;
            std::int64_t heap_bytes = 0;
        };
        struct Row {
            SceneObject object;
            Vector3 position;
            ScriptStats stats;
            std::string owner;
            std::string parcel;
            std::int32_t url_count;
        };
        std::vector<Row> rows;
        if (type == 0) {
            std::map<Uuid, std::int32_t> url_counts;
            {
                std::lock_guard lock(script_http_mutex);
                for (const auto& [id, url] : script_urls)
                    ++url_counts[url.group];
            }
            std::map<Uuid, ScriptStats> scores;
            for (const auto& [id, script] : scripts) {
                if (!script.instance)
                    continue;
                const auto heap_bytes = script.instance->heap_bytes_used();
                if (script.cpu_ms <= minimum_script_time_ms && heap_bytes <= minimum_script_heap_bytes)
                    continue;
                auto object = world.object(script.item.parent);
                if (object && object->parent_id && !object->is_attachment)
                    object = world.object(object->parent_id);
                if (object) {
                    auto& stats = scores[object->id];
                    stats.time_ms += script.cpu_ms;
                    stats.heap_bytes += heap_bytes;
                }
            }
            std::vector<std::pair<Uuid, ScriptStats>> ranked(scores.begin(), scores.end());
            std::stable_sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
                return left.second.time_ms > right.second.time_ms;
            });
            const auto lower = [](std::string_view text) {
                return framework::utf8(framework::invariant_lower_utf16(framework::utf16_replacing(text)));
            };
            const auto name_filter_flags = filter_by_owner_name | filter_by_object_name | filter_by_parcel_name;
            std::string filter;
            if ((request.flags & name_filter_flags) && !framework::trim_utf8(request.filter).empty())
                filter = lower(request.filter);
            if ((request.flags & filter_by_owner_name) && !filter.empty()) {
                const auto dot = filter.find('.');
                if (dot > 0 && dot != std::string::npos)
                    filter.replace(dot, 1, dot + 1 == filter.size() ? "" : " ");
            }
            for (const auto& [id, stats] : ranked) {
                const auto object = world.object(id);
                if (!object)
                    continue;
                const auto position = script_world_position(*object);
                const auto parcel = parcel_at_truncated_position(position);
                if ((request.flags & filter_by_parcel_id) && (!parcel || parcel->local_id != request.parcel))
                    continue;
                auto owner = script_avatar_name(object->owner);
                if (!owner && policies.script_account) {
                    if (const auto account = policies.script_account(object->owner))
                        owner = account->name();
                }
                const auto owner_name = owner.value_or("UnknownUMM2 " + object->owner.str());
                const auto parcel_name = parcel ? parcel->name : "unknown";
                if (!filter.empty()) {
                    std::string_view name = parcel_name;
                    if (request.flags & filter_by_owner_name)
                        name = owner_name;
                    else if (request.flags & filter_by_object_name)
                        name = object->name;
                    if (lower(name).find(filter) == std::string::npos)
                        continue;
                }
                rows.push_back({*object, position, stats, owner_name, parcel_name, url_counts[id]});
                if (rows.size() == report_limit)
                    break;
            }
        }
        if (capability_runtime && capability_runtime->event_queue()) {
            const auto real = [](double value) {
                Value result;
                result.kind = Value::Kind::Real;
                result.real = value;
                return result;
            };
            const auto uuid = [](Uuid value) {
                Value result(value.str());
                result.kind = Value::Kind::Uuid;
                return result;
            };
            const auto array = [](std::vector<Value> values) {
                Value result;
                result.kind = Value::Kind::Array;
                result.array = std::move(values);
                return result;
            };
            auto request_data = Value::object({
                {"ReportType", osd_from_unsigned(type)},
                {"RequestFlags", osd_from_unsigned(request.flags)},
                {"TotalObjectCount", osd_from_unsigned(static_cast<std::uint32_t>(rows.size()))},
            });
            auto body = Value::object({{"RequestData", array({std::move(request_data)})}});
            if (!rows.empty()) {
                std::vector<Value> reports, extended;
                for (const auto& row : rows) {
                    const auto rez_seconds = std::chrono::duration_cast<std::chrono::seconds>(
                        row.object.rezzed_at.time_since_epoch());
                    reports.push_back(Value::object({
                        {"LocationX", real(row.position.x)}, {"LocationY", real(row.position.y)},
                        {"LocationZ", real(row.position.z)}, {"OwnerName", row.owner},
                        {"Score", real(row.stats.time_ms)},
                        {"TaskID", uuid(row.object.id)}, {"TaskLocalID", osd_from_unsigned(row.object.local_id)},
                        {"TaskName", row.object.name}}));
                    extended.push_back(Value::object({
                        {"MonoScore", real(0.)}, {"OwnerID", uuid(row.object.owner)}, {"ParcelName", row.parcel},
                        {"PublicURLs", Value(row.url_count)},
                        {"Size", real(static_cast<double>(row.stats.heap_bytes))},
                        {"TimeStamp", osd_from_unsigned(static_cast<std::uint32_t>(rez_seconds.count()))}}));
                }
                body.set("ReportData", array(std::move(reports)));
                body.set("DataExtended", array(std::move(extended)));
            }
            auto& queue = *capability_runtime->event_queue();
            queue.enqueue(services::EventQueueModule::build_event("LandStatReply",
                          std::make_shared<Value>(std::move(body))), request.agent);
            return;
        }
        auto reply = lludp::make_packet("LandStatReply");
        auto& data = reply.block("RequestData");
        data["ReportType"] = std::uint64_t(type);
        data["RequestFlags"] = std::uint64_t(request.flags);
        data["TotalObjectCount"] = std::uint64_t(rows.size());
        for (const auto& row : rows) {
            auto report = lludp::make_block(reply.spec->blocks.at(1));
            report["TaskLocalID"] = std::uint64_t(row.object.local_id);
            report["TaskID"] = row.object.id;
            report["LocationX"] = double(row.position.x);
            report["LocationY"] = double(row.position.y);
            report["LocationZ"] = double(row.position.z);
            report["Score"] = row.stats.time_ms;
            report["TaskName"] = profile_string_bytes(framework::utf16(row.object.name), 255);
            report["OwnerName"] = profile_string_bytes(framework::utf16(row.owner), 255);
            reply.blocks["ReportData"].push_back(std::move(report));
        }
        viewers->transport().enqueue_if_session(request.agent, request.session, std::move(reply),
                                                ThrottleOutPacketType::Task);
    }

    void run_land_stats_requests() {
        std::vector<LandStatsRequest> requests;
        {
            std::lock_guard lock(commands_mutex);
            requests.swap(land_stats_requests);
        }
        for (const auto& request : requests) {
            const auto viewer = viewers->agent(request.agent);
            if (!viewer || viewer->circuit->session_id != request.session)
                continue;
            try {
                send_land_stats(request, request.type);
                if (request.type > 1)
                    send_land_stats(request, 0);
            } catch (const std::exception& failure) {
                error(std::string("Land statistics failed: ") + failure.what());
            }
        }
    }

    void run_scripts() {
        const bool execute_scripts = scripts_enabled();
        if (execute_scripts) {
            std::lock_guard lock(task_data_mutex);
            const auto pending = std::exchange(restored_scripts, {});
            for (const auto& [part, item] : pending)
                for (const auto& diagnostic : compile_task(part, item))
                    error("Restored native script " + item.str() + ": " + diagnostic);
        }
        std::vector<ScriptCommand> commands;
        {
            std::lock_guard lock(commands_mutex);
            commands.swap(script_commands);
        }
        for (auto& command : commands) {
            auto object = world.object(command.object);
            if (!object)
                continue;
            if (command.item) {
                auto item = std::move(*command.item);
                if (policies.create_script_asset)
                    try {
                        item.asset_id =
                            policies.create_script_asset(command.actor, item, policies.default_script);
                    } catch (const std::exception& e) {
                        alert(command.actor, std::string("Script asset storage failed: ") + e.what());
                        continue;
                    }
                auto task = add_task_script(item);
                if (task_inventory(object->id).find(task->item_id) == task)
                    scripts.emplace(task->item_id, RunningScript{service_item(*task), {}, task});
                if (execute_scripts) {
                    auto errors =
                        compile_task_item(object->id, task, std::string_view(policies.default_script));
                    if (!errors.empty())
                        alert(command.actor, "Native script compile failed: " + errors.front());
                } else if (auto pending = scripts.find(task->item_id); pending != scripts.end()) {
                    pending->second.deferred_default_rez = true;
                }
                if (policies.new_script)
                    policies.new_script(command.actor, object->id, item.id);
                send_part_properties(object->id, command.actor);
            } else if (execute_scripts) {
                auto mask = command.event == "touch_start" ? script_event::touch_start
                            : command.event == "touch"     ? script_event::touch
                                                           : script_event::touch_end;
                const auto post_touch = [&](Uuid part) {
                    if (!(script_events(part).aggregated() & mask))
                        return false;
                    for (auto& [id, running_script] : scripts)
                        if (running_script.item.parent == part && running_script.instance)
                            running_script.instance->post_event(
                                {command.event, {std::int32_t(1)}, {command.detector}});
                    return true;
                };
                const bool handled = post_touch(object->id);
                if ((!handled || object->pass_touches) && object->parent_id && !object->is_attachment)
                    if (const auto root = world.object(object->parent_id))
                        post_touch(root->id);
            }
        }
        process_dataserver_requests();
        for (auto i = scripts.begin(); i != scripts.end();) {
            auto object = world.object(i->second.item.parent);
            if (!object) {
                replace_script_instance(i->second);
                i = scripts.erase(i);
                continue;
            }
            if (execute_scripts && std::exchange(i->second.deferred_default_rez, false)) {
                const auto task = task_inventory(object->id).find(i->first);
                if (task && task == i->second.task && task->type == 10) {
                    const auto errors =
                        compile_task_item(object->id, task, std::string_view(policies.default_script));
                    for (const auto& message : errors)
                        error("Native script " + i->first.str() + ": " + message);
                }
            }
            if (execute_scripts && i->second.instance) {
                auto& instance = *i->second.instance;
                instance.set_context(script_context(*object, i->second.item));
                const auto previous_mask = instance.event_mask();
                const auto started = std::chrono::steady_clock::now();
                const auto result = instance.run(256);
                stats_script_events += result.events_completed;
                if (result.instructions) {
                    const auto elapsed =
                        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                    stats_script_ms += elapsed;
                    i->second.cpu_ms += elapsed;
                }
                const auto current_mask = instance.event_mask();
                if (current_mask != previous_mask) {
                    script_events(object->id).set(i->first, current_mask);
                    refresh_script_flags(object->id);
                }
                if (result.status == script::LslRunStatus::Faulted)
                    error("Native script " + i->first.str() + ": " + result.error);
            }
            ++i;
        }
    }
    void removed(const ViewerAgent& a) {
        if (a.root && a.seat)
            notify_seat_linkset(a.seat->parent_local);
        {
            std::lock_guard lock(terrain_seed_mutex);
            pending_terrain_seeds.erase(a.circuit->session_id);
        }
        {
            std::lock_guard lock(commands_mutex);
            closed_asset_upload_sessions.push_back(a.circuit->session_id);
        }
        {
            std::lock_guard lock(texture_downloads_mutex);
            texture_downloads.erase(a.circuit->session_id);
        }
        {
            std::lock_guard lock(viewer_god_mutex);
            const auto state = viewer_god_modes.find(a.circuit->agent_id);
            if (state != viewer_god_modes.end() && state->second.session == a.circuit->session_id)
                viewer_god_modes.erase(state);
        }
        if (a.root)
            for (const auto& object : world.attachments(a.local_id))
                world.remove(object.id);
        {
            std::lock_guard lock(terrain_upload_mutex);
            if (terrain_upload && terrain_upload->session == a.circuit->session_id)
                terrain_upload.reset();
            if (terrain_upload_receipt && terrain_upload_receipt->session == a.circuit->session_id)
                terrain_upload_receipt.reset();
        }
        {
            std::lock_guard lock(texture_sequences_mutex);
            texture_sequences.erase(a.circuit->session_id);
        }
        if (const auto physics = viewer_physics.load())
            physics->left(a);
        {
            std::lock_guard lock(task_data_mutex);
            const auto animations = avatar_script_animations.find(a.circuit->agent_id);
            if (animations != avatar_script_animations.end() &&
                animations->second.session == a.circuit->session_id)
                avatar_script_animations.erase(animations);
        }
        {
            std::lock_guard lock(environment_mutex);
            agent_environment_overrides.erase(a.circuit->session_id);
        }
        if (basic_profiles)
            basic_profiles->remove_client(a.circuit->agent_id);
        {
            std::lock_guard lock(actors_mutex);
            auto client = profile_clients.find(a.circuit->agent_id);
            if (client != profile_clients.end() &&
                client->second->viewer.circuit->session_id == a.circuit->session_id) {
                client->second->close();
                profile_clients.erase(client);
            }
            actors.erase(a.circuit->agent_id);
        }
        const bool current = circuits.remove_current_circuit(a.circuit);
        if (capability_runtime) {
            if (current)
                capability_runtime->client_closed(a.circuit->agent_id);
            capability_runtime->capabilities().remove_circuit_caps(*a.circuit, current);
        }
        bool preserve_session;
        {
            std::lock_guard lock(teleport_mutex);
            preserve_session = departing_foreign_sessions.erase(a.circuit->session_id) != 0;
        }
        bool logged_out = !preserve_session;
        if (!preserve_session && a.root)
            logged_out = sessions.logout_from_region(a.circuit->session_id, descriptor.id);
        else if (!preserve_session)
            sessions.logout(a.circuit->session_id);
        if (running && logged_out && a.root && policies.friend_status_changed)
            policies.friend_status_changed(a.circuit->agent_id, a.circuit->session_id, false);
        if ((logged_out || (!preserve_session && a.root && a.circuit->verified_foreign_login)) &&
            policies.logged_out)
            policies.logged_out(a);
        for (auto& other : viewers->agents())
            if (other.root) {
                auto p = lludp::make_packet("KillObject");
                auto b = lludp::make_block(p.spec->blocks[0]);
                b["ID"] = std::uint64_t(a.local_id);
                p.blocks["ObjectData"].push_back(std::move(b));
                viewers->transport().enqueue(other.circuit->agent_id, std::move(p),
                                             ThrottleOutPacketType::Task);
            }
    }
};
RegionRuntime::RegionRuntime(GridRegion r, IPresenceService& p, RegionPolicies policies)
    : state_(std::make_shared<State>(std::move(r), p, std::move(policies))) {}
RegionRuntime::~RegionRuntime() {
    try {
        stop();
    } catch (const std::exception& e) {
        try {
            state_->error(e.what());
        } catch (...) {
        }
    } catch (...) {
    }
}
UdpEndpoint RegionRuntime::start(std::shared_ptr<server::RegistryCapabilityListener> listener,
                                 std::shared_ptr<server::RegistryCapabilityListener> secure_script_listener,
                                 std::shared_ptr<server::RegistryCapabilityListener> plain_script_listener) {
    if (!state_->terrain_baseline)
        state_->terrain_baseline.emplace(state_->world.terrain());
    auto s = state_;
    if (s->running)
        throw std::logic_error("Region already started");
    s->registry = std::move(listener);
    s->secure_script_registry = std::move(secure_script_listener);
    s->plain_script_registry = std::move(plain_script_listener);
    if (!s->registry)
        throw std::invalid_argument("Region capability listener required");
    if (s->policies.environment_enabled)
        s->load_parcel_bounds();
    if (s->policies.environment_enabled && s->policies.load_asset) {
        try {
            const Uuid default_day_id("5646d39e-d3d7-6aff-ed71-30fc87d64a92");
            if (const auto asset = s->policies.load_asset(default_day_id); asset && asset->data && !asset->data->empty()) {
                const auto bytes = std::string_view(reinterpret_cast<const char*>(asset->data->data()),
                                                    asset->data->size());
                auto cycle = deserialize_sdk_osd_bytes(bytes);
                if (cycle.kind == Value::Kind::Map && osd_string(cycle.get("type")) == "daycycle")
                    s->default_environment_cycle = normalize_environment_cycle(std::move(cycle));
            }
        } catch (const std::exception& failure) {
            s->error(std::string("Unable to load the default environment asset: ") + failure.what());
        }
    }
    s->capability_runtime = std::make_unique<SceneCapabilities>(s);
    if (s->policies.environment_enabled) {
        s->capability_runtime->shared_events()->on_register_caps.add(
            {{s.get(), "Environment.RegisterCaps"}, [weak = std::weak_ptr<State>(s)](Uuid agent, auto caps) {
                 caps->register_simple("ExtEnvironment", std::make_shared<State::ExtendedEnvironmentRoute>(weak, agent));
                 caps->register_simple("EnvironmentSettings", std::make_shared<State::LegacyEnvironmentRoute>(weak, agent));
             }});
    }
    s->capability_runtime->shared_events()->on_register_caps.add(
        {{s.get(), "AgentPreferences.RegisterCaps"}, [weak = std::weak_ptr<State>(s)](Uuid agent, auto caps) {
             auto route = std::make_shared<State::AgentPreferencesRoute>(weak, agent);
             caps->register_simple("AgentPreferences", route);
             caps->register_simple("UpdateAgentLanguage", route, false);
             caps->register_simple("UpdateAgentInformation", route, false);
             caps->register_simple("DispatchRegionInfo", std::make_shared<State::EstateRegionInfoRoute>(weak, agent));
         }});
    s->capability_runtime->shared_events()->on_register_caps.add(
        {{s.get(), "Gods.RegisterCaps"}, [weak = std::weak_ptr<State>(s)](Uuid agent, auto caps) {
             caps->register_simple("UntrustedSimulatorMessage",
                                   std::make_shared<State::SimulatorMessageRoute>(weak, agent));
         }});
    ViewerRegionOptions options;
    options.name = s->descriptor.name.value_or("");
    options.owner = s->descriptor.estate_owner;
    options.x = s->descriptor.x;
    options.y = s->descriptor.y;
    options.access = s->region_access;
    options.region_flags =
        s->region_flags() | s->policies.estate_menu_flags | s->effective_estate_handshake_flags();
    options.estate_manager = [weak = std::weak_ptr<State>(s)](Uuid agent) {
        const auto state = weak.lock();
        return state && state->is_estate_manager(agent);
    };
    options.billable_factor = s->policies.estate_billable_factor;
    options.water_height = s->water_height;
    options.current_water_height = [weak = std::weak_ptr<State>(s), initial = options.water_height] {
        const auto state = weak.lock();
        return state ? state->water_height.load() : initial;
    };
    std::weak_ptr<State> weak = s;
    options.current_access = [weak, initial = options.access] {
        const auto state = weak.lock();
        return state ? state->region_access.load() : initial;
    };
    options.current_region_flags = [weak, initial = options.region_flags] {
        if (const auto state = weak.lock())
            return state->region_flags() | state->policies.estate_menu_flags |
                   state->effective_estate_handshake_flags();
        return initial;
    };
    options.terrain_texture_heights = [weak] {
        if (const auto state = weak.lock()) {
            std::lock_guard lock(state->terrain_texture_mutex);
            return state->terrain_texture_heights;
        }
        return ViewerRegionOptions::TerrainTextureHeights{};
    };
    options.terrain_texture_assets = [weak](bool terrain_pbr) {
        if (const auto state = weak.lock()) {
            std::lock_guard lock(state->terrain_texture_mutex);
            return terrain_pbr ? state->terrain_pbr_materials : state->terrain_texture_assets;
        }
        return ViewerRegionOptions{}.terrain_details;
    };
    s->profile_scene = std::make_shared<State::RegionProfileScene>(s);
    if (s->policies.basic_profile)
        s->basic_profiles = std::make_unique<BasicProfileRuntime>(
            s->policies.basic_profile, s->policies.basic_profile_accounts, s->descriptor.scope,
            [weak](auto agent, auto session, auto packet) {
                if (auto v = weak.lock())
                    if (v->viewers)
                        v->viewers->transport().enqueue_if_session(agent, session, std::move(packet),
                                                                   ThrottleOutPacketType::Task);
            });
    options.resolve_landmark = s->policies.resolve_landmark;
    options.teleport_pending = [weak](Uuid session) {
        const auto state = weak.lock();
        if (!state)
            return false;
        std::lock_guard lock(state->teleport_mutex);
        return std::any_of(state->pending_teleports.begin(), state->pending_teleports.end(),
                           [&](const auto& transfer) { return transfer.session == session; });
    };
    options.local_seat_exit = [weak](const ViewerAgent& before, const ViewerAgent& after) {
        if (const auto state = weak.lock())
            state->finish_local_seat_exit(before, after);
    };
    options.prepare_root = [weak](ViewerAgent& a) {
        if (auto v = weak.lock()) {
            if (!a.root)
                v->route_root_arrival(a);
            auto appearance = a.appearance();
            if (v->policies.load_preferences) {
                try {
                    const auto hover = v->policies.load_preferences(a.circuit->agent_id).hover;
                    if (std::isfinite(hover) && (!appearance || appearance->hover_z != hover)) {
                        appearance = std::make_shared<AvatarAppearance>(appearance.get());
                        appearance->hover_z = hover;
                        a.appearance_override = appearance;
                    }
                } catch (const std::exception& e) {
                    v->error(std::string("Agent preferences load failed: ") + e.what());
                }
            }
            a.position =
                root_landing_position(a.position, v->descriptor.size_x, v->descriptor.size_y,
                                      appearance ? appearance->avatar_height : 0,
                                      [&](float x, float y) { return v->world.terrain().height_at(x, y); });
        }
    };
    s->viewers = std::make_unique<ViewerRegion>(
        s->world, s->circuits,
        [weak](const auto& e, auto bytes) {
            auto v = weak.lock();
            return v && v->socket && v->socket->send_to(e, bytes);
        },
        [weak](const auto& c, const auto&, const auto& e) {
            auto v = weak.lock();
            if (!v || !v->running)
                return false;
            auto p = v->sessions.agent(c->session_id);
            if (!p || p->user_id != c->agent_id.str()) {
                v->debug("Viewer circuit has no matching session presence for " + c->agent_id.str());
                return false;
            }
            auto ip = IpAddress::parse(e.address);
            if (!ip) {
                v->debug("Viewer circuit has an invalid peer address");
                return false;
            }
            IpEndpoint endpoint{std::make_shared<IpAddress>(*ip), e.port};
            if (c->verified_foreign_login &&
                (!v->policies.verify_foreign_client || !v->policies.verify_foreign_client(*c, ip->str()))) {
                std::lock_guard lock(v->new_mutex);
                if (v->circuits.get_agent_circuit_data(c->circuit_code) == c) {
                    const bool current = v->circuits.remove_current_circuit(c);
                    v->sessions.logout(c->session_id);
                    if (v->capability_runtime) {
                        if (current)
                            v->capability_runtime->client_closed(c->agent_id);
                        v->capability_runtime->capabilities().remove_circuit_caps(*c, current);
                    }
                    if (v->policies.logged_out) {
                        ViewerAgent pending;
                        pending.circuit = c;
                        pending.position = c->start_position;
                        v->policies.logged_out(pending);
                    }
                }
                return false;
            }
            bool login = false;
            if (!v->verification.verify_client(c, &endpoint, login)) {
                v->debug("Viewer circuit client verification failed for " + c->agent_id.str());
                return false;
            }
            std::lock_guard lock(v->actors_mutex);
            if (v->actors.contains(c->agent_id))
                return false;
            v->actors[c->agent_id] = std::make_shared<Actor>(c->agent_id);
            if (v->basic_profiles)
                v->basic_profiles->new_client(c->agent_id, c->session_id);
            return true;
        },
        options, s->policies.circuit_transport);
    s->viewers->callbacks(
        {},
        [weak](const ViewerAgent& a) {
            if (auto v = weak.lock())
                v->entered(a);
        },
        [weak](const ViewerAgent& a) {
            if (auto v = weak.lock())
                v->viewers->disconnect(a.circuit->agent_id);
        },
        [weak](const ViewerAgent& a) {
            if (auto v = weak.lock())
                v->removed(a);
        },
        [weak](const ViewerAgent& a, const lludp::Packet& p) {
            if (auto v = weak.lock())
                try {
                    v->packet(a, p);
                } catch (const std::exception& e) {
                    v->error("Viewer packet " + p.spec->name + ": " + e.what());
                    throw;
                }
        });
    if (!(s->region_flags() & 0x4000u))
        s->activate_physics();
    auto bind = s->descriptor.internal_endpoint ? UdpEndpoint{s->descriptor.internal_endpoint->address,
                                                              s->descriptor.internal_endpoint->port}
                                                : UdpEndpoint{"0.0.0.0", 9000};
    s->socket = std::make_unique<UdpSocket>(
        UdpSocketOptions{bind.address, bind.port, s->policies.udp_receive_buffer_bytes},
                                            [weak](const auto& e, auto bytes) {
                                                if (auto v = weak.lock())
                                                    if (v->running)
                                                        v->viewers->receive(e, bytes);
                                            });
    auto bound = s->socket->start();
    s->descriptor.internal_endpoint = RegionEndpoint{bound.address, bound.port};
    s->running = true;
    {
        std::lock_guard lock(s->commands_mutex);
        s->script_updates_open = true;
    }
    return bound;
}
bool RegionRuntime::can_edit_task_inventory(Uuid object, Uuid agent) const {
    return state_->running && state_->can_edit_inventory(object, agent);
}
bool RegionRuntime::can_edit_task_script(Uuid item, Uuid object, Uuid agent) const {
    return state_->running && state_->can_edit_script(item, object, agent);
}
void RegionRuntime::quiesce_script_updates() {
    state_->quiesce_updates();
}
std::int32_t RegionRuntime::active_script_count() const {
    return state_->scripts_enabled() ? state_->active_scripts.load(std::memory_order_relaxed) : 0;
}
std::vector<std::string> RegionRuntime::update_task_script(Uuid agent, Uuid item, Uuid object, bool running,
                                                           std::span<const std::uint8_t> data) {
    auto s = state_;
    auto job = std::make_shared<State::ScriptUpdateJob>();
    job->agent = agent;
    job->item = item;
    job->object = object;
    job->running = running;
    job->data = std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.end());
    auto result = job->result.get_future();
    {
        std::lock_guard lock(s->commands_mutex);
        if (!s->running || !s->script_updates_open)
            throw std::runtime_error("Region script updates stopped");
        s->script_update_jobs.push_back(job);
    }
    return result.get();
}
Uuid RegionRuntime::update_inventory_asset(Uuid agent, Uuid item, Uuid object,
                                           std::span<const std::uint8_t> data) {
    auto s = state_;
    auto job = std::make_shared<State::InventoryAssetJob>();
    job->agent = agent;
    job->item = item;
    job->object = object;
    job->data = std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.end());
    auto result = job->result.get_future();
    {
        std::lock_guard lock(s->commands_mutex);
        if (!s->running || !s->script_updates_open)
            throw std::runtime_error("Region inventory asset updates stopped");
        s->inventory_asset_jobs.push_back(job);
    }
    return result.get();
}
void RegionRuntime::poll(double host_sleep_ms) {
    auto& state = *state_;
    if (!state.running)
        return;
    state.report_stats(host_sleep_ms);
    state.poll_estate_region_change();
    state.poll_environment_time();
    {
        std::lock_guard lock(state.new_mutex);
        const auto now = std::chrono::steady_clock::now();
        std::erase_if(state.incoming_teleports, [&](const auto& entry) {
            const auto& circuit = entry.first;
            if (state.circuits.get_agent_circuit_data(circuit->circuit_code) != circuit) {
                const auto callback = state.arrival_callbacks.find(circuit->agent_id);
                if (callback != state.arrival_callbacks.end() && callback->second.circuit == circuit)
                    state.arrival_callbacks.erase(callback);
                return true;
            }
            if (auto viewer = state.viewers->agent(circuit->agent_id); viewer && viewer->circuit == circuit)
                return true;
            if (entry.second > now)
                return false;
            const bool current = state.circuits.remove_current_circuit(circuit);
            const auto callback = state.arrival_callbacks.find(circuit->agent_id);
            if (callback != state.arrival_callbacks.end() && callback->second.circuit == circuit)
                state.arrival_callbacks.erase(callback);
            if (state.capability_runtime) {
                if (current)
                    state.capability_runtime->client_closed(circuit->agent_id);
                state.capability_runtime->capabilities().remove_circuit_caps(*circuit, current);
            }
            return true;
        });
    }
    if (state.policies.teleport_arrived) {
        std::vector<State::PendingTeleport> arrived, expired;
        {
            std::lock_guard lock(state.teleport_mutex);
            const auto now = std::chrono::steady_clock::now();
            std::erase_if(state.pending_teleports, [&](const auto& transfer) {
                if (transfer.confirmed ||
                    (!transfer.requires_callback &&
                     state.policies.teleport_arrived(transfer.session, transfer.destination.id))) {
                    arrived.push_back(transfer);
                    return true;
                }
                if (transfer.expires > now)
                    return false;
                expired.push_back(transfer);
                return true;
            });
        }
        for (const auto& transfer : arrived) {
            const auto viewer = state.viewers->agent(transfer.agent);
            if (viewer && viewer->circuit->session_id == transfer.session) {
                if (transfer.requires_callback) {
                    std::lock_guard lock(state.teleport_mutex);
                    state.departing_foreign_sessions.insert(transfer.session);
                }
                state.viewers->transport().remove_client_if_session(transfer.agent, transfer.session);
            }
        }
        for (auto& transfer : expired) {
            if (state.policies.cancel_teleport_destination)
                state.policies.cancel_teleport_destination(transfer.destination, transfer.agent,
                                                           transfer.session);
            auto failure = lludp::make_packet("TeleportFailed");
            auto& info = failure.block("Info");
            info["AgentID"] = transfer.agent;
            constexpr char reason[] = "Destination did not confirm teleport arrival";
            info["Reason"] = std::vector<std::uint8_t>(std::begin(reason), std::end(reason));
            state.viewers->transport().enqueue_if_session(transfer.agent, transfer.session,
                                                          std::move(failure), ThrottleOutPacketType::Unknown);
            state.finish_failed_estate_ejection(transfer);
        }
    }
    const bool simulate_physics = (state.region_flags() & 0x4000u) == 0;
    if (!simulate_physics) {
        state.physics_activation_attempted = false;
    } else if (!state.physics_activation_attempted && !state.viewer_physics.load()) {
        try {
            state.activate_physics();
        } catch (const std::exception& failure) {
            state.error(std::string("Region physics activation failed: ") + failure.what());
        }
    }
    if (const auto physics = state.viewer_physics.load()) {
        const auto sample = physics->poll(simulate_physics);
        state.stats_physics_steps += sample.steps;
        state.stats_physics_ms += sample.solver_ms;
        state.stats_agent_ns.fetch_add(static_cast<std::int64_t>(sample.agent_ms * 1000000.),
                                       std::memory_order_relaxed);
    }
    state.run_inventory();
    state.run_script_updates();
    state.run_inventory_scripts();
    state.deliver_script_http_events();
    const auto reparented = state.viewers->refresh_seated_positions();
    state.run_scripts();
    state.run_land_stats_requests();
    const auto scene_updates_start = std::chrono::steady_clock::now();
    state.world.expire_temporary_objects(scene_updates_start);
    state.refresh_avatar_parcel_views();
    state.close_kicked_viewers();
    state.run_task_inventory();
    state.send_due_appearances();
    state.save_due_appearances();
    state.send_due_wearables();
    state.flush_objects();
    for (const auto& moved : reparented) {
        const auto current = state.viewers->agent(moved.circuit->agent_id);
        if (!current || !current->seat || current->seat->order != moved.seat->order ||
            current->circuit->session_id != moved.circuit->session_id)
            continue;
        for (const auto& recipient : state.viewers->agents())
            if (recipient.root)
                state.send_avatar(*current, recipient);
    }
    state.viewers->broadcast_terrain_changes();
    state.stats_other_ms +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - scene_updates_start)
            .count();
    state.viewers->poll();
    state.finish_pending_terrain_seeds();
    state.send_textures();
}
void RegionRuntime::stop(const std::function<void()>& save_scene) {
    auto s = state_;
    s->quiesce_updates();
    {
        std::lock_guard lock(s->new_mutex);
        s->running = false;
    }
    std::exception_ptr first;
    auto attempt = [&](auto&& cleanup) {
        try {
            cleanup();
        } catch (...) {
            if (!first)
                first = std::current_exception();
        }
    };
    attempt([&] {
        if (s->socket)
            s->socket->stop();
    });
    attempt([&] {
        if (s->viewers)
            s->viewers->close();
    });
    s->viewer_physics.store(nullptr);
    attempt([&] {
        if (s->basic_profiles)
            s->basic_profiles->stop();
    });
    s->basic_profiles.reset();
    attempt([&] {
        if (s->capability_runtime)
            s->capability_runtime->shutdown();
    });
    s->capability_runtime.reset();
    for (const auto& [agent, circuit] : s->circuits.get_agent_circuits()) {
        attempt([&] { s->circuits.remove_circuit(circuit); });
        const bool incoming = std::any_of(s->incoming_teleports.begin(), s->incoming_teleports.end(),
                                          [&](const auto& transfer) { return transfer.first == circuit; });
        if (incoming)
            continue;
        attempt([&] { s->sessions.logout(circuit->session_id); });
        if (s->policies.logged_out) {
            attempt([&] {
                ViewerAgent pending;
                pending.circuit = circuit;
                pending.position = circuit->start_position;
                s->policies.logged_out(pending);
            });
        }
    }
    {
        std::lock_guard lock(s->new_mutex);
        s->incoming_teleports.clear();
        s->arrival_callbacks.clear();
    }
    {
        std::lock_guard lock(s->teleport_mutex);
        s->pending_teleports.clear();
        s->departing_foreign_sessions.clear();
    }
    if (save_scene)
        attempt(save_scene);
    attempt([&] { s->sessions.logout_region(s->descriptor.id); });
    {
        std::lock_guard lock(s->script_http_mutex);
        for (const auto& [id, url] : s->script_urls)
            s->script_url_listener(url.secure)->routes.remove_poll(s->script_url_prefix(url.secure) + id.str());
        s->script_urls.clear();
        s->script_http_requests.clear();
        s->script_http_events.clear();
    }
    s->scripts.clear();
    s->dataserver_requests.clear();
    s->dataserver_lookup.reset();
    s->active_scripts.store(0, std::memory_order_relaxed);
    {
        std::lock_guard lock(s->task_data_mutex);
        s->task_parts.clear();
    }
    s->task_recipient.reset();
    {
        std::lock_guard lock(s->inventory_mutex);
        s->inventory_items.clear();
        s->inventory_folders.clear();
        s->inventory_items_running = s->inventory_folders_running = false;
    }
    {
        std::lock_guard lock(s->commands_mutex);
        s->script_commands.clear();
        s->task_commands.clear();
        s->inventory_script_commands.clear();
    }
    if (first)
        std::rethrow_exception(first);
}
std::shared_ptr<const framework::AgentCircuitData::ServiceUrls>
RegionRuntime::profile_service_urls(Uuid id) const {
    const auto& state = state_;
    if (!state->running || !state->viewers)
        return {};
    auto circuit = state->circuits.get_agent_circuit_data(id);
    if (!circuit || !circuit->service_urls)
        return {};
    return std::make_shared<const framework::AgentCircuitData::ServiceUrls>(*circuit->service_urls);
}
std::shared_ptr<ProfileRequestPresence> RegionRuntime::profile_presence(Uuid id) const {
    return state_->profile_presence(id);
}
bool RegionRuntime::kick_viewer(Uuid id, std::u16string_view reason) {
    auto state = state_;
    if (!state->running || !state->viewers)
        return false;
    const auto viewer = state->viewers->agent(id);
    if (!viewer)
        return false;
    if (viewer->root) {
        state->send_kick(*viewer, reason);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return state->viewers->transport().remove_client_if_session(id, viewer->circuit->session_id);
}
bool RegionRuntime::deliver_instant_message(const RegionInstantMessage& message) {
    return state_->deliver_instant_message(message);
}
bool RegionRuntime::receive_grid_moderation(const RegionInstantMessage& message) {
    if (message.bucket.empty())
        return false;
    static const Uuid service_god("6571e388-6218-4574-87db-f9379718315e");
    if (message.sender != service_god) {
        const auto sender = state_->viewers->agent(message.sender);
        const auto* viewer = sender ? &*sender : nullptr;
        const auto level = state_->moderation_level(message.sender, viewer);
        return state_->moderate_user(message.sender, viewer, level, message.recipient, message.bucket.front(),
                                     message.message, false);
    }
    const auto target = state_->viewers->agent(message.recipient);
    if (!target || !target->root)
        return false;
    if (state_->agent_god_level(message.recipient, target->circuit->session_id) >= 240) {
        alert_viewer(message.recipient,
                     "GRID kick detected and ignored, kick reason: " + framework::utf8(message.message));
        return true;
    }
    state_->send_kick(*target, message.message);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return state_->viewers->transport().remove_client_if_session(message.recipient,
                                                                 target->circuit->session_id);
}
bool RegionRuntime::notify_friend_rights(Uuid recipient, Uuid principal, std::int32_t previous,
                                         std::int32_t granted) {
    return state_->notify_friend_rights(recipient, principal, previous, granted);
}
bool RegionRuntime::notify_friend_termination(Uuid recipient, Uuid former_friend) {
    return state_->notify_friend_termination(recipient, former_friend);
}
bool RegionRuntime::alert_viewer(Uuid recipient, const std::string& message) {
    auto state = state_;
    if (!state->running || !state->viewers)
        return false;
    const auto viewer = state->viewers->agent(recipient);
    if (!viewer || !viewer->root || viewer->logout_requested)
        return false;
    return state->viewers->transport().enqueue_if_session(recipient, viewer->circuit->session_id,
                                                          agent_alert(recipient, message, false),
                                                          ThrottleOutPacketType::Task);
}
bool RegionRuntime::notify_friend_status(Uuid recipient, Uuid friend_id, bool online,
                                         std::optional<Uuid> expected_session) {
    auto state = state_;
    if (!state->running || !state->viewers)
        return false;
    const auto viewer = state->viewers->agent(recipient);
    if (!viewer || !viewer->root || viewer->logout_requested ||
        (expected_session && viewer->circuit->session_id != *expected_session))
        return false;
    auto packet = lludp::make_packet(online ? "OnlineNotification" : "OfflineNotification");
    auto agent = lludp::make_block(packet.spec->blocks[0]);
    agent["AgentID"] = friend_id;
    packet.blocks["AgentBlock"].push_back(std::move(agent));
    return state->viewers->transport().enqueue_if_session(recipient, viewer->circuit->session_id,
                                                          std::move(packet), ThrottleOutPacketType::Task);
}
GridRegion RegionRuntime::descriptor() const {
    auto region = state_->descriptor;
    region.maturity = state_->region_maturity;
    region.access = state_->region_access;
    return region;
}
World& RegionRuntime::world() {
    return state_->world;
}
ViewerRegion& RegionRuntime::viewers() {
    if (!state_->viewers)
        throw std::logic_error("Region not started");
    return *state_->viewers;
}
void RegionRuntime::load_terrain_baseline(int format, std::string_view blob) {
    if (state_->running)
        throw std::logic_error("Terrain baseline must be loaded before region startup");
    auto& terrain = state_->world.terrain();
    Terrain baseline(terrain.width(), terrain.height());
    baseline.load_database_blob(format, blob);
    state_->terrain_baseline.emplace(baseline);
}
void RegionRuntime::bake_terrain() {
    state_->bake_terrain();
}
void RegionRuntime::set_terrain_limits(float lower, float raise) {
    std::lock_guard lock(state_->terrain_edit_mutex);
    state_->terrain_lower_limit = std::clamp(lower, -200.f, 0.f);
    state_->terrain_raise_limit = std::clamp(raise, 0.f, 200.f);
}
void RegionRuntime::load_region_sun_settings(bool use_estate_sun) {
    std::lock_guard lock(state_->terrain_edit_mutex);
    state_->use_estate_sun = use_estate_sun;
}
bool RegionRuntime::use_estate_sun() const {
    std::lock_guard lock(state_->terrain_edit_mutex);
    return state_->use_estate_sun;
}
void RegionRuntime::load_environment_day(EnvironmentDay day) {
    if (state_->running)
        throw std::logic_error("Environment timing must be loaded before region startup");
    state_->environment_day = day;
}
void RegionRuntime::restore_task_inventory(
    Uuid part, const std::vector<std::shared_ptr<framework::TaskInventoryItem>>& items) {
    if (items.empty() || !state_->world.object(part))
        return;
    std::lock_guard lock(state_->task_data_mutex);
    auto& inventory = state_->task_inventory(part);
    inventory.restore(items);
    for (const auto& item : items)
        if (item && item->inventory_type == 10 && inventory.find(item->item_id) == item)
            state_->restored_scripts.emplace_back(part, item->item_id);
}
std::optional<std::vector<framework::TaskInventoryItem>>
RegionRuntime::snapshot_task_inventory(Uuid part) const {
    std::lock_guard lock(state_->task_data_mutex);
    const auto found = state_->task_parts.find(part);
    if (found == state_->task_parts.end() || !found->second.inventory)
        return std::nullopt;
    std::vector<framework::TaskInventoryItem> result;
    for (const auto& item : found->second.inventory->items())
        result.push_back(*item);
    return result;
}
void RegionRuntime::set_scripts_enabled(bool enabled) {
    std::lock_guard lock(state_->estate_region_settings_mutex);
    constexpr std::uint32_t skip_scripts = 0x2000u;
    if (enabled)
        state_->region_setting_flags.fetch_and(~skip_scripts);
    else
        state_->region_setting_flags.fetch_or(skip_scripts);
}
void RegionRuntime::set_region_setting_flags(std::uint32_t flags) {
    if (state_->running)
        throw std::logic_error("Region flags must be set before region startup");
    state_->region_setting_flags = flags;
}
void RegionRuntime::set_region_maturity(std::int32_t maturity) {
    if (state_->running)
        throw std::logic_error("Region maturity must be loaded before region startup");
    state_->update_maturity(maturity);
}
void RegionRuntime::set_region_telehub(Uuid object, std::vector<RegionSpawnPoint> points) {
    if (state_->running)
        throw std::logic_error("Region telehub must be set before region startup");
    state_->region_telehub = object;
    state_->region_spawn_points = std::move(points);
}
void RegionRuntime::set_region_menu_limits(int agent_limit, float object_bonus) {
    state_->region_agent_limit = agent_limit;
    state_->region_object_bonus = object_bonus;
}
void RegionRuntime::set_estate_covenant(Uuid covenant, std::uint32_t timestamp) {
    std::lock_guard lock(state_->estate_covenant_mutex);
    state_->estate_covenant = covenant;
    state_->estate_covenant_timestamp = timestamp;
}
void RegionRuntime::receive_estate_covenant(std::uint32_t estate, Uuid covenant) {
    if (estate != state_->policies.estate_id)
        return;
    std::lock_guard lock(state_->estate_covenant_mutex);
    state_->estate_covenant = covenant;
}
void RegionRuntime::receive_estate_message(std::uint32_t estate, const std::string& message) {
    if (estate == state_->policies.estate_id)
        state_->send_region_notice(message);
}
void RegionRuntime::receive_estate_teleport_home(std::uint32_t estate, std::optional<Uuid> target) {
    auto& state = *state_;
    if (estate != state.policies.estate_id || !state.viewers)
        return;
    for (const auto& viewer : state.viewers->agents())
        if (viewer.root && (!target || viewer.circuit->agent_id == *target))
            state.estate_send_home(viewer, true, !target);
}
void RegionRuntime::set_terrain_texture_heights(ViewerRegionOptions::TerrainTextureHeights heights) {
    std::lock_guard lock(state_->terrain_texture_mutex);
    state_->terrain_texture_heights = heights;
}
void RegionRuntime::set_terrain_texture_assets(std::array<Uuid, 4> textures,
                                              std::array<Uuid, 4> materials) {
    std::lock_guard lock(state_->terrain_texture_mutex);
    state_->terrain_texture_assets = textures;
    state_->terrain_pbr_materials = materials;
}
void RegionRuntime::set_terrain_handshake(bool blocked, float water_height) {
    if (state_->running)
        throw std::logic_error("Terrain flags must be set before region startup");
    state_->block_terraform = blocked;
    state_->water_height = water_height;
}
void RegionRuntime::load_terrain_file(std::string_view filename, std::span<const std::uint8_t> data) {
    state_->load_terrain_file(filename, data, true);
}
void RegionRuntime::revert_terrain() {
    std::lock_guard lock(state_->terrain_edit_mutex);
    if (!state_->terrain_baseline)
        return;
    const auto& baseline = *state_->terrain_baseline;
    state_->world.terrain().replace_heights(baseline);
}
void RegionRuntime::flip_terrain(bool x_axis) {
    std::lock_guard lock(state_->terrain_edit_mutex);
    auto& active = state_->world.terrain();
    Terrain terrain(active);
    const auto columns = x_axis ? terrain.width() / 2 : terrain.width();
    const auto rows = x_axis ? terrain.height() : terrain.height() / 2;
    for (std::uint32_t y = 0; y < rows; ++y)
        for (std::uint32_t x = 0; x < columns; ++x) {
            const auto other_x = x_axis ? terrain.width() - 1 - x : x;
            const auto other_y = x_axis ? y : terrain.height() - 1 - y;
            const auto first = terrain.get(x, y);
            terrain.set(x, y, terrain.get(other_x, other_y));
            terrain.set(other_x, other_y, first);
        }
    active.replace_heights(terrain);
}
void RegionRuntime::adjust_terrain(const std::function<float(float)>& adjust) {
    std::lock_guard lock(state_->terrain_edit_mutex);
    auto& active = state_->world.terrain();
    Terrain terrain(active);
    for (std::uint32_t y = 0; y < terrain.height(); ++y)
        for (std::uint32_t x = 0; x < terrain.width(); ++x) {
            const auto height = adjust(terrain.get(x, y));
            if (std::isfinite(height))
                terrain.set(x, y, height);
        }
    active.replace_heights(terrain);
}
void RegionRuntime::rescale_terrain(float minimum, float maximum) {
    const float range = maximum - minimum;
    if (range == 0.f) {
        adjust_terrain([maximum](float) { return maximum; });
        return;
    }
    std::lock_guard lock(state_->terrain_edit_mutex);
    auto& active = state_->world.terrain();
    Terrain terrain(active);
    const auto samples = terrain.compressed_map();
    float low = std::numeric_limits<float>::max(), high = std::numeric_limits<float>::lowest();
    for (float sample : samples) {
        if (sample < low)
            low = sample;
        else if (sample > high)
            high = sample;
    }
    const float scale = range / (high - low);
    for (std::uint32_t x = 0; x < terrain.width(); ++x)
        for (std::uint32_t y = 0; y < terrain.height(); ++y) {
            const float height = minimum + (samples[std::size_t(x) * terrain.height() + y] - low) * scale;
            if (std::isfinite(height))
                terrain.set(x, y, height);
        }
    active.replace_heights(terrain);
}
SceneCapabilities* RegionRuntime::capabilities() {
    return state_->capability_runtime.get();
}
bool RegionRuntime::query_gatekeeper_access(const GridRegion& region, const AgentCircuitData& circuit,
                                            std::string& reason) {
    if (region.id != state_->descriptor.id || !state_->running) {
        reason = "Region unavailable";
        return false;
    }
    const auto& position = circuit.start_position;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        reason = "Invalid agent position";
        return false;
    }
    return true;
}
bool RegionRuntime::close_agent_session(Uuid agent, std::string_view session) {
    auto& state = *state_;
    std::unique_lock lock(state.new_mutex);
    if (!state.running)
        return false;
    const auto circuit = state.circuits.get_agent_circuit_data(agent);
    if (!circuit || circuit->session_id.str() != session)
        return false;
    if (const auto viewer = state.viewers->agent(agent)) {
        if (viewer->circuit != circuit)
            return false;
        lock.unlock();
        return state.viewers->transport().remove_client_if_session(agent, circuit->session_id);
    }
    const auto pending = std::find_if(state.incoming_teleports.begin(), state.incoming_teleports.end(),
                                      [&](const auto& transfer) { return transfer.first == circuit; });
    if (pending == state.incoming_teleports.end())
        return false;
    state.circuits.remove_current_circuit(circuit);
    state.arrival_callbacks.erase(agent);
    state.incoming_teleports.erase(pending);
    if (state.capability_runtime) {
        state.capability_runtime->client_closed(agent);
        state.capability_runtime->capabilities().remove_circuit_caps(*circuit, true);
    }
    return true;
}
bool RegionRuntime::confirm_teleport_arrival(Uuid agent, Uuid token) {
    if (token.is_zero())
        return false;
    std::lock_guard lock(state_->teleport_mutex);
    for (auto& transfer : state_->pending_teleports)
        if (transfer.agent == agent && transfer.token == token &&
            transfer.expires > std::chrono::steady_clock::now()) {
            transfer.confirmed = true;
            return true;
        }
    return false;
}
bool RegionRuntime::viewer_god_mode(Uuid agent, Uuid session) const {
    return state_->viewer_is_god(agent, session);
}
bool RegionRuntime::agent_has_god_permissions(Uuid agent) const {
    const auto viewer = state_->viewers->agent(agent);
    return viewer && state_->agent_god_level(agent, viewer->circuit->session_id) >= 200;
}
bool RegionRuntime::update_agent(GridRegion& destination, const std::shared_ptr<AgentData>& data,
                                 LoginTransferContext&) {
    auto& state = *state_;
    if (!data || destination.id != state.descriptor.id)
        return false;
    std::lock_guard lock(state.new_mutex);
    if (!state.running)
        return false;
    const auto circuit = state.circuits.get_agent_circuit_data(data->agent_id);
    if (!circuit || circuit->session_id != data->session_id || circuit->circuit_code != data->circuit_code ||
        state.viewers->agent(data->agent_id))
        return false;
    if (!std::isfinite(data->position.x) || !std::isfinite(data->position.y) ||
        !std::isfinite(data->position.z))
        return false;
    std::optional<bool> transferred_god_mode;
    if (data->god_data) {
        const auto& god = data->god_data->view();
        if (god.kind != Value::Kind::Map)
            return false;
        if (god.contains("ViewerUiIsGod"))
            transferred_god_mode = osd_boolean(god.get("ViewerUiIsGod"));
    }
    const auto uri = data->new_callback_uri.value_or(data->callback_uri.value_or(""));
    if (!uri.empty()) {
        const auto callback = state.arrival_callbacks.find(data->agent_id);
        if (callback == state.arrival_callbacks.end() || callback->second.circuit != circuit)
            return false;
        if (uri != callback->second.prefix) {
            const auto authenticated_prefix = callback->second.prefix + "?auth=";
            Uuid token;
            if (!uri.starts_with(authenticated_prefix) ||
                !framework::try_parse_sdk_uuid(std::string_view(uri).substr(authenticated_prefix.size()),
                                               token) ||
                token.is_zero())
                return false;
        }
        callback->second.uri = uri;
    }
    circuit->start_position = data->position;
    circuit->active_group_id = data->active_group_id;
    circuit->transfer_viewer_god_mode = transferred_god_mode;
    if (std::isfinite(data->at_axis.x) && std::isfinite(data->at_axis.y) &&
        (data->at_axis.x != 0 || data->at_axis.y != 0))
        circuit->transfer_look_at = Vector3{data->at_axis.x, data->at_axis.y, 0};
    return true;
}
bool RegionRuntime::query_access(GridRegion& r, const Uuid& a, const std::optional<std::string>&, bool,
                                 const Vector3& p, std::vector<Uuid>&, LoginTransferContext&,
                                 std::string& reason) {
    if (r.id != state_->descriptor.id || !state_->running) {
        reason = "Region unavailable";
        return false;
    }
    return state_->check_region_access(a, p, reason);
}
bool RegionRuntime::create_agent(const GridRegion* source, GridRegion& r,
                                 const std::shared_ptr<AgentCircuitData>& c, std::uint32_t flags,
                                 LoginTransferContext&, std::string& reason) {
    auto s = state_;
    if (r.id != s->descriptor.id || !s->running) {
        reason = "Region unavailable";
        return false;
    }
    std::lock_guard lock(s->new_mutex);
    if (!s->running) {
        reason = "Region unavailable";
        return false;
    }
    const bool incoming = source && source->id != r.id && c && !c->child;
    if (!s->admission.new_user_connection(c, flags, source, reason))
        return false;
    s->arrival_callbacks.erase(c->agent_id);
    if (source && source->id != r.id && source->raw_server_uri && !source->raw_server_uri->empty())
        s->arrival_callbacks.emplace(c->agent_id, State::ArrivalCallback{c,
                                                                         source->server_uri() + "agent/" +
                                                                             c->agent_id.str() + "/" +
                                                                             source->id.str() + "/release/",
                                                                         {}});
    if (incoming)
        s->incoming_teleports.emplace_back(c, std::chrono::steady_clock::now() + std::chrono::seconds(30));
    return true;
}
}
