// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/presence_connector.hpp"

#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/http_host.hpp"
#include "opensim/framework/sdk_uuid.hpp"

namespace opensim::services {
namespace {
const network::ServiceXmlValue* field(const network::ServiceXmlValue& value, std::string_view name) {
    for (const auto& [key, child] : value.fields)
        if (key == name)
            return &child;
    return nullptr;
}

PresenceInfo presence_info(const network::ServiceXmlValue& value) {
    PresenceInfo info;
    if (const auto* user = field(value, "UserID"))
        info.user_id = user->text;
    if (const auto* region = field(value, "RegionID"))
        framework::try_parse_sdk_uuid(region->text, info.region_id);
    return info;
}
}

PresenceConnector::PresenceConnector(const server::IniConfig& config,
                                     std::shared_ptr<network::WebHttpClients> clients,
                                     std::function<void(const std::string&)> log)
    : endpoint_(config.get("PresenceService", "PresenceServerURI")), log_(std::move(log)) {
    if (!framework::HttpHost(endpoint_).valid())
        throw std::invalid_argument("PresenceService.PresenceServerURI must be an absolute HTTP URL");
    while (endpoint_.ends_with('/'))
        endpoint_.pop_back();
    endpoint_ += "/presence";
    const auto setting = [&](const char* key) {
        return config.get("PresenceService", key, config.get("Network", key));
    };
    std::optional<std::pair<std::string, std::string>> auth;
    if (setting("AuthType") == "BasicHttpAuthentication")
        auth.emplace(setting("HttpAuthUsername"), setting("HttpAuthPassword"));
    request_ = network::rest_forms_transport(std::move(clients), std::move(auth));
}

std::string PresenceConnector::post(std::string_view method, const Fields& fields,
                                    const std::vector<std::string>& users) {
    Fields form{{"VERSIONMIN", "1"}, {"VERSIONMAX", "1"}, {"METHOD", std::string(method)}};
    form.insert(form.end(), fields.begin(), fields.end());
    auto body = network::build_service_form(form);
    for (const auto& user : users)
        body += "&uuids[]=" + network::service_url_encode(user);
    try {
        return request_(endpoint_, body);
    } catch (const std::exception& error) {
        if (log_)
            log_("Presence connector " + std::string(method) + ": " + error.what());
        return {};
    }
}

bool PresenceConnector::simple(std::string_view method, const Fields& fields) {
    auto reply = post(method, fields);
    for (auto& character : reply)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    const auto success = reply.find("success");
    return success != std::string::npos && success != 0;
}

network::ServiceXmlValue PresenceConnector::query(std::string_view method, const Fields& fields,
                                                 const std::vector<std::string>& users) {
    try {
        return network::parse_service_xml(post(method, fields, users));
    } catch (const std::exception& error) {
        if (log_)
            log_("Presence connector " + std::string(method) + ": " + error.what());
        return {};
    }
}

bool PresenceConnector::login(const std::string& user, const framework::Uuid& session,
                             const framework::Uuid& secure_session) {
    return simple("login", {{"UserID", user}, {"SessionID", session.str()},
                            {"SecureSessionID", secure_session.str()}});
}

bool PresenceConnector::logout(const framework::Uuid& session) {
    return simple("logout", {{"SessionID", session.str()}});
}

bool PresenceConnector::logout_from_region(const framework::Uuid& session, const framework::Uuid& region) {
    const auto current = agent(session);
    return current && current->region_id == region && logout(session);
}

bool PresenceConnector::logout_region(const framework::Uuid& region) {
    return simple("logoutregion", {{"RegionID", region.str()}});
}

bool PresenceConnector::report(const framework::Uuid& session, const framework::Uuid& region) {
    const auto response = query("report", {{"SessionID", session.str()}, {"RegionID", region.str()}});
    const auto* result = field(response, "result");
    return result && !result->list && framework::invariant_equals_ignore_case(result->text, "success");
}

std::optional<PresenceInfo> PresenceConnector::agent(const framework::Uuid& session) {
    const auto response = query("getagent", {{"SessionID", session.str()}});
    const auto* result = field(response, "result");
    return result && result->list ? std::optional(presence_info(*result)) : std::nullopt;
}

std::vector<PresenceInfo> PresenceConnector::agents(const std::vector<std::string>& users) {
    if (users.empty())
        return {};
    const auto response = query("getagents", {}, users);
    if (const auto* result = field(response, "result");
        result && !result->list && (result->text == "null" || result->text == "Failure"))
        return {};
    std::vector<PresenceInfo> result;
    for (const auto& [key, entry] : response.fields)
        if (entry.list)
            result.push_back(presence_info(entry));
    return result;
}
}
