// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/grid_user_connector.hpp"

#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/expiring_cache.hpp"
#include "opensim/framework/http_host.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/vector_text.hpp"

#include <utility>

namespace opensim::services {
namespace {
constexpr int cache_seconds = 30;

const network::ServiceXmlValue* field(const network::ServiceXmlValue& value, std::string_view name) {
    for (const auto& [key, child] : value.fields)
        if (key == name)
            return &child;
    return nullptr;
}

std::int32_t timestamp(const std::string& text) {
    framework::AssetDateTime parsed;
    const auto context = framework::shared_native_invariant_datetime_context()->callbacks();
    if (!framework::try_parse_invariant_datetime_utf8(text, parsed, context))
        return 0;
    constexpr std::int64_t unix_epoch_ticks = 621355968000000000LL;
    const auto seconds = (parsed.ticks() - unix_epoch_ticks) / 10000000;
    return std::in_range<std::int32_t>(seconds) ? static_cast<std::int32_t>(seconds) : 0;
}

GridUserInfo read_info(const network::ServiceXmlValue& entry) {
    GridUserInfo info;
    if (const auto* user = field(entry, "UserID"))
        info.user_id = user->text;
    const auto uuid = [&](const char* name, framework::Uuid& value) {
        if (const auto* item = field(entry, name))
            framework::try_parse_sdk_uuid(item->text, value);
    };
    const auto vector = [&](const char* name, framework::Vector3& value) {
        if (const auto* item = field(entry, name))
            framework::try_parse_sdk_vector3(framework::utf16_replacing(item->text), value);
    };
    uuid("HomeRegionID", info.home_region);
    uuid("LastRegionID", info.last_region);
    vector("HomePosition", info.home_position);
    vector("HomeLookAt", info.home_look_at);
    vector("LastPosition", info.last_position);
    vector("LastLookAt", info.last_look_at);
    if (const auto* online = field(entry, "Online"))
        info.online = framework::invariant_equals_ignore_case(framework::trim_utf8(online->text), "true");
    if (const auto* login = field(entry, "Login"))
        info.login = timestamp(login->text);
    if (const auto* logout = field(entry, "Logout"))
        info.logout = timestamp(logout->text);
    return info;
}

std::optional<GridUserInfo> result_info(const network::ServiceXmlValue& response) {
    const auto* result = field(response, "result");
    return result && result->list ? std::optional(read_info(*result)) : std::nullopt;
}
}

struct GridUserConnector::Cache {
    framework::ExpiringCacheOS<std::string, std::optional<GridUserInfo>> records{10000};
};

GridUserConnector::GridUserConnector(const server::IniConfig& config,
                                     std::shared_ptr<network::WebHttpClients> clients,
                                     std::function<void(const std::string&)> log)
    : cache_(std::make_unique<Cache>()), log_(std::move(log)) {
    const framework::HttpHost host(config.get("GridUserService", "GridUserServerURI"), true);
    if (!host.resolved())
        throw std::invalid_argument("GridUserService.GridUserServerURI must resolve to an HTTP host");
    endpoint_ = host.uri + "/griduser";
    const auto setting = [&](const char* key) {
        return config.get("GridUserService", key, config.get("Network", key));
    };
    std::optional<std::pair<std::string, std::string>> auth;
    if (setting("AuthType") == "BasicHttpAuthentication")
        auth.emplace(setting("HttpAuthUsername"), setting("HttpAuthPassword"));
    request_ = network::rest_forms_transport(std::move(clients), std::move(auth));
}

GridUserConnector::~GridUserConnector() = default;

network::ServiceXmlValue GridUserConnector::query(std::string_view method, const Fields& fields,
                                                  const std::vector<std::string>& users) {
    Fields form{{"VERSIONMIN", "1"}, {"VERSIONMAX", "1"}, {"METHOD", std::string(method)}};
    form.insert(form.end(), fields.begin(), fields.end());
    auto body = network::build_service_form(form);
    for (const auto& user : users)
        body += "&AgentIDs[]=" + network::service_url_encode(user);
    try {
        return network::parse_service_xml(request_(endpoint_, body));
    } catch (const std::exception& error) {
        if (log_)
            log_("Grid-user connector " + std::string(method) + ": " + error.what());
        return {};
    }
}

std::optional<GridUserInfo> GridUserConnector::logged_in(const std::string& user) {
    return result_info(query("loggedin", {{"UserID", user}}));
}

std::optional<GridUserInfo> GridUserConnector::info(const std::string& user) {
    std::optional<GridUserInfo> result;
    if (!cache_->records.try_get_value(user, cache_seconds * 1000, result)) {
        result = result_info(query("getgriduserinfo", {{"UserID", user}}));
        cache_->records.add_or_update(user, result, cache_seconds);
    }
    return result;
}

std::vector<GridUserInfo> GridUserConnector::info(const std::vector<std::string>& users) {
    const auto response = query("getgriduserinfos", {}, users);
    if (const auto* result = field(response, "result");
        result && !result->list && (result->text == "null" || result->text == "Failure"))
        return {};
    std::vector<GridUserInfo> result;
    for (const auto& entry : response.fields)
        if (entry.second.list)
            result.push_back(read_info(entry.second));
    return result;
}

bool GridUserConnector::set(std::string_view method, const std::string& user, framework::Uuid region,
                            framework::Vector3 position, framework::Vector3 look) {
    const auto response = query(method, {{"UserID", user}, {"RegionID", region.str()},
        {"Position", framework::vector3_string(position)}, {"LookAt", framework::vector3_string(look)}});
    const auto* result = field(response, "result");
    return result && !result->list && framework::invariant_equals_ignore_case(result->text, "success");
}

bool GridUserConnector::logged_out(const std::string& user, framework::Uuid region,
                                   framework::Vector3 position, framework::Vector3 look) {
    cache_->records.remove(user);
    return set("loggedout", user, region, position, look);
}

bool GridUserConnector::set_home(const std::string& user, framework::Uuid region,
                                 framework::Vector3 position, framework::Vector3 look) {
    if (!set("sethome", user, region, position, look))
        return false;
    std::optional<GridUserInfo> info;
    if (cache_->records.try_get_value(user, cache_seconds * 1000, info) && info) {
        info->home_region = region;
        info->home_position = position;
        info->home_look_at = look;
        cache_->records.add_or_update(user, info, cache_seconds);
    }
    return true;
}

bool GridUserConnector::set_last_position(const std::string& user, framework::Uuid region,
                                          framework::Vector3 position, framework::Vector3 look) {
    if (!set("setposition", user, region, position, look))
        return false;
    std::optional<GridUserInfo> info;
    if (cache_->records.try_get_value(user, cache_seconds * 1000, info) && info) {
        info->last_region = region;
        info->last_position = position;
        info->last_look_at = look;
        cache_->records.add_or_update(user, info, cache_seconds);
    }
    return true;
}
}
