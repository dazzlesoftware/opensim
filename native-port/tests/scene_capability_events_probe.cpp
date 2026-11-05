#include "opensim/services/scene_capability_events.hpp"
#include "opensim/server/http_route_registry.hpp"
#include <iostream>
using namespace opensim;
using namespace services;
struct ScenePayload : EventQueueScene {
    std::string name() const override {
        return "Scene";
    }
    std::string region_name() const override {
        return "Region";
    }
    void register_module(EventQueueModule*) override {
    }
    void unregister_module(EventQueueModule*) override {
    }
    void subscribe_closed(EventQueueModule*, Closed) override {
    }
    void unsubscribe_closed(EventQueueModule*) override {
    }
    void subscribe_register(EventQueueModule*, Register) override {
    }
    void unsubscribe_register(EventQueueModule*) override {
    }
};
struct Fixture {
    int profile, targets[4]{}, depth = 0;
    bool mutated = false;
    char selected = 'r';
    std::string trace;
    SceneCapabilityEvents events;
    std::shared_ptr<Caps> caps;
    std::shared_ptr<EventQueueScene> scene;
    explicit Fixture(int p)
        : profile(p), events([this](const std::string& prefix, const std::exception& e) {
              trace += "error:" + prefix + e.what() + ";";
              if (profile & 64)
                  throw std::runtime_error("logger");
          }) {
        if (p & 8) {
            auto listener =
                std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{});
            caps = std::make_shared<Caps>(listener, "host", 0, "seed", framework::Uuid{}, "Region",
                                          [listener] { return listener; });
        }
        if (p & 16)
            scene = std::make_shared<ScenePayload>();
    }
    SceneDelegateIdentity identity(int n) {
        return {&targets[n], std::to_string(n)};
    }
    template <class E> typename E::Handler handler(int n, char channel) {
        return {identity(n), [this, n, channel](auto agent, auto value) {
                    trace += std::string(1, channel) + std::to_string(n) + ":" + agent.str() + ":" +
                             (value ? "1" : "0") + ";";
                    if (n == 1 && !mutated && (profile & 6)) {
                        mutated = true;
                        if (profile & 2)
                            add(channel, 3);
                        if (profile & 4)
                            remove(channel, 2);
                    }
                    if (n == 1 && (profile & 32) && depth == 0) {
                        ++depth;
                        trigger('D');
                        --depth;
                    }
                    if ((n == 2 && (profile & 1)) || (n == 3 && (profile & 128)))
                        throw std::runtime_error("failure" + std::to_string(n));
                }};
    }
    void add(char channel, int n) {
        if (channel == 'r')
            events.on_register_caps.add(handler<SceneCapabilityEvents::CapsEvent>(n, channel));
        else if (channel == 'e')
            events.on_deregister_caps.add(handler<SceneCapabilityEvents::CapsEvent>(n, channel));
        else
            events.on_client_closed.add(handler<SceneCapabilityEvents::ClosedEvent>(n, channel));
    }
    void remove(char channel, int n) {
        if (channel == 'r')
            events.on_register_caps.remove(identity(n));
        else if (channel == 'e')
            events.on_deregister_caps.remove(identity(n));
        else
            events.on_client_closed.remove(identity(n));
    }
    template <class E> void range(E& e, char channel, bool remove) {
        if (remove)
            e.remove(std::vector<SceneDelegateIdentity>{identity(1), identity(2)});
        else
            e.add(std::vector<typename E::Handler>{handler<E>(1, channel), handler<E>(2, channel)});
    }
    void range(bool remove) {
        if (selected == 'r')
            range(events.on_register_caps, selected, remove);
        else if (selected == 'e')
            range(events.on_deregister_caps, selected, remove);
        else
            range(events.on_client_closed, selected, remove);
    }
    void trigger(char op) {
        framework::Uuid agent("00000000-0000-0000-0000-000000000007");
        if (op == 'C')
            events.trigger_closed(agent, scene);
        else if (op == 'R')
            events.trigger_register(agent, caps);
        else
            events.trigger_deregister(agent, caps);
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        auto split = line.find(':');
        Fixture f(std::stoi(line.substr(0, split)));
        for (char c : line.substr(split + 1)) {
            try {
                switch (c) {
                case 'r':
                case 'e':
                case 'h':
                    f.selected = c;
                    break;
                case 'a':
                case 'd':
                    f.add(f.selected, 1);
                    break;
                case 'b':
                    f.add(f.selected, 2);
                    break;
                case 'c':
                    f.add(f.selected, 3);
                    break;
                case 'x':
                    f.remove(f.selected, 1);
                    break;
                case 'y':
                    f.remove(f.selected, 2);
                    break;
                case 'm':
                    f.range(false);
                    break;
                case 'n':
                    f.range(true);
                    break;
                case '0':
                    f.events.on_register_caps.add(SceneCapabilityEvents::CapsEvent::Handler{});
                    break;
                default:
                    f.trigger(c);
                    break;
                }
            } catch (...) {
                f.trace += "EX;";
            }
        }
        std::cout << f.trace << '\n';
    }
}
