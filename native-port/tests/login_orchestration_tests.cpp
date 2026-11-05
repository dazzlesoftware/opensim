// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_orchestration.hpp"
#include "opensim/services/login_service.hpp"
#include "opensim/services/login_completion.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/services/login_recovery.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;
using namespace services;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct CompletionAdmission : ISimulationLogin {
    std::vector<std::string>& events;
    bool allow = true;
    std::shared_ptr<framework::AgentCircuitData> admitted;
    explicit CompletionAdmission(std::vector<std::string>& e) : events(e) {
    }
    bool query_access(GridRegion&, const framework::Uuid&, const std::optional<std::string>&, bool,
                      const framework::Vector3&, std::vector<framework::Uuid>&, LoginTransferContext&,
                      std::string& reason) override {
        events.push_back("query");
        reason = "denied";
        return allow;
    }
    bool create_agent(const GridRegion*, GridRegion&,
                      const std::shared_ptr<framework::AgentCircuitData>& circuit, std::uint32_t,
                      LoginTransferContext&, std::string& reason) override {
        events.push_back("create");
        admitted = circuit;
        reason = "";
        return true;
    }
};
int main() {
    try {
        server::IniConfig config;
        const auto message = duplicate_login_message({}, framework::Uuid::random(), 0xffffffffu, "key<&>");
        const auto call =
            network::parse_xml_rpc(network::xml_rpc_request({"grid_instant_message", {message}}));
        check(call.method == "grid_instant_message" && call.parameters.size() == 1 &&
                  call.parameters[0].get("timestamp").as_string() == "4294967295" &&
                  call.parameters[0].get("message_key").as_string() == "key<&>",
              "kick request serialization retains uint timestamp and escaped key");
        check(!duplicate_login_message({}, {}, 0, "").contains("message_key"),
              "empty IM message key omitted");
        const auto rpc_result = network::parse_xml_rpc_response(
            network::xml_rpc_response(network::Value::object({{"success", "TRUE"}})));
        check(rpc_result.get("success").as_string() == "TRUE", "XML-RPC delivery response decoding");
        bool fault = false;
        try {
            network::parse_xml_rpc_response(network::xml_rpc_fault(42, "failure"));
        } catch (const std::invalid_argument&) {
            fault = true;
        }
        check(fault, "XML-RPC fault is not a delivery success");
        AccountStore accounts(":memory:", ":memory:", true);
        auto id = accounts.create_user("Native", "Resident", "secret", framework::ZeroUuid, 200);
        auto account = *accounts.find(framework::ZeroUuid, "Native", "Resident");
        const framework::Uuid user(id), session = framework::Uuid::random(),
                                        secure = framework::Uuid::random();
        InventoryFolderStore inventory_store(":memory:", true);
        InventoryService inventory(inventory_store);
        SqliteGridUserStore user_store(":memory:", true);
        GridUserService users(user_store);
        SqliteRegionStore regions(":memory:", true);
        GridService grid(regions, config);
        GridRegion region;
        region.id = framework::Uuid::random();
        region.name = "Region";
        region.x = 25600;
        region.y = 25600;
        region.size_x = region.size_y = 256;
        region.internal_endpoint = RegionEndpoint{"0.0.0.0", 9000};
        region.external_host = "127.0.0.1";
        check(grid.register_region({}, region).empty(), "register destination");
        regions.set(region.id, "flags", "5");
        NullPresenceStore presence_store;
        PresenceService presence(presence_store);
        LoginDependencies dependencies;
        dependencies.grid = &grid;
        dependencies.presence = &presence;
        dependencies.grid_user = [&](const std::string& value) { return users.info(value); };
        dependencies.inventory = [&](const framework::Uuid& value) { return inventory.skeleton(value); };
        dependencies.logged_out = [&](const std::string& value, const framework::Uuid& s,
                                      const framework::Uuid& r, const framework::Vector3& p,
                                      const framework::Vector3& l) {
            return users.logged_out(value, s, r, p, l);
        };
        auto appearance = std::make_shared<framework::AvatarAppearance>();
        int appearance_calls = 0;
        dependencies.appearance = [&](const framework::Uuid& id) {
            check(id == user && !presence.agents({id.str()}).empty(),
                  "appearance lookup follows inventory and presence");
            ++appearance_calls;
            return appearance;
        };
        LoginOrchestrator orchestrator(dependencies, config);
        auto prepared = orchestrator.prepare(account, {}, session, secure, "home");
        check(std::holds_alternative<network::Value>(prepared), "missing inventory fails");
        check(std::get<network::Value>(prepared).get("message").as_string() ==
                  login_failure("InventoryProblem").get("message").as_string(),
              "inventory failure response");
        check(appearance_calls == 0, "inventory failure skips avatar retrieval");
        check(!presence.agent(session), "inventory failure precedes presence creation");
        check(inventory.create_user_inventory(user), "create inventory");
        users.set_home(id, region.id, {128, 128, 30}, {0, 1, 0});
        prepared = orchestrator.prepare(account, {}, session, secure, "home");
        check(std::holds_alternative<PreparedLogin>(prepared), "prepare connected login");
        const auto& ready = std::get<PreparedLogin>(prepared);
        check(ready.appearance == appearance && appearance_calls == 1,
              "prepared login retains service appearance reference");
        check(ready.destination.region->id == region.id && !ready.skeleton.empty() &&
                  (ready.destination.flags & 512),
              "home inventory and godlike flag");
        check(presence.agent(session).has_value() && !users.info(id)->online,
              "presence exists before admission without marking grid user online");
        orchestrator.abort(session);
        check(!presence.agent(session), "failed admission removes presence");
        bool threw = false;
        try {
            orchestrator.prepare(account, {}, session, secure, "");
        } catch (const std::exception&) {
            threw = true;
            orchestrator.cleanup(session);
        }
        check(threw && !presence.agent(session), "malformed start rollback");
        users.set_last_position(id, {}, framework::Uuid::random(), {1, 2, 3}, {0, 1, 0});
        users.logged_in(id);
        prepared = orchestrator.prepare(account, {}, session, secure, "home");
        check(std::get<network::Value>(prepared).get("reason").as_string() == "presence" &&
                  !users.info(id)->online,
              "duplicate login with vanished region logs out grid user");
        LoginService login(accounts, config);
        login.set_orchestrator(&orchestrator);
        LoginRequest request;
        request.first = "Native";
        request.last = "Resident";
        request.password = "secret";
        request.scope = framework::ZeroUuid;
        request.start = "home";
        const auto response = login.login(request);
        check(response.get("login").as_string() == "false" && response.get("reason").as_string() == "key" &&
                  response.get("message").as_string().empty(),
              "missing simulation connector preserves source launch failure");
        check(presence.agents({id}).empty() && !users.info(id)->online,
              "launch failure leaves no presence or online grid user");
        request.password = "wrong";
        check(login.login(request).get("message").as_string() ==
                  login_failure("UserProblem").get("message").as_string(),
              "authentication still precedes orchestration");
        request.password = "secret";
        request.remote_ip = "192.0.2.10";
        request.version = "Viewer 1";
        request.channel = "Channel";
        std::vector<std::string> events;
        CompletionAdmission admission(events);
        AgentLauncher launcher(&grid, &admission, nullptr, nullptr, {}, [] { return 123456u; });
        LoginAgentFactory factory(
            LoginAgentFactory::Config{}, std::nullopt, [&](auto& a) { return accounts.store_account(a); },
            [] { return "native-seed"; });
        LoginCompletionDependencies completion_dependencies;
        completion_dependencies.launcher = &launcher;
        completion_dependencies.factory = &factory;
        int fail = 0;
        completion_dependencies.logged_in = [&](const std::string& id) -> std::optional<GridUserInfo> {
            events.push_back("online");
            if (fail == 1)
                throw std::runtime_error("grid-user failure");
            return users.logged_in(id);
        };
        completion_dependencies.friends =
            [&](const framework::Uuid&) -> std::optional<std::vector<std::shared_ptr<LoginFriendInfo>>> {
            events.push_back("friends");
            if (fail == 2)
                throw std::runtime_error("friends failure");
            return std::vector<std::shared_ptr<LoginFriendInfo>>{};
        };
        completion_dependencies.gestures =
            [&](const framework::Uuid& id) -> std::optional<std::vector<std::shared_ptr<InventoryItem>>> {
            events.push_back("gestures");
            if (fail == 3)
                throw std::runtime_error("gesture failure");
            std::vector<std::shared_ptr<InventoryItem>> result;
            for (auto& item : inventory.active_gestures(id))
                result.push_back(std::make_shared<InventoryItem>(item));
            return result;
        };
        auto library = std::make_shared<LoginResponseLibrary>();
        library->root = []() { return std::make_shared<LoginInventoryFolder>(); };
        library->folders = []() -> std::optional<LoginResponseLibrary::Folders> {
            return LoginResponseLibrary::Folders{};
        };
        completion_dependencies.library = library;
        bind_login_response_network(completion_dependencies.environment);
        bind_login_response_timezones(completion_dependencies.environment);
        LoginCompletionOptions completion_options;
        completion_options.dst_zone = "none";
        completion_options.welcome = "Hello <USERNAME> / <USERNAME>";
        completion_options.max_agent_groups = 73;
        LoginCompletion completion(completion_dependencies, completion_options);
        login.set_completion(&completion);
        auto clear_login = [&]() {
            if (admission.admitted)
                presence.logout(admission.admitted->session_id);
            users.logged_out(id, {}, region.id, {128, 128, 30}, {0, 1, 0});
            events.clear();
        };
        clear_login();
        admission.allow = false;
        auto denied = login.login(request);
        check(denied.get("login").as_string() == "false" && denied.get("reason").as_string() == "key" &&
                  denied.get("message").as_string() == "denied",
              "rejected admission preserves launch reason");
        check(!users.info(id)->online && presence.agents({id}).empty() &&
                  std::find(events.begin(), events.end(), "online") == events.end(),
              "rejected admission rolls back presence before any grid-user login");
        clear_login();
        admission.allow = true;
        auto success = login.login(request);
        check(success.get("login").as_string() == "true" && success.get("agent_id").as_string() == id &&
                  success.get("circuit_code").integer == 123456,
              "authenticated prepared login reaches native successful XML-RPC response");
        check(success.get("message").as_string() == "Hello Native Resident / Native Resident" &&
                  success.get("max-agent-groups").integer == 73,
              "completion preserves welcome replacements and group configuration");
        check(events == std::vector<std::string>({"query", "create", "online", "friends", "gestures"}),
              "completion preserves source admission and service call ordering");
        check(users.info(id)->online && presence.agent(admission.admitted->session_id).has_value(),
              "successful admission retains presence and marks grid user online");
        check(admission.admitted->ip_address == std::optional<std::string>(request.remote_ip) &&
                  admission.admitted->viewer() == std::optional<std::string>(request.version),
              "completion forwards client identity to MakeAgent");
        clear_login();
        request.llsd = true;
        auto llsd = login.login(request);
        check(llsd.get("login").as_string() == "true" &&
                  llsd.get("agent_id").kind == network::Value::Kind::Uuid && !llsd.contains("http_port"),
              "LLSD completion uses source protocol-specific response");
        request.llsd = false;
        clear_login();
        check(login.console_command({"login", "level", "201"}) ==
                  std::optional<std::string>("Set minimum login level to 201"),
              "console level exact output");
        check(login.login(request).get("reason").as_string() == "presence" && presence.agents({id}).empty(),
              "console login level blocks before presence admission");
        check(login.console_command({"login", "level", "bad"}) ==
                      std::optional<std::string>("ERROR: bad is not a valid login level") &&
                  login.minimum_login_level() == 0,
              "invalid level follows TryParse out-zero behavior");
        login.console_command({"login", "text", "Hi <USERNAME>\\nNext", "ignored"});
        success = login.login(request);
        check(success.get("message").as_string() == "Hi Native Resident\nNext",
              "console text updates live welcome with source first-token and newline behavior");
        clear_login();
        config.set("LoginService", "MinLoginLevel", "202");
        login.console_command({"login", "reset"});
        check(login.minimum_login_level() == 202, "console reset reads shared current configuration");
        config.set("LoginService", "MinLoginLevel", "0");
        login.console_command({"login", "reset"});
        for (fail = 1; fail <= 3; ++fail) {
            clear_login();
            auto broken = login.login(request);
            check(broken.get("reason").as_string() == "Internal Error" && presence.agents({id}).empty(),
                  "post-admission dependency exceptions clean up presence");
            check(users.info(id)->online == (fail > 1),
                  "cleanup preserves source grid-user online state after post-login failure");
            check(events.size() == static_cast<std::size_t>(fail + 2),
                  "post-admission failures stop later service calls");
        }
        fail = 0;
        clear_login();
        std::cout << "PASS: connected login preparation, completion and failure rollback\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
