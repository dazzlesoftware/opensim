// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/event_queue.hpp"
#include <iostream>
#include <limits>
using namespace opensim;
using namespace opensim::services;
namespace opensim::services {
struct EventQueueProbe {
    static void id(EventQueueModule& m, framework::Uuid a, int32_t value) {
        m.ids_[a.str()] = value;
    }
};
}
namespace {
int checks = 0;
void check(bool value, const char* message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
struct Backend : EventQueueBackend {
    int draws = 0;
    std::vector<std::string> messages;
    std::vector<CapabilityCommand> commands;
    framework::Uuid random_uuid() override {
        return framework::Uuid("00000000-0000-0000-0000-000000000001");
    }
    int32_t random_next(int32_t maximum) override {
        check(maximum == 30000000, "random bound");
        return 40 + draws++;
    }
    void debug(const std::string& s) override {
        messages.push_back(s);
    }
    void warn(const std::string& s) override {
        messages.push_back(s);
    }
    void error(const std::string& s) override {
        messages.push_back(s);
    }
    void console(const std::string& s) override {
        messages.push_back(s);
    }
    void add_command(CapabilityCommand c) override {
        commands.push_back(std::move(c));
    }
};
struct Scene : EventQueueScene {
    int registered = 0, closed = 0, caps = 0;
    std::string name() const override {
        return "Scene";
    }
    std::string region_name() const override {
        return "Region";
    }
    void register_module(EventQueueModule*) override {
        ++registered;
    }
    void unregister_module(EventQueueModule*) override {
        --registered;
    }
    void subscribe_closed(EventQueueModule*, Closed) override {
        ++closed;
    }
    void unsubscribe_closed(EventQueueModule*) override {
        --closed;
    }
    void subscribe_register(EventQueueModule*, Register) override {
        ++caps;
    }
    void unsubscribe_register(EventQueueModule*) override {
        --caps;
    }
};
struct CapsSink : EventQueueCaps {
    std::vector<std::shared_ptr<CapabilityPollHandler>> handlers;
    void register_poll(const std::string& name, std::shared_ptr<CapabilityPollHandler> h) override {
        check(name == "EventQueueGet", "cap name");
        handlers.push_back(std::move(h));
    }
};
server::PollBytes bytes(std::string s) {
    return std::make_shared<std::vector<uint8_t>>(s.begin(), s.end());
}
int status(server::PollTablePtr r) {
    return std::any_cast<int32_t>(r->get("int_response_code"));
}
std::string body(server::PollTablePtr r) {
    auto b = std::any_cast<server::PollBytes>(r->get("bin_response_data"));
    return {b->begin(), b->end()};
}
std::string response(std::string events, int id) {
    return "<llsd><map><key>events</key><array>" + events + "</array><key>id</key>" +
           (id == 0 ? "<integer />" : "<integer>" + std::to_string(id) + "</integer>") + "</map></llsd>";
}
void run() {
    Backend backend;
    auto module = std::make_shared<EventQueueModule>(backend);
    auto scene = std::make_shared<Scene>();
    auto caps = std::make_shared<CapsSink>();
    framework::Uuid agent("00000000-0000-0000-0000-000000000002"), request;
    module->add_region(scene);
    check(scene->registered == 1 && scene->closed == 1 && scene->caps == 1 && backend.commands.size() == 2,
          "region hooks");
    check(module->has_events(request, agent), "missing agent wakes poll");
    check(status(module->get_events(request, agent)) == 404, "missing agent 404");
    module->on_register_caps(agent, caps);
    auto handler = caps->handlers.back();
    check(handler->id == agent && handler->timeout_ms == 58000 && !handler->request, "poll metadata");
    check(handler->url == "/CE/00000000-0000-0000-0000-000000000001", "poll URL");
    check(!handler->has_events(request, agent) && status(handler->no_events(request, agent)) == 502,
          "empty existing queue");
    auto raw = bytes("A");
    module->enqueue(raw, agent);
    (*raw)[0] = 'B';
    auto utf8 = std::make_shared<EventQueueByteBuffer>();
    utf8->text = "C";
    module->enqueue_utf8(utf8, agent);
    utf8->text = "D";
    handler->drop(request, agent);
    check(handler->has_events(request, agent), "drop retains events");
    auto first = handler->get_events(request, agent);
    check(status(first) == 200 && body(first) == response("BC", 40), "raw alias and UTF8 snapshot ordering");
    check(std::any_cast<int32_t>(first->get("keepaliveTimeout")) == 60, "keepalive");
    module->enqueue(bytes("old"), agent);
    module->on_register_caps(agent, caps);
    module->enqueue(bytes("new"), agent);
    check(caps->handlers.back()->url == handler->url, "reconnect URL reused");
    check(body(module->get_events(request, agent)) == response("old", 41),
          "old events stop at reconnect marker");
    check(backend.draws == 2, "marker resets random sequence");
    check(body(module->get_events(request, agent)) == response("new", 41), "new batch uses new sequence");
    module->enqueue(bytes(""), agent);
    check(status(module->get_events(request, agent)) == 502, "empty array advances ID without response");
    module->enqueue(bytes("next"), agent);
    check(body(module->get_events(request, agent)) == response("next", 43), "empty event consumed sequence");
    EventQueueProbe::id(*module, agent, 0);
    module->enqueue(bytes("zero"), agent);
    check(body(module->get_events(request, agent)) == response("zero", 0), "SDK zero integer spelling");
    EventQueueProbe::id(*module, agent, std::numeric_limits<int32_t>::max());
    module->enqueue(bytes("max"), agent);
    check(body(module->get_events(request, agent)) == response("max", std::numeric_limits<int32_t>::max()),
          "max sequence");
    module->enqueue(bytes("min"), agent);
    check(body(module->get_events(request, agent)) == response("min", std::numeric_limits<int32_t>::min()),
          "unchecked negative overflow");
    std::vector<std::optional<std::string>> args = {"debug", "eq", "+-1"};
    module->handle_debug(&args);
    check(int32_t(module->debug_level) == 0, "reject double sign");
    args[2] = " +2 ";
    module->handle_debug(&args);
    check(int32_t(module->debug_level) == 2, "parse signed debug level");
    module->handle_show();
    module->client_closed(agent, nullptr);
    check(status(handler->get_events(request, agent)) == 404 && handler->has_events(request, agent),
          "old callback after cleanup");
    check(module->enqueue(bytes("orphan"), agent), "missing queue logs but returns true");
    module->remove_region(std::make_shared<Scene>());
    check(scene->registered == 1, "other scene ignored");
    module->remove_region(scene);
    check(scene->registered == 0 && scene->closed == 0 && scene->caps == 0, "region unhook");
    check(!module->enqueue(bytes("orphan"), agent), "null scene enqueue handled");
    check(event_queue_inner_xml(network::Value("<&\"'>")) == "<string>&lt;&amp;&quot;&apos;&gt;</string>",
          "legacy escaping");
    check(event_queue_inner_xml(network::Value(int32_t(0))) == "<integer>0</integer>",
          "legacy zero differs from queue ID");
    check(event_queue_inner_xml(network::Value{}) == "<undef />", "legacy undefined");
    network::Value date("1970-01-01T00:00:00Z");
    date.kind = network::Value::Kind::Date;
    check(event_queue_inner_xml(date) == "<date />", "legacy epoch date");
}
}
#ifndef EVENT_QUEUE_PROBE
int main() {
    try {
        run();
        std::cout << "PASS: " << checks << " event queue checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
#endif
