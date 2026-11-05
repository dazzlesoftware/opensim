#include "opensim/services/event_queue.hpp"
#include "opensim/server/poll_http_service.hpp"
#include <iostream>
using namespace opensim;
using namespace services;
using namespace server;
using namespace network;
struct QueueBackend : EventQueueBackend {
    std::atomic<int32_t> draws = 40;
    int32_t random_next(int32_t) override {
        return draws.fetch_add(1);
    }
    void debug(const std::string&) override {
    }
    void warn(const std::string&) override {
    }
    void error(const std::string&) override {
    }
    void console(const std::string&) override {
    }
    void add_command(CapabilityCommand) override {
    }
};
int main(int argc, char** argv) {
    try {
        QueueBackend backend;
        auto module = std::make_shared<EventQueueModule>(backend);
        auto registry = std::make_shared<RegistryCapabilityListener>(CapabilityListenerInfo{});
        PollHttpService polls(2);
        framework::Uuid agent("00000000-0000-0000-0000-000000000002");
        auto caps = std::make_shared<Caps>(registry, "127.0.0.1", 9000, "fixture", agent, "Scene",
                                           [registry] { return registry; });
        auto binding = event_queue_caps(caps);
        module->on_register_caps(agent, binding);
        HttpServerOptions options;
        options.port = 0;
        options.workers = 1;
        options.queued_requests = 128;
        if (argc == 3) {
            options.certificate = argv[1];
            options.private_key = argv[2];
        }
        AsyncHttpServer http(options, [&](const HttpRequest& request,
                                          std::shared_ptr<HttpResponseChannel> channel) {
            polls.dispatch(*registry, request, channel, [&](const HttpRequest& incoming) {
                HttpResponse response;
                if (incoming.path == "/discover") {
                    std::shared_ptr<CapabilityPollHandler> handler;
                    caps->try_get_poll("EventQueueGet", handler);
                    response.body = *handler->url;
                } else if (incoming.path == "/event") {
                    auto text = incoming.body;
                    module->enqueue(std::make_shared<std::vector<uint8_t>>(text.begin(), text.end()), agent);
                } else if (incoming.path == "/produce") {
                    framework::Uuid id("01234567-89ab-cdef-0123-456789abcdef");
                    auto endpoint = std::make_shared<framework::IpEndpoint>();
                    endpoint->address =
                        std::make_shared<framework::IpAddress>(*framework::IpAddress::parse("192.0.2.37"));
                    endpoint->port = 9000;
                    if (incoming.body == "enable")
                        module->enable_simulator(1234, endpoint, agent, 256, 512);
                    else if (incoming.body == "teleport")
                        module->teleport_finish(1234, 21, endpoint, 999, 8192, "http://seed/", agent, 256,
                                                512);
                    else if (incoming.body == "cross")
                        module->cross_region(1234, {1, 2, 3}, {0, 1, 0}, endpoint, "http://seed/", agent, id,
                                             256, 512);
                    else if (incoming.body == "establish")
                        module->establish_communication(agent, endpoint, "http://seed/", 1234, 256, 512);
                    else if (incoming.body == "invite")
                        module->chatterbox_invitation(id, "session", id, "hello", agent, "name", 1, 123,
                                                      false, 0, {1, 2, 3}, 0, id, false, nullptr);
                    else if (incoming.body == "updates") {
                        std::vector<EventGroupMember> list{EventGroupMember(id)};
                        module->chatterbox_updates(id, agent, &list);
                    } else if (incoming.body == "chat-start")
                        module->chatterbox_start(id, "session", 0, true, false, id, true, std::nullopt,
                                                 agent);
                    else if (incoming.body == "chat-close")
                        module->chatterbox_close(agent, id, "closed");
                    else if (incoming.body == "groups") {
                        auto group = std::make_shared<EventGroupMembership>();
                        group->group = id;
                        group->name = "group";
                        group->powers = 1234;
                        std::vector<std::shared_ptr<EventGroupMembership>> list{group};
                        module->group_membership(agent, &list);
                    } else if (incoming.body == "places") {
                        std::vector<std::shared_ptr<EventPlace>> list;
                        module->places_reply(agent, id, id, &list);
                    } else if (incoming.body == "script")
                        module->script_running(id, id, true, agent);
                    else if (incoming.body == "physics")
                        module->physics_properties(1234, 2, 1000, .5f, .1f, 1, agent);
                    else if (incoming.body == "windlight")
                        module->windlight_refresh(1, agent);
                    else if (incoming.body == "inventory") {
                        InventoryItem item;
                        item.id = id;
                        item.name = "item";
                        module->bulk_inventory(&item, agent, id);
                    } else if (incoming.body == "generic") {
                        std::vector<std::optional<std::string>> list{"first", "<&"};
                        module->large_message(agent, id, id, "test", id, &list);
                    } else if (incoming.body == "generic-bytes") {
                        auto bytes = std::make_shared<std::vector<uint8_t>>(
                            std::initializer_list<uint8_t>{'r', 'a', 'w'});
                        std::vector<PollBytes> list{bytes};
                        module->large_message_bytes(agent, id, id, "test", id, &list);
                    } else if (incoming.body == "build")
                        module->enqueue(module->build_event("Custom", std::make_shared<Value>(int32_t(7))),
                                        agent);
                    else
                        response.status = 400;
                } else if (incoming.path == "/register")
                    module->on_register_caps(agent, binding);
                else if (incoming.path == "/closed")
                    module->client_closed(agent, nullptr);
                else if (incoming.path == "/remove")
                    caps->deregister_handlers();
                else if (incoming.path == "/pending")
                    response.body = std::to_string(polls.pending_count());
                else if (incoming.path == "/ready") {
                    response.status = 503;
                    response.body = "Native world runtime incomplete";
                } else {
                    response.status = 404;
                }
                return response;
            });
        });
        polls.start();
        http.start([](int port) { std::cout << "LISTENING " << port << std::endl; });
        std::string line;
        std::getline(std::cin, line);
        polls.stop();
        http.stop();
        caps->deregister_handlers();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
