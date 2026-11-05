// Native scene capability composition; world actors and HG service are controlled.
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/services/scene_client_verification.hpp"
#include "opensim/server/poll_http_service.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
using namespace opensim;
using namespace services;
using namespace server;
using namespace network;
framework::Uuid identity(int n) {
    return framework::Uuid("00000000-0000-0000-0000-00000000000" + std::to_string(n));
}
struct Client : VerifiedSceneClient {
    framework::Uuid session_id() const override {
        return identity(3);
    }
};
struct Actor : VerifiedScenePresence {
    framework::Uuid agent_id() const override {
        return identity(2);
    }
    std::shared_ptr<VerifiedSceneClient> controlling_client() const override {
        return std::make_shared<Client>();
    }
};
struct Backend;
struct Verifier : UserAgentVerificationModule {
    Backend& backend;
    explicit Verifier(Backend& b) : backend(b) {
    }
    bool verify_client(const std::shared_ptr<framework::AgentCircuitData>& circuit,
                       const std::string& address) override;
};
struct Backend : SceneCapabilityBackend, SceneClientVerificationBackend {
    std::shared_ptr<RegistryCapabilityListener> registry;
    mutable framework::AgentCircuitManager circuits;
    NullPresenceStore store;
    PresenceService sessions;
    SceneClientVerification verification;
    std::shared_ptr<framework::AgentCircuitData> incoming;
    std::shared_ptr<Verifier> verifier;
    SceneCapabilities* runtime = nullptr;
    std::atomic<int> seed_requests = 0, checks = 0, verify_calls = 0, errors = 0, commands = 0,
                     registration_errors = 0, registration_followers = 0, closed = 0, next_id = 100;
    std::atomic<bool> allow = true, present = true;
    explicit Backend(std::shared_ptr<RegistryCapabilityListener> r)
        : registry(std::move(r)), sessions(store, false, std::make_shared<PresenceCache>([] { return 0.; })),
          verification(*this), verifier(std::make_shared<Verifier>(*this)) {
    }
    std::string scene_name() const override {
        return "Scene";
    }
    std::optional<std::string> region_name() const override {
        return "Region";
    }
    std::optional<std::string> external_host() const override {
        return "127.0.0.1";
    }
    std::shared_ptr<CapabilityRuntimeListener> listener() const override {
        return registry;
    }
    int32_t random_next(int32_t max) override {
        if (max != 30000000)
            throw std::runtime_error("random bound");
        return next_id.fetch_add(1);
    }
    void debug(const std::string& text) override {
        if (text.starts_with("[CAPS]: Received SEED"))
            ++seed_requests;
    }
    void warn(const std::string&) override {
    }
    void error(const std::string&) override {
        ++errors;
    }
    void console(const std::string&) override {
    }
    void add_command(CapabilityCommand) override {
        ++commands;
    }
    void event_error(const std::string&, const std::exception&) override {
        ++registration_errors;
    }
    framework::AgentCircuitManager* authenticate_handler() const override {
        return &circuits;
    }
    std::shared_ptr<UserAgentVerificationModule> user_verification_module() override {
        return verifier;
    }
    std::shared_ptr<VerifiedScenePresence> scene_presence(const framework::Uuid& a) override {
        return present && a == identity(2) ? std::make_shared<Actor>() : nullptr;
    }
    IPresenceService* presence_service() override {
        return &sessions;
    }
    void close_agent(const framework::Uuid& a, bool) override {
        auto circuit = circuits.get_agent_circuit_data(a);
        runtime->client_closed(a);
        if (circuit)
            runtime->capabilities().remove_caps(a, circuit->circuit_code);
        circuits.remove_circuit(a);
        present = false;
        ++closed;
    }
    void cleanup_exception(const std::exception&) override {
        ++errors;
    }
    bool check_client(framework::Uuid a, const HttpRequest& request) override {
        ++checks;
        auto ip = framework::IpAddress::parse(request.remote_ip);
        if (!ip)
            throw std::runtime_error("Invalid peer IP");
        framework::IpEndpoint endpoint{std::make_shared<framework::IpAddress>(*ip),
                                       static_cast<uint16_t>(request.remote_port)};
        return verification.check_client(a, &endpoint);
    }
    void admit(const std::string& path) {
        incoming = std::make_shared<framework::AgentCircuitData>();
        incoming->agent_id = identity(2);
        incoming->session_id = identity(3);
        incoming->secure_session_id = identity(4);
        incoming->circuit_code = 123;
        incoming->caps_path = path;
        incoming->teleport_flags = 0x40000000;
        circuits.add_new_circuit(incoming);
        sessions.login(incoming->agent_id.str(), incoming->session_id, incoming->secure_session_id);
        present = true;
        runtime->capabilities().set_agent_caps_seeds(incoming);
        runtime->capabilities().create_caps(incoming->agent_id, incoming->circuit_code);
    }
};
bool Verifier::verify_client(const std::shared_ptr<framework::AgentCircuitData>& circuit,
                             const std::string& address) {
    ++backend.verify_calls;
    return backend.allow && circuit->agent_id == identity(2) && (address == "127.0.0.1" || address == "::1");
}
int main(int argc, char** argv) {
    try {
        std::shared_ptr<RegistryCapabilityListener> registry;
        std::shared_ptr<Backend> backend;
        std::unique_ptr<SceneCapabilities> runtime;
        PollHttpService polls(2);
        HttpServerOptions options;
        options.port = 0;
        options.workers = 2;
        options.queued_requests = 128;
        if (argc == 3) {
            options.certificate = argv[1];
            options.private_key = argv[2];
        }
        AsyncHttpServer http(options, [&](const HttpRequest& request,
                                          std::shared_ptr<HttpResponseChannel> channel) {
            polls.dispatch(*registry, request, channel, [&](const HttpRequest& r) {
                HttpResponse response;
                if (r.path == "/activate")
                    runtime->capabilities().activate_caps(123);
                else if (r.path == "/register") {
                    backend->incoming->caps_path = r.body;
                    runtime->capabilities().set_agent_caps_seeds(backend->incoming);
                    runtime->capabilities().create_caps(identity(2), 123);
                } else if (r.path == "/readmit")
                    backend->admit(r.body);
                else if (r.path == "/allow")
                    backend->allow = r.body != "0";
                else if (r.path == "/enqueue")
                    runtime->event_queue()->enqueue(
                        std::make_shared<std::vector<uint8_t>>(r.body.begin(), r.body.end()), identity(2));
                else if (r.path == "/physics")
                    runtime->event_queue()->physics_properties(1234, 2, 1000, .5f, .1f, 1, identity(2));
                else if (r.path == "/closed")
                    backend->close_agent(identity(2), false);
                else if (r.path == "/deregister")
                    runtime->capabilities().remove_caps(identity(2), 123);
                else if (r.path == "/disable-eq")
                    runtime->event_queue()->remove_region(runtime->event_scene());
                else if (r.path == "/enable-eq")
                    runtime->event_queue()->add_region(runtime->event_scene());
                else if (r.path == "/shutdown")
                    runtime->shutdown();
                else if (r.path == "/state") {
                    auto v = Value::object({});
                    v.set("caps", Value(int32_t(runtime->capabilities().caps_count())));
                    v.set("polls", Value(int32_t(registry->routes.poll_keys().size())));
                    v.set("simple", Value(int32_t(registry->routes.simple_keys().size())));
                    v.set("checks", Value(backend->checks.load()));
                    v.set("seed_requests", Value(backend->seed_requests.load()));
                    v.set("presence", Value(backend->sessions.agent(identity(3)).has_value()));
                    v.set("verifications", Value(backend->verify_calls.load()));
                    v.set("errors", Value(backend->errors.load()));
                    v.set("commands", Value(backend->commands.load()));
                    v.set("event_errors", Value(backend->registration_errors.load()));
                    v.set("followers", Value(backend->registration_followers.load()));
                    v.set("closed", Value(backend->closed.load()));
                    v.set("pending", Value(int32_t(polls.pending_count())));
                    v.set("circuits", Value(int32_t(backend->circuits.get_agent_circuits().size())));
                    auto caps =
                        std::dynamic_pointer_cast<Caps>(runtime->capabilities().get_caps_for_user(123));
                    v.set("flags", Value(caps ? int32_t(uint32_t(caps->flags)) : 0));
                    response.body = osd_json(v, true);
                    response.content_type = "application/json";
                } else if (r.path == "/ready") {
                    response.status = 503;
                    response.body = "Native world runtime incomplete";
                } else {
                    response.status = 404;
                }
                return response;
            });
        });
        polls.start();
        http.start([&](int port) {
            registry = std::make_shared<RegistryCapabilityListener>(
                CapabilityListenerInfo{argc == 3, static_cast<uint32_t>(port), static_cast<uint32_t>(port),
                                       std::string("localhost")});
            backend = std::make_shared<Backend>(registry);
            runtime = std::make_unique<SceneCapabilities>(backend);
            backend->runtime = runtime.get();
            runtime->events().on_register_caps.add(
                {{backend.get(), "Fail"},
                 [](auto, auto) { throw std::runtime_error("controlled subscriber failure"); }});
            runtime->events().on_register_caps.add(
                {{backend.get(), "Follower"}, [b = backend](auto, auto) { ++b->registration_followers; }});
            backend->admit("first");
            std::cout << "LISTENING " << port << std::endl;
        });
        std::string command;
        std::getline(std::cin, command);
        polls.stop();
        http.stop();
        runtime->shutdown();
        runtime.reset();
        backend->runtime = nullptr;
        if (!registry->routes.poll_keys().empty() || !registry->routes.simple_keys().empty())
            throw std::runtime_error("Capability routes retained after owner shutdown");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
