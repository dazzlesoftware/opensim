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
#include "opensim/services/login_service.hpp"
#include "opensim/services/login_orchestration.hpp"
#include "opensim/services/login_completion.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/dotnet_regex_interpreter.hpp"
#include "opensim/framework/execution_stack.hpp"
#include "opensim/network/osd_value.hpp"
#include <mutex>
#include <algorithm>
#include <stdexcept>
namespace opensim::services {
using network::Value;
using namespace opensim::framework;
namespace {
Value failure(const std::string& reason, const std::string& message) {
    return Value::object({{"reason", Value(reason)}, {"message", Value(message)}, {"login", Value("false")}});
}
std::string optional(const Value& data, std::string_view name, const std::string& fallback = "") {
    return data.contains(name) && data.get(name).kind != Value::Kind::Undefined ? data.get(name).as_string()
                                                                                : fallback;
}
bool nonnull(const Value& data, std::string_view key) {
    return data.contains(key) && data.get(key).kind != Value::Kind::Undefined;
}
bool identifier_denied(const std::string& denied, const std::string& identifier) {
    const auto text = utf16(denied);
    // Source string.IsNullOrWhiteSpace, followed by ordinal string.Contains.
    return !std::all_of(text.begin(), text.end(), unicode_whitespace) &&
           text.find(utf16(identifier)) != std::u16string::npos;
}
}
struct LoginService::ClientPattern {
    std::mutex mutex;
    DotnetRegexInterpreter interpreter;
    explicit ClientPattern(DotnetRegexNodeText expression)
        : interpreter(dotnet_regex_parse(
                          expression, 9, [] { return std::u16string{}; }, sufficient_execution_stack),
                      expression, sufficient_execution_stack) {}
    bool matches(const std::string& value) {
        std::lock_guard lock(mutex);
        auto input = std::make_shared<const std::u16string>(utf16(value));
        return interpreter.scan(input, 0, static_cast<std::int32_t>(input->size()), 0).success;
    }
    static std::shared_ptr<ClientPattern> create(const std::string& expression) {
        if (expression.empty())
            return {};
        try {
            return std::make_shared<ClientPattern>(std::make_shared<const std::u16string>(utf16(expression)));
        } catch (...) {
            return {};
        }
    }
};

std::function<bool(const std::string&)> LoginService::compile_client_pattern(const std::string& expression) {
    auto pattern = ClientPattern::create(expression);
    if (!pattern)
        return {};
    return [pattern = std::move(pattern)](const std::string& value) { return pattern->matches(value); };
}

Value login_failure(std::string_view name) {
    if (name == "UserProblem")
        return failure(
            "key",
            "Could not authenticate your avatar. Please check your username and password, "
            "and check the grid if problems persist.");
    if (name == "GridProblem")
        return failure("key", "Error connecting to the desired location. Try connecting to another region.");
    if (name == "InventoryProblem")
        return failure("key",
                       "The inventory service is not responding.  Please notify your login region operator.");
    if (name == "DeadRegionProblem")
        return failure(
            "key",
            "The region you are attempting to log into is not responding. Please select another region and try again.");
    if (name == "LoginBlockedProblem")
        return failure("presence", "Logins are currently restricted. Please try again later.");
    if (name == "UnverifiedAccountProblem")
        return failure(
            "presence",
            "Your account has not yet been verified. Please check your email and click the provided link.");
    if (name == "AlreadyLoggedInProblem")
        return failure(
            "presence",
            "You appear to be already logged in. Please wait a a minute or two and retry. "
            "If this takes longer than a few minutes please contact the grid owner. ");
    if (name == "InternalError")
        return failure("Internal Error", "Error generating Login Response");
    if (name == "IncompleteXMLRPC")
        return failure("key", "Incomplete login credentials. Check your username and password.");
    if (name == "InvalidLLSD")
        return failure("key", "Invalid login credentials. Check your username and passwd.");
    throw std::invalid_argument("Unknown login failure");
}
LoginService::LoginService(AccountStore& store, const server::IniConfig& config)
    : store_(store), login_config_(config),
      minimum_level_(config.integer("LoginService", "MinLoginLevel", 0)),
      allow_remote_set_level_(config.boolean("LoginService", "AllowRemoteSetLoginLevel", false)),
      allow_web_keys_(config.boolean("NativeServer", "AllowWebLoginKeys", true)),
      require_inventory_(config.boolean("LoginService", "RequireInventory", true)) {
    // Util.GetConfigVarFromSections visits both sections; the later value wins.
    auto access = [&config](const std::string& key) {
        return config.get("LoginService", key, config.get("AccessControl", key));
    };
    denied_macs_ = access("DeniedMacs");
    denied_id0s_ = access("DeniedID0s");
    allowed_clients_ = ClientPattern::create(access("AllowedClients"));
    denied_clients_ = ClientPattern::create(access("DeniedClients"));
}
void LoginService::set_completion(LoginCompletion* value) {
    completion_ = value;
    if (completion_ && welcome_override_)
        completion_->welcome_message(*welcome_override_);
}
std::optional<std::string> LoginService::console_command(const std::vector<std::string>& command) {
    if (command.size() < 2)
        return {};
    const auto& subcommand = command[1];
    if (subcommand == "level" && command.size() > 2) {
        std::int32_t value = 0;
        bool valid = true;
        try {
            value = invariant_convert_to_int32(utf16(command[2]));
        } catch (const InvariantNumberError&) {
            valid = false;
        }
        // Int32.TryParse writes zero to its out argument on failure.
        minimum_level_.store(value);
        return valid ? "Set minimum login level to " + std::to_string(value)
                     : "ERROR: " + command[2] + " is not a valid login level";
    }
    if (subcommand == "reset") {
        const auto value = login_config_.integer("LoginService", "MinLoginLevel", 0);
        minimum_level_.store(value);
        return "Reset min login level to " + std::to_string(value);
    }
    if (subcommand == "text" && command.size() > 2) {
        const auto output = "Login welcome message set to '" + command[2] + "'";
        auto message = command[2];
        for (std::size_t at = 0; (at = message.find("\\n", at)) != std::string::npos; ++at)
            message.replace(at, 2, "\n");
        welcome_override_ = std::move(message);
        if (completion_)
            completion_->welcome_message(*welcome_override_);
        return output;
    }
    return {};
}
Value LoginService::login(const LoginRequest& request) {
    const auto session = orchestrator_ ? Uuid::random() : Uuid{};
    try {
        const std::string client = request.version.find(' ') != std::string::npos
                                       ? request.version
                                       : request.channel + " " + request.version;
        if ((allowed_clients_ && !allowed_clients_->matches(client)) ||
            (denied_clients_ && denied_clients_->matches(client)) || identifier_denied(denied_macs_, request.mac) ||
            identifier_denied(denied_id0s_, request.id0))
            return login_failure("LoginBlockedProblem");
        const auto account = store_.find(request.scope, request.first, request.last);
        if (!account)
            return login_failure("UserProblem");
        if (account->user_level < minimum_level_.load())
            return login_failure("LoginBlockedProblem");
        if (account->principal_id == "6571e388-6218-4574-87db-f9379718315e")
            return login_failure("UserProblem");
        if (request.scope != ZeroUuid && account->scope_id != request.scope && account->scope_id != ZeroUuid)
            return login_failure("UserProblem");
        const std::string password = request.password.starts_with("$1$")
                                         ? request.password.substr(3)
                                         : md5(request.password);
        const auto authentication =
            authenticate_ ? authenticate_(account->principal_id, password, 30, allow_web_keys_)
                          : store_.authenticate(account->principal_id, password, 30, allow_web_keys_);
        if (!authentication)
            return login_failure("UserProblem");
        Uuid secure_session;
        if (!try_parse_sdk_uuid(utf16_replacing(authentication.token), secure_session))
            return login_failure("UserProblem");
        if (!orchestrator_)
            return login_failure(require_inventory_ ? "InventoryProblem" : "GridProblem");

        const auto scope = Uuid(request.scope == ZeroUuid ? account->scope_id : request.scope);
        auto prepared = orchestrator_->prepare(*account, scope, session, secure_session, request.start);
        if (auto* failure = std::get_if<Value>(&prepared))
            return *failure;

        if (!completion_) {
            // The managed login service returns an empty reason when the simulation connector is absent.
            orchestrator_->abort(session);
            return failure("key", "");
        }

        LoginCompletionRequest completion_request;
        completion_request.start = request.start;
        completion_request.viewer = request.version;
        completion_request.channel = request.channel;
        completion_request.mac = request.mac;
        completion_request.id0 = request.id0;
        completion_request.remote_ip = request.remote_ip;
        completion_request.first = request.first;
        completion_request.last = request.last;
        if (!authentication.real_id.empty())
            completion_request.real_id = Uuid(authentication.real_id);
        auto result = completion_->finish(std::get<PreparedLogin>(prepared), completion_request);
        if (auto* rejected = std::get_if<Value>(&result)) {
            orchestrator_->abort(session);
            return *rejected;
        }
        auto response = std::get<std::shared_ptr<LoginResponse>>(result);
        return request.llsd ? response->to_llsd() : response->to_xml_rpc();
    } catch (const std::exception&) {
        if (orchestrator_)
            orchestrator_->cleanup(session);
        return login_failure("InternalError");
    }
}
Value LoginService::set_level(std::string_view first, std::string_view last, std::string_view password,
                              int level) {
    auto result = Value::object({{"success", Value("false")}});
    if (!allow_remote_set_level_)
        return result;
    try {
        const auto account = store_.find(ZeroUuid, first, last);
        if (!account || account->user_level < 200)
            return result;
        const auto authentication =
            authenticate_ ? authenticate_(account->principal_id, password, 30, allow_web_keys_)
                          : store_.authenticate(account->principal_id, password, 30, allow_web_keys_);
        Uuid token;
        if (!authentication || !try_parse_sdk_uuid(utf16_replacing(authentication.token), token))
            return result;
        minimum_level_.store(level);
        return Value::object({{"success", Value(true)}});
    } catch (const std::exception&) {
        return result;
    }
}
Value LoginHandlers::xml_rpc_login(const Value& data, const std::string& ip) {
    if (data.kind != Value::Kind::Map || !nonnull(data, "first") || !nonnull(data, "last") ||
        !(nonnull(data, "passwd") || (!data.contains("passwd") && nonnull(data, "web_login_key") &&
                                      data.get("web_login_key").as_string() != ZeroUuid)))
        return login_failure("IncompleteXMLRPC");
    LoginRequest r;
    r.first = data.get("first").as_string();
    r.last = data.get("last").as_string();
    r.password = data.contains("passwd") ? data.get("passwd").as_string()
                                         : "$1$" + data.get("web_login_key").as_string();
    r.scope = construct_sdk_uuid(utf16(optional(data, "scope_id", std::string(ZeroUuid)))).str();
    if (data.contains("start") && data.get("start").kind == Value::Kind::Undefined)
        throw std::invalid_argument("Null XML-RPC start location");
    r.start = optional(data, "start");
    r.version = optional(data, "version", "Unknown");
    r.channel = optional(data, "channel", "Unknown");
    r.mac = optional(data, "mac", "Unknown");
    r.id0 = optional(data, "id0", "Unknown");
    r.remote_ip = ip;
    return service_.login(r);
}
Value LoginHandlers::llsd_login(const Value& data, const std::string& ip) {
    if (data.kind != Value::Kind::Map || !data.contains("first") || !data.contains("last") ||
        !data.contains("passwd"))
        return login_failure("InvalidLLSD");
    LoginRequest r;
    r.llsd = true;
    r.first = network::osd_string(data.get("first"));
    r.last = network::osd_string(data.get("last"));
    r.password = network::osd_string(data.get("passwd"));
    r.scope = data.contains("scope_id")
        ? construct_sdk_uuid(utf16(network::osd_string(data.get("scope_id")))).str()
        : std::string(ZeroUuid);
    r.start = network::osd_string(data.get("start"));
    r.version = network::osd_string(data.get("version"));
    r.channel = network::osd_string(data.get("channel"));
    r.mac = network::osd_string(data.get("mac"));
    r.id0 = network::osd_string(data.get("id0"));
    r.remote_ip = ip;
    return service_.login(r);
}
Value LoginHandlers::set_level(const Value& data) {
    if (data.kind != Value::Kind::Map || !nonnull(data, "first") || !nonnull(data, "last") ||
        !nonnull(data, "passwd") || !nonnull(data, "level"))
        return Value::object({{"success", Value("false")}});
    // Reuse the source NumberStyles.Integer grammar: six ASCII whitespace
    // characters, one sign, checked magnitude and trailing NUL handling.
    // Application/ServerMain startup and BaseHttpServer request dispatch call
    // Culture.SetCurrentCulture(), which selects the invariant NumberFormatInfo.
    const int level = invariant_convert_to_int32(utf16(data.get("level").as_string()));
    return service_.set_level(data.get("first").as_string(), data.get("last").as_string(),
                              data.get("passwd").as_string(), level);
}
}
