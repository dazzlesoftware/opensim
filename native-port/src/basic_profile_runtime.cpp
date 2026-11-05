// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/basic_profile_runtime.hpp"
#include <map>
namespace opensim::scene {
namespace {
struct RuntimeScene : BasicProfileScene {
    std::shared_ptr<BasicProfileAccounts> service;
    framework::Uuid scope_id;
    std::vector<std::function<void(std::shared_ptr<BasicProfileClient>)>> clients;
    std::shared_ptr<ProfileModule> module;
    std::shared_ptr<BasicProfileAccounts> accounts() override {
        return service;
    }
    framework::Uuid scope() override {
        return scope_id;
    }
    void on_new_client(std::function<void(std::shared_ptr<BasicProfileClient>)> handler) override {
        clients.push_back(std::move(handler));
    }
    void register_profile(std::shared_ptr<ProfileModule> value) override {
        module = std::move(value);
    }
};
struct RuntimeClient : BasicProfileClient, std::enable_shared_from_this<RuntimeClient> {
    framework::Uuid agent, session;
    std::shared_ptr<RuntimeScene> owner;
    BasicProfileRuntime::Sender send;
    std::vector<std::function<void(std::shared_ptr<BasicProfileClient>, framework::Uuid)>> requests;
    std::shared_ptr<ProfileSceneBase> scene() override {
        return owner;
    }
    void on_properties_request(
        std::function<void(std::shared_ptr<BasicProfileClient>, framework::Uuid)> handler) override {
        requests.push_back(std::move(handler));
    }
    void send_properties(const AvatarProfileProperties& props) override {
        send(agent, session, avatar_profile_properties_packet(agent, props));
    }
    void send_interests(const AvatarProfileInterests& interests) override {
        send(agent, session, avatar_profile_interests_packet(agent, interests));
    }
    void request(framework::Uuid avatar) {
        auto handlers = requests;
        for (auto& handler : handlers)
            handler(shared_from_this(), avatar);
    }
};
}
struct BasicProfileRuntime::State {
    std::shared_ptr<BasicProfileModule> module;
    std::shared_ptr<RuntimeScene> scene = std::make_shared<RuntimeScene>();
    Sender sender;
    std::recursive_mutex mutex;
    std::map<framework::Uuid, std::shared_ptr<RuntimeClient>> clients;
    bool stopped = false;
};
BasicProfileRuntime::BasicProfileRuntime(std::shared_ptr<BasicProfileModule> module,
                                         std::shared_ptr<BasicProfileAccounts> accounts,
                                         framework::Uuid scope, Sender sender)
    : state_(std::make_unique<State>()) {
    state_->module = std::move(module);
    state_->scene->service = std::move(accounts);
    state_->scene->scope_id = scope;
    state_->sender = std::move(sender);
    state_->module->add_region(state_->scene);
    state_->module->region_loaded(state_->scene);
}
BasicProfileRuntime::~BasicProfileRuntime() {
    stop();
}
void BasicProfileRuntime::new_client(framework::Uuid agent, framework::Uuid session) {
    std::lock_guard lock(state_->mutex);
    if (state_->stopped)
        return;
    auto client = std::make_shared<RuntimeClient>();
    client->agent = agent;
    client->session = session;
    client->owner = state_->scene;
    client->send = state_->sender;
    state_->clients[agent] = client;
    auto handlers = state_->scene->clients;
    for (auto& handler : handlers)
        handler(client);
}
void BasicProfileRuntime::request(framework::Uuid agent, framework::Uuid session, framework::Uuid avatar) {
    std::shared_ptr<RuntimeClient> client;
    {
        std::lock_guard lock(state_->mutex);
        auto found = state_->clients.find(agent);
        if (state_->stopped || found == state_->clients.end() || found->second->session != session)
            return;
        client = found->second;
    }
    client->request(avatar);
}
void BasicProfileRuntime::remove_client(framework::Uuid agent) {
    std::lock_guard lock(state_->mutex);
    state_->clients.erase(agent);
}
void BasicProfileRuntime::stop() {
    std::lock_guard lock(state_->mutex);
    if (state_->stopped)
        return;
    state_->stopped = true;
    state_->clients.clear();
    state_->module->remove_region(state_->scene);
    state_->scene->clients.clear();
    state_->scene->module.reset();
}
}
