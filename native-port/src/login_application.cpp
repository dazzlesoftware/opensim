/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/server/login_application.hpp"
#include "opensim/framework/ip_address.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/services/user_account.hpp"
#include <chrono>
#include <limits>
#include <stdexcept>

namespace opensim::server {
using namespace network;
namespace {
void require_rpc_struct(const Value& data) {
    if (data.view().kind != Value::Kind::Map)
        throw std::invalid_argument("Hypergrid request requires an XML-RPC struct");
}

void check_rpc_string(const Value& data, std::string_view key) {
    const auto kind = data.get(key).view().kind;
    if (kind != Value::Kind::String && kind != Value::Kind::Undefined)
        throw std::invalid_argument("Invalid Hypergrid XML-RPC field type");
}
}

LoginApplication::LoginApplication(services::ILoginService& login, const IniConfig& config)
    : grid_statistics_disabled_(config.boolean("GridInfoService", "DisableStatsEndpoint", false)),
      handlers_(login), grid_info_(config.section("GridInfoService")) {
    if (!grid_info_.contains("platform"))
        grid_info_["platform"] = "OpenSim";
    const auto discovery_uri = [&](const char* key, const std::string& fallback = {}) {
        return config.get("Hypergrid", key, config.get("Startup", key, fallback));
    };
    const auto configured = [&](const char* key) {
        const auto found = grid_info_.find(key);
        return found != grid_info_.end() ? found->second : std::string{};
    };
    auto home = discovery_uri("HomeURI", configured("home"));
    if (home.empty())
        home = config.get("LoginService", "SRV_HomeURI");
    if (!home.empty())
        grid_info_["home"] = home;
    const auto gatekeeper = discovery_uri("GatekeeperURI", configured("gatekeeper"));
    if (!gatekeeper.empty())
        grid_info_["gatekeeper"] = gatekeeper;
    for (const auto& [config_key, response_key] :
         {std::pair{"HomeURIAlias", "homealias"}, std::pair{"GatekeeperURIAlias", "gatekeeperalias"}}) {
        const auto value = discovery_uri(config_key);
        if (!value.empty())
            grid_info_[response_key] = value;
    }
    grid_info_xml(grid_info_); // Reject invalid XML keys at startup, before accepting traffic.
}
HttpResponse LoginApplication::handle_grid_statistics(const HttpRequest& request) {
    HttpResponse response;
    if (grid_statistics_disabled_) {
        response.status = 404;
        return response;
    }
    if (request.method != "GET" || !statistics_grid_ || !statistics_users_) {
        response.status = 405;
        return response;
    }

    std::lock_guard lock(grid_statistics_mutex_);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (grid_statistics_xml_.empty() || now - grid_statistics_updated_ > 900) {
        std::int64_t region_count = 0, residents = 0, active_users = 0;
        try {
            for (const auto& region : statistics_grid_->online({}, 0, 0, std::numeric_limits<int>::max()))
                region_count += (static_cast<std::int64_t>(region.size_x) * region.size_y) >> 16;
            for (const auto& user : statistics_users_->get_all("")) {
                if (!user || !user->user_id || !user->data)
                    continue;
                if (user->user_id->find(';') == std::string::npos)
                    ++residents;
                const auto login = user->data->find("Login");
                std::int32_t last_login = 0;
                if (login != user->data->end() && login->second &&
                    services::parse_account_integer(*login->second, last_login) &&
                    last_login != 0 && last_login > now - 2592000)
                    ++active_users;
            }
        } catch (const std::exception&) {
            // The managed endpoint publishes counts accumulated before a provider failure.
        }
        grid_statistics_xml_ = "<gridstats><residents>" + std::to_string(residents) +
            "</residents><active_users>" + std::to_string(active_users) +
            "</active_users><region_count>" + std::to_string(region_count) + "</region_count></gridstats>";
        grid_statistics_updated_ = now;
    }
    response.content_type = "application/xml";
    response.body = grid_statistics_xml_;
    return response;
}
void LoginApplication::set_default_login_uri(const std::string& uri) {
    std::lock_guard lock(grid_info_mutex_);
    if (!grid_info_.contains("login"))
        grid_info_["login"] = uri;
}
std::string LoginApplication::grid_discovery_rpc_response() const {
    std::lock_guard lock(grid_info_mutex_);
    std::map<std::string, Value> info;
    for (const auto& [key, value] : grid_info_)
        info[key] = Value(value);
    return xml_rpc_response(Value::object(std::move(info)));
}

std::optional<HttpResponse> LoginApplication::handle_grid_discovery(const HttpRequest& request,
                                                                   bool include_xml_rpc) {
    if (request.path == "/get_grid_stats")
        return handle_grid_statistics(request);
    HttpResponse response;
    response.headers["Cache-Control"] = "no-store";
    if (request.path == "/get_grid_info" || request.path == "/json_grid_info") {
        if (request.method != "GET") {
            response.status = 405;
            response.headers["Allow"] = "GET";
        } else if (request.path == "/get_grid_info") {
            std::lock_guard lock(grid_info_mutex_);
            response.content_type = "application/xml";
            response.body = grid_info_xml(grid_info_);
        } else {
            std::lock_guard lock(grid_info_mutex_);
            response.content_type = "application/json";
            response.body = json_strings(grid_info_);
        }
        return response;
    }
    if (!include_xml_rpc || request.method != "POST" || (request.path != "/" && request.path != "/login"))
        return std::nullopt;
    try {
        if (parse_xml_rpc(request.body).method != "get_grid_info")
            return std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
    response.content_type = "text/xml";
    response.headers["Access-Control-Allow-Origin"] = "*";
    response.body = grid_discovery_rpc_response();
    return response;
}

HttpResponse LoginApplication::handle_gatekeeper(const HttpRequest& request) {
    HttpResponse response;
    response.status = 404;
    if (!gatekeeper_ || request.method != "POST" || request.path != "/")
        return response;
    RpcCall call;
    try {
        call = parse_xml_rpc(request.body);
    } catch (const std::exception&) {
        return response;
    }
    response.status = 200;
    response.content_type = "text/xml";
    if (call.method != "link_region" && call.method != "get_region") {
        response.body = xml_rpc_fault(-32601, "Requested method [" + call.method + "] not found");
        return response;
    }
    try {
        const Value empty;
        response.body = xml_rpc_response(
            gatekeeper_rpc(call.method, call.parameters.empty() ? empty : call.parameters.front()));
    } catch (const std::exception&) {
        response.body = xml_rpc_fault(-32603, "Error processing XML-RPC request");
    }
    return response;
}

Value LoginApplication::gatekeeper_rpc(std::string_view method, const Value& data) {
    require_rpc_struct(data);
    if (method == "link_region") {
        check_rpc_string(data, "region_name");
    } else {
        check_rpc_string(data, "region_uuid");
        check_rpc_string(data, "agent_id");
        check_rpc_string(data, "agent_home_uri");
    }
    return gatekeeper_(method, data);
}

std::optional<Value> LoginApplication::home_agent_rpc(std::string_view method, const Value& data,
                                                     std::string_view caller) {
    if (home_agent_verifier_ &&
        (method == "verify_agent" || method == "verify_client" ||
         method == "agent_is_coming_home" || method == "logout_agent")) {
        require_rpc_struct(data);
        check_rpc_string(data, "sessionID");
        if (method == "logout_agent")
            check_rpc_string(data, "userID");
        else if (method == "agent_is_coming_home")
            check_rpc_string(data, "externalName");
        else
            check_rpc_string(data, "token");
        const auto success = home_agent_verifier_(method, data);
        return Value::object({{"result", success ? (method == "logout_agent" ? "true" : "True") : "False"}});
    }
    if (method == "get_home_region" && home_region_lookup_)
        return home_region_lookup_(data);
    if (method == "get_online_friends" && home_user_lookup_)
        return Value::object({});
    if (home_user_lookup_ &&
        (method == "get_uuid" || method == "locate_user" || method == "get_server_urls" ||
         method == "get_user_info" || method == "get_uui" || method == "status_notification"))
        return home_user_lookup_(method, data, caller);
    return std::nullopt;
}

std::optional<HttpResponse> LoginApplication::handle_home_agent(const HttpRequest& request) {
    if (request.method != "POST" || request.path != "/")
        return std::nullopt;
    RpcCall call;
    try {
        call = parse_xml_rpc(request.body);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    HttpResponse response;
    response.content_type = "text/xml";
    try {
        const Value empty;
        const auto result = home_agent_rpc(call.method, call.parameters.empty() ? empty : call.parameters.front(),
                                           request.remote_ip);
        if (!result)
            return std::nullopt;
        response.body = xml_rpc_response(*result);
    } catch (const std::exception&) {
        response.body = xml_rpc_fault(-32603, "Error processing XML-RPC request");
    }
    return response;
}

HttpResponse LoginApplication::handle(const HttpRequest& request, bool allow_gatekeeper,
                                      bool allow_grid_discovery, bool allow_home_agent) {
    if (request.path.starts_with("/auth") && authentication_)
        return authentication_->handle(request);
    if (request.path == "/friends" && friends_)
        return friends_->handle(request);
    if (request.path == "/offlineim" && offline_im_)
        return offline_im_->handle(request);
    if (request.path == "/avatar" && avatar_)
        return avatar_->handle(request);
    if (request.path == "/accounts" && accounts_)
        return accounts_->handle(request);
    if (request.path == "/presence" && presence_)
        return presence_->handle(request);
    if (request.path == "/grid" && grid_)
        return grid_->handle(request);
    if (request.path == "/griduser" && grid_user_)
        return grid_user_->handle(request);
    HttpResponse response;
    response.headers["Cache-Control"] = "no-store";
    if (request.path == "/health" || request.path == "/ready") {
        if (request.method != "GET" && request.method != "HEAD") {
            response.status = 405;
            response.headers["Allow"] = "GET, HEAD";
            return response;
        }
        response.content_type = "application/json";
        response.status = request.path == "/ready" ? 503 : 200;
        response.body = "{\"service\":\"OpenSimNative\",\"running\":true,"
                        "\"authentication_ready\":true,\"simulation_ready\":false,"
                        "\"login_success_available\":false}";
        return response;
    }
    if (allow_grid_discovery)
        if (auto discovery = handle_grid_discovery(request, false))
            return std::move(*discovery);
    if (request.content_type == "application/json-rpc")
        return json_rpc_.handle(request);
    if (request.path != "/" && request.path != "/login") {
        response.status = 404;
        return response;
    }
    if (request.method != "POST") {
        response.status = 405;
        response.headers["Allow"] = "POST";
        return response;
    }
    auto content = request.content_type.substr(0, request.content_type.find(';'));
    for (auto& c : content)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    if (content == "application/llsd+xml") {
        response.content_type = "application/llsd+xml";
        try {
            const auto value = deserialize_sdk_llsd_xml(request.body);
            if (value.kind != Value::Kind::Map) {
                response.status = 400;
                return response;
            }
            response.body = llsd_xml(handlers_.llsd_login(value, request.remote_ip));
        } catch (const std::exception&) {
            response.status = 400;
        }
        return response;
    }
    response.content_type = "text/xml";
    RpcCall call;
    try {
        call = parse_xml_rpc(request.body);
    } catch (const std::exception&) {
        response.status = 404;
        return response;
    }
    try {
        const Value empty;
        const auto& data = call.parameters.empty() ? empty : call.parameters.front();
        if (call.method == "login_to_simulator") {
            auto client_ip = request.remote_ip;
            // BaseHttpServer passes XFF as Params[3]; LLLoginHandlers uses
            // Util.GetClientIPFromXFF's first comma-delimited address, untrimmed.
            if (const auto header = request.headers.find("x-forwarded-for");
                header != request.headers.end()) {
                try {
                    const auto first = header->second.substr(0, header->second.find(','));
                    if (const auto address = framework::IpAddress::parse(first))
                        client_ip = address->str();
                } catch (const std::exception&) {
                } // Source retains the transport peer on parse failure.
            }
            response.body = xml_rpc_response(handlers_.xml_rpc_login(data, client_ip));
        } else if (call.method == "grid_instant_message" && grid_message_)
            response.body =
                xml_rpc_response(Value::object({{"success", grid_message_(data) ? "TRUE" : "FALSE"}}));
        else if (const auto home = allow_home_agent ? home_agent_rpc(call.method, data, request.remote_ip)
                                                    : std::nullopt; home)
            response.body = xml_rpc_response(*home);
        else if (call.method == "land_data" && land_data_lookup_)
            response.body = xml_rpc_response(land_data_lookup_(data));
        else if (allow_gatekeeper && (call.method == "link_region" || call.method == "get_region") &&
                 gatekeeper_)
            response.body = xml_rpc_response(gatekeeper_rpc(call.method, data));
        else if (call.method == "set_login_level")
            response.body = xml_rpc_response(handlers_.set_level(data));
        else if (allow_grid_discovery && call.method == "get_grid_info")
            response.body = grid_discovery_rpc_response();
        else
            response.body = xml_rpc_fault(-32601, "Requested method [" + call.method + "] not found");
    } catch (const std::exception&) {
        response.body = xml_rpc_fault(-32603, "Error processing XML-RPC request");
    }
    response.headers["Access-Control-Allow-Origin"] = "*";
    return response;
}
}
