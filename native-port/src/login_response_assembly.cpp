// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_response.hpp"
#include "opensim/services/avatar_data.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <chrono>
#include <stdexcept>
namespace opensim::services {
using network::Value;
namespace {
Value array() {
    Value a;
    a.kind = Value::Kind::Array;
    return a;
}
Value raw(const std::optional<std::string>& text) {
    return text ? Value(*text) : Value{};
}
Value folder_value(const LoginInventoryFolder& folder) {
    return Value::object({{"name", raw(folder.name)},
                          {"parent_id", folder.parent.str()},
                          {"version", Value(static_cast<std::int32_t>(folder.version))},
                          {"type_default", Value(static_cast<std::int32_t>(folder.type))},
                          {"folder_id", folder.id.str()}});
}
std::string number(float value) {
    return avatar_float_string(value);
}
bool universal_id(const std::optional<std::string>& value, framework::Uuid& id) {
    if (!value)
        return false;
    const auto text = framework::utf16_replacing(*value);
    if (text.size() < 36)
        return false;
    return framework::try_parse_sdk_uuid(framework::utf8(std::u16string_view(text).substr(0, 36)), id);
}
}
void bind_login_response_network(LoginResponseEnvironment& environment,
                                 std::shared_ptr<framework::HostResolver> resolver) {
    environment.external_endpoint = [resolver](const GridRegion& region) -> std::optional<RegionEndpoint> {
        if (!region.internal_endpoint)
            throw std::invalid_argument("Null region internal endpoint");
        auto& service = resolver ? *resolver : framework::host_resolver();
        auto endpoint = service.endpoint(region.external_host, region.internal_endpoint->port);
        return endpoint ? std::optional<RegionEndpoint>({endpoint->address->str(), endpoint->port})
                        : std::nullopt;
    };
    environment.server_uri = [](const GridRegion& region) {
        return std::optional<std::string>(region.server_uri());
    };
}
void bind_login_response_timezones(LoginResponseEnvironment& environment,
                                   std::shared_ptr<framework::TimeZoneDatabase> database,
                                   std::function<std::int64_t()> clock) {
    if (!clock)
        clock = []() {
            return std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                .count();
        };
    environment.local_daylight = [database, clock]() {
        auto& service = database ? *database : framework::time_zones();
        return service.local()->daylight_at(clock());
    };
    environment.find_timezone = [database,
                                 clock](const std::string& id) -> LoginResponseEnvironment::Daylight {
        auto& service = database ? *database : framework::time_zones();
        auto zone = service.find(id);
        return [zone, clock]() { return zone->daylight_at(clock()); };
    };
}
std::shared_ptr<LoginResponseLibrary> LoginResponseLibrary::bind(const LibraryService& service) {
    auto result = std::make_shared<LoginResponseLibrary>();
    result->root = [service]() -> std::shared_ptr<LoginInventoryFolder> {
        auto folder = service.root();
        return folder ? std::make_shared<LoginInventoryFolder>(*folder) : nullptr;
    };
    result->folders = [service]() -> std::optional<Folders> {
        Folders result;
        for (const auto& folder : service.ordered_folders())
            result.push_back(folder ? std::make_shared<LoginInventoryFolder>(*folder) : nullptr);
        return result;
    };
    return result;
}
LoginResponse::LoginResponse(const LoginResponseInput& input, const LoginResponseEnvironment& environment,
                             Random random, Clock clock)
    : LoginResponse(std::move(random), std::move(clock)) {
    // Only skeleton conversion is caught by FillOutInventoryData in the source.
    try {
        if (!input.skeleton)
            throw std::invalid_argument("Null inventory skeleton");
        framework::Uuid root;
        auto skeleton = array();
        for (const auto& folder : *input.skeleton) {
            if (!folder)
                throw std::invalid_argument("Null inventory folder");
            if (folder->parent.is_zero() && folder->name == Text("My Inventory"))
                root = folder->id;
            skeleton.array.push_back(folder_value(*folder));
        }
        inventory_root = std::make_shared<Value>(array());
        inventory_root->array.push_back(Value::object({{"folder_id", root.str()}}));
        inventory_skeleton = std::make_shared<Value>(std::move(skeleton));
    } catch (const std::exception&) {
    }
    if (input.library && input.library->root()) {
        inventory_library_root = std::make_shared<Value>(array());
        inventory_library_root->array.push_back(
            Value::object({{"folder_id", "00000112-000f-0000-0000-000100bba000"}}));
        input.library->root(); // The second getter is evaluated although GetLibraryOwner ignores it.
        inventory_library_owner = std::make_shared<Value>(array());
        inventory_library_owner->array.push_back(
            Value::object({{"agent_id", "11111111-1111-0000-0000-000100bba000"}}));
        auto folders = input.library->folders();
        if (!folders)
            throw std::invalid_argument("Null library folders");
        auto library = array();
        for (const auto& folder : *folders) {
            if (!folder)
                throw std::invalid_argument("Null library folder");
            library.array.push_back(folder_value(*folder));
        }
        inventory_library = std::make_shared<Value>(std::move(library));
    }
    auto gestures = array();
    if (input.gestures)
        for (const auto& item : *input.gestures) {
            if (!item)
                throw std::invalid_argument("Null gesture");
            gestures.array.push_back(
                Value::object({{"item_id", item->id.str()}, {"asset_id", item->asset_id.str()}}));
        }
    active_gestures = std::make_shared<Value>(std::move(gestures));
    if (!input.circuit)
        throw std::invalid_argument("Null circuit");
    circuit_code = std::bit_cast<std::int32_t>(input.circuit->circuit_code);
    if (!input.account)
        throw std::invalid_argument("Null account");
    last_name = input.account->last_name;
    first_name = input.account->first_name;
    agent = framework::Uuid(input.account->principal_id);
    session = input.circuit->session_id;
    secure_session = input.circuit->secure_session_id;
    real_id = input.real_id;
    message = input.message;
    if (!input.friends)
        throw std::invalid_argument("Null friends");
    buddy_list = std::make_shared<LoginBuddyList>();
    for (const auto& friend_info : *input.friends) {
        if (!friend_info)
            throw std::invalid_argument("Null friend");
        if (friend_info->their_flags == -1)
            continue;
        auto buddy = std::make_shared<LoginBuddyInfo>();
        buddy->id = friend_info->friend_id;
        framework::Uuid id;
        if (!universal_id(friend_info->friend_id, id))
            continue;
        buddy->id = id.str();
        buddy->rights_have = friend_info->their_flags;
        buddy->rights_given = friend_info->my_flags;
        buddy_list->add(buddy);
    }
    start_location = input.where;
    map_url = input.map_url;
    profile_url = input.profile_url;
    openid_url = input.openid_url;
    destinations_url = input.destinations_url;
    avatars_url = input.avatars_url;
    search_url = input.search_url;
    currency = input.currency;
    classified_fee = input.classified_fee;
    max_agent_groups = input.max_agent_groups;
    if (!input.grid_user)
        throw std::invalid_argument("Null grid user");
    const auto& p = *input.grid_user;
    const auto x = input.home ? input.home->x : 256000, y = input.home ? input.home->y : 256000;
    home = "{'region_handle':[r" + std::to_string(x) + ",r" + std::to_string(y) + "], 'position':[r" +
           number(p.home_position.x) + ",r" + number(p.home_position.y) + ",r" + number(p.home_position.z) +
           "], 'look_at':[r" + number(p.home_look_at.x) + ",r" + number(p.home_look_at.y) + ",r" +
           number(p.home_look_at.z) + "]}";
    look_at = "[r" + number(input.look_at.x) + ",r" + number(input.look_at.y) + ",r" +
              number(input.look_at.z) + "]";
    if (!input.destination)
        throw std::invalid_argument("Null destination");
    if (!environment.external_endpoint)
        throw std::invalid_argument("Response endpoint provider unavailable");
    auto endpoint = environment.external_endpoint(*input.destination);
    if (endpoint) {
        sim_address = endpoint->address;
        sim_port = endpoint->port;
        region_x = static_cast<std::uint32_t>(input.destination->x);
        region_y = static_cast<std::uint32_t>(input.destination->y);
        region_size_x = input.destination->size_x;
        region_size_y = input.destination->size_y;
    }
    const auto uri = environment.server_uri ? environment.server_uri(*input.destination)
                                            : Text(input.destination->server_uri());
    seed_capability = uri.value_or("") + "CAPS/" + input.circuit->caps_path.value_or("") + "0000/";
    if (input.dst_zone == Text("none")) {
        dst = "N";
        return;
    }
    if (input.dst_zone == Text("local")) {
        if (!environment.local_daylight)
            throw std::invalid_argument("Local timezone provider unavailable");
        dst = environment.local_daylight() ? "Y" : "N";
        return;
    }
    if (!input.dst_zone)
        throw std::invalid_argument("Null DST zone");
    if (!environment.find_timezone)
        throw std::invalid_argument("Named timezone provider unavailable");
    LoginResponseEnvironment::Daylight selected;
    std::size_t start = 0;
    const auto& zones = *input.dst_zone;
    while (true) {
        auto end = zones.find(';', start);
        try {
            selected =
                environment.find_timezone(zones.substr(start, end == std::string::npos ? end : end - start));
            break;
        } catch (const std::exception&) {
        }
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    if (selected)
        dst = selected() ? "Y" : "N";
    else {
        if (!environment.local_daylight)
            throw std::invalid_argument("Local timezone provider unavailable");
        dst = environment.local_daylight() ? "Y" : "N";
    }
}
}
