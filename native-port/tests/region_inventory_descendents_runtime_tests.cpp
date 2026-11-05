// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_inventory_descendents_runtime.hpp"
#include "opensim/services/scene_capabilities.hpp"
#include "opensim/server/http_route_registry.hpp"
#include "opensim/server/poll_http_service.hpp"
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
using namespace opensim;
using namespace services;
namespace {
int checks = 0;
void check(bool value, const char *text) {
    ++checks;
    if (!value)
        throw std::runtime_error(text);
}
framework::Uuid id(int n) {
    auto tail = std::to_string(n);
    return framework::Uuid("00000000-0000-0000-0000-" + std::string(12 - tail.size(), '0') + tail);
}
template <class F> void until(F condition) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Timed out waiting for inventory worker");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
struct Backend final : SceneCapabilityBackend {
    std::shared_ptr<server::RegistryCapabilityListener> registry;
    explicit Backend(std::shared_ptr<server::RegistryCapabilityListener> r) : registry(std::move(r)) {}
    std::string scene_name() const override {
        return "Descendents scene";
    }
    std::optional<std::string> region_name() const override {
        return "Descendents region";
    }
    std::optional<std::string> external_host() const override {
        return "127.0.0.1";
    }
    std::shared_ptr<CapabilityRuntimeListener> listener() const override {
        return registry;
    }
    bool check_client(framework::Uuid, const network::HttpRequest &) override {
        return true;
    }
    void debug(const std::string &) override {}
    void warn(const std::string &) override {}
    void error(const std::string &) override {}
    void console(const std::string &) override {}
    void add_command(CapabilityCommand) override {}
};
struct Channel final : network::HttpResponseChannel {
    bool can_send() const override {
        return true;
    }
    bool is_sending() const override {
        return false;
    }
    bool is_closed() const override {
        return false;
    }
    bool complete(network::HttpResponse) override {
        return false;
    }
    bool complete_raw(network::RawHttpResponse) override {
        return false;
    }
    int requests_remaining() const override {
        return 20;
    }
    int idle_timeout_ms() const override {
        return 30000;
    }
    void idle_timeout_ms(int) override {}
    bool send_chunk(std::shared_ptr<const std::vector<std::uint8_t>>, std::size_t, std::size_t,
                    std::function<void()>) override {
        return false;
    }
    void finish_response(bool) override {}
    void on_disconnect(std::function<void()>) override {}
    void cancel() override {}
};
struct ConfigFile {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("opensim-desc-" + framework::Uuid::random().str() + ".ini");
    explicit ConfigFile(const std::string &url) {
        std::ofstream file(path);
        file << "[ClientStack.LindenCaps]\nCap_FetchInventoryDescendents2=" << url << '\n';
    }
    ~ConfigFile() {
        std::error_code error;
        std::filesystem::remove(path, error);
    }
};
std::shared_ptr<Caps> create(SceneCapabilities &scene, int code) {
    auto circuit = std::make_shared<framework::AgentCircuitData>();
    circuit->agent_id = id(code);
    circuit->circuit_code = code;
    circuit->caps_path = "desc" + std::to_string(code);
    scene.capabilities().set_agent_caps_seeds(circuit);
    scene.capabilities().create_caps(circuit->agent_id, code);
    scene.capabilities().activate_caps(code);
    return std::dynamic_pointer_cast<Caps>(scene.capabilities().get_caps_for_user(code));
}
network::HttpRequest request(int key) {
    network::HttpRequest r;
    r.method = "POST";
    r.response_channel = std::make_shared<Channel>();
    r.body =
        "<llsd><map><key>folders</key><array><map><key>folder_id</key><uuid>" + id(key).str() +
        "</uuid><key>owner_id</key><uuid>" + id(100).str() +
        "</uuid><key>fetch_items</key><boolean>1</boolean><key>fetch_folders</key><boolean>1</boolean></map></array></map></llsd>";
    return r;
}
void run() {
    auto registry = std::make_shared<server::RegistryCapabilityListener>(
        server::CapabilityListenerInfo{false, 19005, 0, {}});
    SceneCapabilities scene(std::make_shared<Backend>(registry));
    auto before = scene.events().on_register_caps.snapshot().size();
    ConfigFile file("localhost");
    auto config = server::IniConfig::load(file.path);
    std::mutex mutex;
    std::condition_variable changed;
    int entered = 0;
    bool block = false, release = false;
    framework::Uuid selected;
    std::atomic<int> provider_calls = 0, errors = 0;
    std::atomic<bool> shutting = false, throw_provider = false;
    InventoryDescendentsBackend backend;
    backend.multiple_content =
        [&](auto owner, const auto &ids) -> std::optional<InventoryDescendentsBackend::Collections> {
        ++provider_calls;
        if (throw_provider)
            throw std::runtime_error("inventory provider failure");
        {
            std::unique_lock lock(mutex);
            selected = owner;
            if (block) {
                ++entered;
                changed.notify_all();
                changed.wait(lock, [&] { return release; });
            }
        }
        InventoryCollection c;
        c.owner = owner;
        c.folder = ids.at(0);
        c.version = 4;
        return InventoryDescendentsBackend::Collections{c};
    };
    backend.folder = [](auto, auto) -> std::optional<InventoryFolder> { return {}; };
    backend.multiple_items = [](auto, const auto &) -> std::optional<FetchInventory2Core::Items> {
        return {};
    };
    backend.log = [&](auto category) {
        if (category == "inventory-worker-error")
            ++errors;
    };
    RegionInventoryDescendentsRuntime runtime(config, scene.shared_events(), backend,
                                              [&] { return shutting.load(); });
    struct Unblock {
        std::mutex &mutex;
        std::condition_variable &changed;
        bool &release;
        ~Unblock() {
            {
                std::lock_guard lock(mutex);
                release = true;
            }
            changed.notify_all();
        }
    } unblock{mutex, changed, release};
    auto caps = create(scene, 123);
    std::shared_ptr<CapabilityPollHandler> poll;
    check(caps->try_get_poll("FetchInventoryDescendents2", poll), "source local poll capability registered");
    check(poll->id == id(123) && poll->timeout_ms == INT32_MAX, "source poll agent and timeout");
    check(registry->routes.poll_keys().size() == 2, "inventory and event queue poll routes");
    auto timeout = poll->no_events({}, {});
    check(std::any_cast<std::string>(timeout->get("str_response_string")) == "Script timeout" &&
              std::any_cast<int32_t>(timeout->get("int_response_code")) == 500 &&
              !std::any_cast<bool>(timeout->get("keepalive")),
          "source timeout response");
    check(!poll->request(id(1), request(1)), "poll request queues rather than returns direct response");
    until([&] { return poll->has_events(id(1), {}) && runtime.processed_requests() == 1; });
    check(runtime.processed_requests() == 1, "processed statistic advances");
    auto table = poll->get_events(id(1), {});
    auto response = std::any_cast<server::PollResponsePtr>(table->get("h"));
    auto model = server::PollHttpService::response_state(response);
    auto bytes = model->raw_buffer();
    std::string body(bytes->begin(), bytes->end());
    check(model->status() == 200 && body.find("<integer>4</integer>") != body.npos,
          "actual descendents core response retained");
    check(selected == id(100), "source body owner used instead of capability agent");
    check(!poll->has_events(id(1), {}), "GetEvents consumes response");
    bool missing = false;
    try {
        poll->get_events(id(1), {});
    } catch (const std::out_of_range &) {
        missing = true;
    }
    check(missing, "source missing response lookup throws");
    poll->request(id(2), request(2));
    until([&] { return poll->has_events(id(2), {}); });
    poll->drop(id(2), {});
    check(!poll->has_events(id(2), {}), "drop removes completed response");
    poll->request({}, request(1));
    until([&] { return poll->has_events({}, {}); });
    poll->get_events({}, {});
    until([&] { return runtime.processed_requests() == 3; });
    check(runtime.processed_requests() == 3, "source zero request UUID is processed");
    // Saturate both source workers before enqueueing a pre-dropped request.
    {
        std::lock_guard lock(mutex);
        block = true;
        release = false;
        entered = 0;
    }
    poll->request(id(3), request(3));
    poll->request(id(4), request(4));
    poll->request(id(5), request(5));
    until([&] {
        std::lock_guard lock(mutex);
        return entered == 2;
    });
    int calls_before = provider_calls.load();
    poll->drop(id(5), {});
    poll->drop(id(3), {});
    {
        std::lock_guard lock(mutex);
        release = true;
        block = false;
    }
    changed.notify_all();
    until([&] { return runtime.queued_requests() == 0 && runtime.processed_requests() == 5; });
    check(provider_calls == calls_before, "pre-dropped request bypasses provider");
    check(!poll->has_events(id(3), {}) && poll->has_events(id(4), {}),
          "in-flight drop discards result but counts processing");
    poll->get_events(id(4), {});
    shutting = true;
    poll->request(id(6), request(6));
    until([&] { return runtime.queued_requests() == 0; });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(!poll->has_events(id(6), {}) && runtime.processed_requests() == 5,
          "scene shutdown suppresses processing");
    shutting = false;
    throw_provider = true;
    poll->request(id(8), request(8));
    until([&] { return errors == 1; });
    throw_provider = false;
    check(!poll->has_events(id(8), {}) && runtime.processed_requests() == 5,
          "provider exception logs but publishes no fabricated response");
    // stop must wait for a currently executing provider before releasing workers.
    {
        std::lock_guard lock(mutex);
        block = true;
        release = false;
        entered = 0;
    }
    poll->request(id(7), request(7));
    until([&] {
        std::lock_guard lock(mutex);
        return entered == 1;
    });
    auto stopping = std::async(std::launch::async, [&] { runtime.stop(); });
    check(stopping.wait_for(std::chrono::milliseconds(30)) == std::future_status::timeout,
          "stop waits for active worker");
    {
        std::lock_guard lock(mutex);
        release = true;
        block = false;
    }
    changed.notify_all();
    stopping.get();
    runtime.stop();
    check(scene.events().on_register_caps.snapshot().size() == before, "stop unsubscribes idempotently");
    bool rejected = false;
    try {
        poll->request(id(8), request(8));
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    check(rejected, "retained poll cannot enqueue after worker stop");
    auto fresh = create(scene, 124);
    std::shared_ptr<CapabilityPollHandler> absent;
    check(!fresh->try_get_poll("FetchInventoryDescendents2", absent), "new caps after stop omit inventory");
    scene.shutdown();
    check(registry->routes.poll_keys().empty() && registry->routes.simple_keys().empty(),
          "caps owner releases routes");
    check(errors == 1, "only injected provider failure reported");
    bool invalid = false;
    try {
        RegionInventoryDescendentsRuntime missing_provider(config, scene.shared_events(), {});
    } catch (const std::invalid_argument &) {
        invalid = true;
    }
    check(invalid && scene.events().on_register_caps.snapshot().empty(),
          "failed enabled constructor leaves no delegate");
    for (const auto &url : {std::string{}, std::string("https://inventory.invalid/")}) {
        auto other_registry = std::make_shared<server::RegistryCapabilityListener>(
            server::CapabilityListenerInfo{false, 19006, 0, {}});
        SceneCapabilities other(std::make_shared<Backend>(other_registry));
        ConfigFile other_file(url);
        RegionInventoryDescendentsRuntime other_runtime(
            server::IniConfig::load(other_file.path), other.shared_events(),
            url.empty() ? InventoryDescendentsBackend{} : backend);
        auto other_caps = create(other, 125);
        check(!other_caps->try_get_poll("FetchInventoryDescendents2", absent),
              "disabled/external config has no local poll");
        check(other_caps->external_paths().contains("FetchInventoryDescendents2") == !url.empty(),
              "source external URL advertised only when enabled");
        other_runtime.stop();
        other.shutdown();
    }
}
}
int main() {
    try {
        run();
        std::cout << "PASS: " << checks << " region inventory descendents runtime checks\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
