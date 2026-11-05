// Copyright (c) Contributors, http://opensimulator.org/
// Adapted from Services/Connectors/Hypergrid/UserAgentServiceConnector.cs.
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/account_store.hpp"
#include "opensim/services/user_agent_connector.hpp"
#include "opensim/services/open_profile_client.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/viewer_name.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <algorithm>
#include "opensim/framework/nini_xml_config.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/nini_xml_text_writer.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/integer_text.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/tick_count.hpp"
#include "opensim/server/grid_identity.hpp"
namespace opensim::services {
std::function<framework::Uuid(std::string_view, std::string_view, std::string_view)>
make_script_user_key_lookup(AccountStore& accounts, std::shared_ptr<network::WebHttpClients> clients) {
    auto script_user_keys = std::make_shared<std::vector<std::pair<std::string, framework::Uuid>>>();
    return [&accounts, script_user_keys, clients = std::move(clients)](
        std::string_view first, std::string_view last, std::string_view server) {
        const auto account_first = server.empty() ? std::string(first)
                                                   : std::string(first) + "." + std::string(last);
        const auto account_last = server.empty() ? std::string(last) : "@" + std::string(server);
        const auto name = account_first + " " + account_last;
        for (const auto& [cached_name, id] : *script_user_keys)
            if (framework::invariant_equals_ignore_case(cached_name, name))
                return id;
        framework::Uuid id;
        if (const auto account = accounts.find(framework::Uuid{}.str(), account_first, account_last))
            framework::try_parse_sdk_uuid(account->principal_id, id);
        if (id.is_zero() && !server.empty()) {
            try {
                auto transport = std::make_shared<network::NwcXmlRpcHttpTransport>(clients);
                services::UserAgentConnectorServices connector;
                connector.debug = connector.error = connector.warn = [](const auto&) {};
                connector.client = [transport](std::int32_t timeout) {
                    return network::xml_rpc_http_client(transport, timeout);
                };
                services::UserAgentServiceConnector remote(
                    framework::utf16_replacing("http://" + std::string(server)), std::move(connector));
                id = remote.get_uuid(framework::utf16_replacing(first), framework::utf16_replacing(last));
            } catch (const std::exception&) {
                return framework::Uuid{};
            }
        }
        if (!id.is_zero())
            script_user_keys->emplace_back(name, id);
        return id;
    };
}

namespace {
using network::Value;
using Text = UserAgentConnectorServices::Text;
using Kind = Value::Kind;
[[noreturn]] void error(std::u16string text) {
    throw network::NwcXmlRpcError("System.Exception", std::move(text));
}
[[noreturn]] void null_reference() {
    throw network::NwcXmlRpcError("System.NullReferenceException",
                                  u"Object reference not set to an instance of an object.");
}
bool is_null(const Value& v) {
    if (v.reference)
        return is_null(*v.reference);
    return v.kind == Kind::NullReference || v.kind == Kind::Undefined;
}
Value boxed(const Text& t) {
    if (!t)
        return Value::null_reference();
    Value v(framework::utf8(*t));
    v.sdk_string = std::make_shared<const std::u16string>(*t);
    return v;
}
std::shared_ptr<Value> user_request(const framework::Uuid& id) {
    return std::make_shared<Value>(Value::object({{"userID", Value(id.str())}}));
}
bool whitespace(char16_t c) {
    return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f ||
           c == 0x3000;
}
std::u16string boxed_type(const Value& v) {
    switch (v.view().kind) {
    case Kind::Map:
        return u"System.Collections.Hashtable";
    case Kind::Array:
        return u"System.Collections.ArrayList";
    case Kind::Integer:
        return u"System.Int32";
    case Kind::Long:
        return u"System.Int64";
    case Kind::Boolean:
        return u"System.Boolean";
    case Kind::Real:
        return u"System.Double";
    case Kind::Date:
        return u"System.DateTime";
    case Kind::Binary:
        return u"System.Byte[]";
    default:
        return u"System.String";
    }
}
[[noreturn]] void cast_error(const Value& v, std::u16string_view target) {
    throw network::NwcXmlRpcError("System.InvalidCastException", u"Unable to cast object of type '" +
                                                                     boxed_type(v) + u"' to type '" +
                                                                     std::u16string(target) + u"'.");
}
Text string_cast(const Value& v) {
    if (is_null(v))
        return {};
    if (v.view().kind != Kind::String)
        cast_error(v, u"System.String");
    return v.view().sdk_string ? *v.view().sdk_string : framework::utf16_replacing(v.view().text);
}
bool bool_parse(Text text) {
    if (!text)
        return false;
    std::u16string_view t = *text;
    while (!t.empty() && (t.front() == 0 || whitespace(t.front())))
        t.remove_prefix(1);
    while (!t.empty() && (t.back() == 0 || whitespace(t.back())))
        t.remove_suffix(1);
    return framework::ordinal_equals_ignore_case(framework::utf8(t), "true");
}
std::shared_ptr<network::NwcXmlRpcRequest> request(std::u16string method, Value fields) {
    auto args = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
    args->push_back(std::make_shared<Value>(std::move(fields)));
    return std::make_shared<network::NwcXmlRpcRequest>(std::move(method), args);
}
std::u16string message(std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (const network::NwcXmlRpcError& ex) {
        return ex.message;
    } catch (const OpenProfileError& ex) {
        return ex.message;
    } catch (const framework::ConfigUriComponentsError& ex) {
        return ex.message;
    } catch (const framework::ConfigXmlHttpError& ex) {
        return ex.message;
    } catch (const framework::ConfigHttpHeaderError& ex) {
        return ex.message;
    } catch (const framework::NiniXmlProviderError& ex) {
        return ex.message;
    } catch (const framework::XmlTextWriterError& ex) {
        return ex.message;
    } catch (const framework::XmlEncoderError& ex) {
        return ex.message;
    } catch (const framework::InvariantNumberError& ex) {
        return ex.message;
    } catch (const std::exception& ex) {
        return framework::utf16_replacing(ex.what());
    }
}
}
UserAgentServiceConnector::UserAgentServiceConnector(Text url, UserAgentConnectorServices services,
                                                     bool configuration)
    : SimulationConnector(services.simulation_request, {}, {}, services.simulation_options),
      services_(std::move(services)) {
    initialise_services();
    configure_url(url, configuration);
}
UserAgentServiceConnector::UserAgentServiceConnector(std::shared_ptr<framework::NiniConfigSourceBase> config,
                                                     UserAgentConnectorServices services,
                                                     framework::RegionUri::Resolver resolver)
    : SimulationConnector(services.simulation_request, {}, {}, services.simulation_options),
      services_(std::move(services)) {
    initialise_services();
    if (!config)
        null_reference();
    Text home;
    try {
        server::GridIdentity grid(server::IniConfig::from_native_source(std::move(config)), std::string{},
                                  std::move(resolver),
                                  [this](auto message) { services_.error(std::u16string(message)); });
        home = framework::utf16(grid.home_uri_slash());
    } catch (const server::GridIdentityError& e) {
        error(framework::utf16(e.what()));
    }
    configure_url(home, true);
}
void UserAgentServiceConnector::initialise_services() {
    auto& s = services_;
    if (!s.client || !s.debug || !s.error || !s.warn)
        throw std::invalid_argument("User-agent client, debug, error and warning loggers are required");
    if (!s.lower)
        s.lower = [](auto t) { return framework::lower_invariant_culture(t); };
    if (!s.validate_uri) {
        auto uris = framework::native_config_uri_object_backend();
        s.validate_uri = [uris](auto text) {
            uris->create(std::make_shared<const std::u16string>(text),
                         framework::ConfigUriObjectKind::Absolute);
        };
    }
    if (!s.object_text)
        s.object_text = profile_boxed_object_text;
    if (!s.exception_message)
        s.exception_message = message;
}
void UserAgentServiceConnector::configure_url(const Text& url, bool configuration) {
    auto& s = services_;
    if (configuration && (!url || std::all_of(url->begin(), url->end(), whitespace))) {
        s.error(u"[USER AGENT CONNECTOR]: No Home URI named in configuration");
        error(u"UserAgent connector init error");
    }
    if (!set_service_url(url) && configuration)
        error(u"UserAgent connector init error");
}
bool UserAgentServiceConnector::set_service_url(const Text& input) {
    if (!input)
        null_reference();
    auto url = services_.lower(*input);
    try {
        services_.validate_uri(url);
    } catch (...) {
        services_.debug(u"[USER AGENT CONNECTOR]: Malformed Uri " + url + u": " +
                        services_.exception_message(std::current_exception()));
        return false;
    }
    server_url_ = url.ends_with(u'/') ? url : url + u"/";
    return true;
}
std::shared_ptr<Value> UserAgentServiceConnector::call_server(std::u16string_view method,
                                                              std::shared_ptr<Value> data) {
    auto args = std::make_shared<network::NwcXmlRpcRequest::Parameters>();
    args->push_back(std::move(data));
    auto request = std::make_shared<network::NwcXmlRpcRequest>(std::u16string(method), args);
    std::shared_ptr<network::NwcXmlRpcResponse> response;
    try {
        response = send_request(request);
    } catch (...) {
        services_.debug(u"[USER AGENT CONNECTOR]: " + std::u16string(method) + u" call to " +
                        server_url_.value_or(u"") + u" failed: " +
                        services_.exception_message(std::current_exception()));
        throw;
    }
    if (!response)
        null_reference();
    if (response->is_fault)
        error(u"[USER AGENT CONNECTOR]: " + std::u16string(method) + u" call to " +
              server_url_.value_or(u"") + u" returned an error: " + response->fault_string().value_or(u""));
    if (!response->value || is_null(*response->value) || response->value->view().kind != Kind::Map)
        error(u"[USER AGENT CONNECTOR]: " + std::u16string(method) + u" call to " +
              server_url_.value_or(u"") + u" returned null");
    return response->value;
}
Value UserAgentServiceConnector::get_user_info(const framework::Uuid& id) {
    auto response = call_server(u"get_user_info", user_request(id));
    auto result = Value::object({});
    for (const auto& key : response->view().ordered_keys()) {
        const auto& v = response->view().map.at(key);
        if (!is_null(v))
            result.set(key, v);
    }
    return result;
}
Value UserAgentServiceConnector::get_server_urls(const framework::Uuid& id) {
    auto response = call_server(u"get_server_urls", user_request(id));
    auto result = Value::object({});
    for (const auto& key : response->view().ordered_keys()) {
        const auto& v = response->view().map.at(key);
        if (framework::invariant_starts_with(key, "SRV_") && !is_null(v)) {
            auto name = framework::utf16_replacing(key).substr(4);
            auto text = services_.object_text(v);
            auto utf8_name = framework::utf8(name);
            if (result.contains(utf8_name))
                throw network::NwcXmlRpcError("System.ArgumentException",
                                              u"An item with the same key has already been added. Key: " +
                                                  name);
            result.set(std::move(utf8_name), boxed(text));
        }
    }
    return result;
}
UserAgentConnectorServices::Text UserAgentServiceConnector::locate_user(const framework::Uuid& id) {
    auto r = call_server(u"locate_user", user_request(id));
    if (!r->view().contains("URL"))
        return u"";
    const auto& v = r->view().get("URL");
    if (is_null(v))
        null_reference();
    return services_.object_text(v);
}
UserAgentConnectorServices::Text UserAgentServiceConnector::get_uui(const framework::Uuid& user,
                                                                    const framework::Uuid& target) {
    auto data = user_request(user);
    data->set("targetUserID", Value(target.str()));
    auto r = call_server(u"get_uui", data);
    if (!r->view().contains("UUI"))
        return u"";
    const auto& v = r->view().get("UUI");
    if (is_null(v))
        null_reference();
    return services_.object_text(v);
}
framework::Uuid UserAgentServiceConnector::get_uuid(const Text& first, const Text& last) {
    auto data = std::make_shared<Value>(Value::object({{"first", boxed(first)}, {"last", boxed(last)}}));
    auto r = call_server(u"get_uuid", data);
    if (!r->view().contains("UUID"))
        error(u"[USER AGENT CONNECTOR]: get_uuid call to " + server_url_.value_or(u"") +
              u" didn't return a UUID");
    auto& v = r->view().get("UUID");
    if (is_null(v))
        null_reference();
    auto text = services_.object_text(v);
    framework::Uuid uuid;
    if (!text || !framework::try_parse_sdk_uuid(*text, uuid))
        error(u"[USER AGENT CONNECTOR]: get_uuid call to " + server_url_.value_or(u"") +
              u" returned an invalid UUID: " + services_.object_text(v).value_or(u""));
    return uuid;
}
std::shared_ptr<network::NwcXmlRpcResponse>
UserAgentServiceConnector::send_request(const std::shared_ptr<network::NwcXmlRpcRequest>& request) {
    auto client = services_.client(10000);
    std::exception_ptr pending;
    std::shared_ptr<network::NwcXmlRpcResponse> response;
    try {
        if (!client)
            null_reference();
        response = client->send(request, server_url_);
    } catch (...) {
        pending = std::current_exception();
    }
    if (client)
        client->close();
    if (pending)
        std::rethrow_exception(pending);
    return response;
}
bool UserAgentServiceConnector::get_bool_response(const std::shared_ptr<network::NwcXmlRpcRequest>& request,
                                                  Text& reason) {
    std::shared_ptr<network::NwcXmlRpcResponse> response;
    try {
        response = send_request(request);
    } catch (...) {
        auto ex = std::current_exception();
        services_.debug(u"[USER AGENT CONNECTOR]: Unable to contact remote server " +
                        server_url_.value_or(u"") + u" for GetBoolResponse");
        reason = u"Exception: " + services_.exception_message(ex);
        return false;
    }
    if (!response)
        null_reference();
    if (response->is_fault) {
        auto text = response->fault_string();
        services_.error(u"[USER AGENT CONNECTOR]: remote call to " + server_url_.value_or(u"") +
                        u" for GetBoolResponse returned an error: " + text.value_or(u""));
        reason = u"XMLRPC Fault";
        return false;
    }
    auto hash = response->value;
    if (hash && is_null(*hash))
        hash.reset();
    if (hash && hash->view().kind != Kind::Map)
        cast_error(*hash, u"System.Collections.Hashtable");
    try {
        if (!hash) {
            services_.error(u"[USER AGENT CONNECTOR]: Got null response from " + server_url_.value_or(u"") +
                            u"! THIS IS BAAAAD");
            reason = u"Internal error 1";
            return false;
        }
        reason = u"";
        if (hash->view().contains("result"))
            return bool_parse(string_cast(hash->view().get("result")));
        reason = u"Internal error 2";
        services_.warn(u"[USER AGENT CONNECTOR]: response from " + server_url_.value_or(u"") +
                       u" does not have expected key 'result'");
        return false;
    } catch (...) {
        auto ex = std::current_exception();
        services_.error(u"[USER AGENT CONNECTOR]: Got exception on GetBoolResponse response.");
        if (!hash)
            null_reference();
        if (hash->view().contains("result") && !is_null(hash->view().get("result"))) {
            string_cast(hash->view().get("result"));
            services_.error(u"Reply was ");
        }
        reason = u"Exception: " + services_.exception_message(ex);
        return false;
    }
}
bool UserAgentServiceConnector::verify_agent(const framework::Uuid& session, const Text& token) {
    Text reason = u"";
    return get_bool_response(request(u"verify_agent", Value::object({{"sessionID", Value(session.str())},
                                                                     {"token", boxed(token)}})),
                             reason);
}
bool UserAgentServiceConnector::verify_client(const framework::Uuid& session, const Text& token) {
    Text reason = u"";
    return get_bool_response(request(u"verify_client", Value::object({{"sessionID", Value(session.str())},
                                                                      {"token", boxed(token)}})),
                             reason);
}
bool UserAgentServiceConnector::is_agent_coming_home(const framework::Uuid& session, const Text& name) {
    Text reason = u"";
    return get_bool_response(
        request(u"agent_is_coming_home",
                Value::object({{"sessionID", Value(session.str())}, {"externalName", boxed(name)}})),
        reason);
}
void UserAgentServiceConnector::logout_agent(const framework::Uuid& user, const framework::Uuid& session) {
    Text reason = u"";
    get_bool_response(request(u"logout_agent", Value::object({{"userID", Value(user.str())},
                                                              {"sessionID", Value(session.str())}})),
                      reason);
}
std::shared_ptr<GridRegion> UserAgentServiceConnector::get_home_region(const framework::Uuid& user,
                                                                       framework::Vector3& position,
                                                                       framework::Vector3& look_at) {
    position = {0, 1, 0};
    look_at = {0, 1, 0};
    auto hash = call_server(u"get_home_region", user_request(user));
    auto field = [&](const char* key) -> const Value* {
        auto& map = hash->view().map;
        auto i = map.find(key);
        return i == map.end() ? nullptr : &i->second;
    };
    auto text = [&](const char* key) -> Text {
        auto v = field(key);
        return v ? string_cast(*v) : Text{};
    };
    auto present = [&](const char* key) {
        auto v = field(key);
        return v && !is_null(*v);
    };
    if (!bool_parse(text("result")))
        return {};
    auto region = std::make_shared<GridRegion>();
    if (auto uuid = text("uuid"))
        framework::try_parse_sdk_uuid(*uuid, region->id);
    auto number = [&](const char* key) {
        try {
            return framework::invariant_convert_to_int32(text(key));
        } catch (const framework::InvariantNumberError&) {
            return std::int32_t{0};
        }
    };
    if (present("x"))
        region->x = number("x");
    if (present("y"))
        region->y = number("y");
    if (present("size_x"))
        region->size_x = number("size_x");
    if (present("size_y"))
        region->size_y = number("size_y");
    if (present("region_name"))
        region->name = framework::utf8(*text("region_name"));
    if (present("hostname"))
        region->external_host = framework::utf8(*text("hostname"));
    if (present("http_port")) {
        std::uint32_t port = 0;
        framework::try_parse_unsigned_integer(framework::utf8(*text("http_port")), port);
        region->http_port = port;
    }
    if (present("server_uri"))
        region->set_server_uri(framework::utf8(*text("server_uri")));
    if (present("internal_port")) {
        auto port = number("internal_port");
        if (port < 0 || port > 65535)
            throw network::NwcXmlRpcError(
                "System.ArgumentOutOfRangeException",
                u"Specified argument was out of the range of valid values. (Parameter 'port')");
        region->internal_endpoint = RegionEndpoint{"0.0.0.0", static_cast<std::uint16_t>(port)};
    }
    if (present("position"))
        framework::try_parse_sdk_vector3(*text("position"), position);
    if (present("lookAt"))
        framework::try_parse_sdk_vector3(*text("lookAt"), look_at);
    return region;
}
std::vector<framework::Uuid>
UserAgentServiceConnector::online_friends_response(const std::shared_ptr<network::NwcXmlRpcRequest>& request,
                                                   std::u16string_view operation) {
    std::vector<framework::Uuid> online;
    std::shared_ptr<network::NwcXmlRpcResponse> response;
    try {
        response = send_request(request);
    } catch (...) {
        services_.debug(u"[USER AGENT CONNECTOR]: Unable to contact remote server " +
                        server_url_.value_or(u"") + u" for " + std::u16string(operation));
        return online;
    }
    if (!response)
        null_reference();
    if (response->is_fault) {
        auto text = response->fault_string();
        services_.error(u"[USER AGENT CONNECTOR]: remote call to " + server_url_.value_or(u"") + u" for " +
                        std::u16string(operation) + u" returned an error: " + text.value_or(u""));
        return online;
    }
    auto hash = response->value;
    if (hash && is_null(*hash))
        hash.reset();
    if (hash && hash->view().kind != Kind::Map)
        cast_error(*hash, u"System.Collections.Hashtable");
    try {
        if (!hash) {
            services_.error(u"[USER AGENT CONNECTOR]: GetOnlineFriends Got null response from " +
                            server_url_.value_or(u"") + u"! THIS IS BAAAAD");
            return online;
        }
        for (const auto& key : hash->view().ordered_keys()) {
            const auto& value = hash->view().map.at(key);
            if (framework::invariant_starts_with(key, "friend_") && !is_null(value)) {
                auto text = services_.object_text(value);
                framework::Uuid id;
                if (text && framework::try_parse_sdk_uuid(*text, id))
                    online.push_back(id);
            }
        }
    } catch (...) {
        services_.error(u"[USER AGENT CONNECTOR]: Got exception on GetOnlineFriends response.");
    }
    return online;
}
std::vector<framework::Uuid> UserAgentServiceConnector::status_notification(const Friends& friends,
                                                                            const framework::Uuid& user,
                                                                            bool online) {
    auto hash = Value::object({{"userID", Value(user.str())}, {"online", Value(online ? "True" : "False")}});
    if (!friends)
        null_reference();
    std::int32_t index = 0;
    for (const auto& item : *friends) {
        hash.set("friend_" + std::to_string(index), boxed(item));
        index = framework::wrapping_add(index, 1);
    }
    return online_friends_response(request(u"status_notification", std::move(hash)), u"StatusNotification");
}
std::vector<framework::Uuid> UserAgentServiceConnector::get_online_friends(const framework::Uuid& user,
                                                                           const Friends& friends) {
    auto hash = Value::object({{"userID", Value(user.str())}});
    if (!friends)
        null_reference();
    std::int32_t index = 0;
    for (const auto& item : *friends) {
        hash.set("friend_" + std::to_string(index), boxed(item));
        index = framework::wrapping_add(index, 1);
    }
    return online_friends_response(request(u"get_online_friends", std::move(hash)), u"GetOnlineFriends");
}
bool UserAgentServiceConnector::login_grid(const GridRegion* source,
                                           const std::shared_ptr<framework::AgentCircuitData>& circuit,
                                           GridRegion* gatekeeper, std::shared_ptr<GridRegion> owner,
                                           GridRegion* destination, bool from_login, std::string& reason) {
    std::lock_guard lock(gatekeeper_mutex_);
    reason.clear();
    if (!destination) {
        reason = "Destination is null";
        services_.debug(u"[USER AGENT CONNECTOR]: Given destination is null");
        return false;
    }
    GridRegion home;
    home.set_server_uri(server_url_ ? std::optional<std::string>(framework::utf8(*server_url_))
                                    : std::nullopt);
    home.id = destination->id;
    home.x = destination->x;
    home.y = destination->y;
    if (!owner && gatekeeper)
        owner = std::make_shared<GridRegion>(*gatekeeper);
    gatekeeper_ = std::move(owner);
    LoginTransferContext context;
    return create_agent(source, home, circuit, from_login ? 0x80u : 0x20u, context, reason);
}
bool UserAgentServiceConnector::login_agent_to_grid(
    const GridRegion* source, const std::shared_ptr<framework::AgentCircuitData>& circuit,
    GridRegion& gatekeeper, GridRegion& destination, bool login, std::string& reason) {
    return login_grid(source, circuit, &gatekeeper, {}, &destination, login, reason);
}
bool UserAgentServiceConnector::login_agent_to_grid(
    const GridRegion* source, const std::shared_ptr<framework::AgentCircuitData>& circuit,
    GridRegion* gatekeeper, GridRegion* destination, bool login, std::string& reason) {
    return login_grid(source, circuit, gatekeeper, {}, destination, login, reason);
}
bool UserAgentServiceConnector::login_agent_to_grid_owned(
    const GridRegion* source, const std::shared_ptr<framework::AgentCircuitData>& circuit,
    const std::shared_ptr<GridRegion>& gatekeeper, const std::shared_ptr<GridRegion>& destination, bool login,
    std::string& reason) {
    return login_grid(source, circuit, gatekeeper.get(), gatekeeper, destination.get(), login, reason);
}
void UserAgentServiceConnector::pack_data(Value& args, const GridRegion* source,
                                          framework::AgentCircuitData& circuit, const GridRegion& destination,
                                          std::uint32_t flags) {
    std::lock_guard lock(gatekeeper_mutex_);
    SimulationConnector::pack_data(args, source, circuit, destination, flags);
    if (!gatekeeper_)
        null_reference();
    args.set("gatekeeper_serveruri", gatekeeper_->server_uri());
    args.set("gatekeeper_host", gatekeeper_->external_host.value_or(""));
    args.set("gatekeeper_port", std::to_string(gatekeeper_->http_port));
    args.set("destination_serveruri", destination.server_uri());
}
}
