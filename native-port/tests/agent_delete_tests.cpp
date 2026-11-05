// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/agent_delete.hpp"
#include <future>
#include <iostream>
#include <stdexcept>
using namespace opensim;
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("lifecycle check failed");
}
struct Service : services::ISimulationAgentLifecycle {
    std::shared_ptr<std::promise<std::string>> completed;
    explicit Service(std::shared_ptr<std::promise<std::string>> p) : completed(std::move(p)) {}
    bool release_agent(const framework::Uuid&, const framework::Uuid&, const std::string&) override {
        throw std::runtime_error("unexpected release");
    }
    bool close_agent(services::GridRegion& region, const framework::Uuid& agent,
                     const std::optional<std::string>& auth) override {
        completed->set_value(region.id.str() + "/" + agent.str() + "/" + auth.value_or("null"));
        return false;
    }
};
int main() {
    try {
        std::function<void()> queued;
        auto result = std::make_shared<std::promise<std::string>>();
        auto future = result->get_future();
        std::weak_ptr<Service> weak;
        network::HttpResponse response;
        const framework::Uuid region("00000000-0000-0000-0000-000000000008"),
            agent("00000000-0000-0000-0000-000000000007");
        {
            auto service = std::make_shared<Service>(result);
            weak = service;
            server::AgentDeleteHandler handler(service,
                                               [&](std::function<void()> work, const std::string& context) {
                                                   check(context == "AgentHandler.DoAgentDelete");
                                                   queued = std::move(work);
                                               });
            handler.delete_agent(response, agent, "close", region, std::nullopt, "127.0.0.1");
            check(response.status == 200 && response.body == "OpenSim agent " + agent.str());
            check(future.wait_for(std::chrono::seconds(0)) == std::future_status::timeout);
        }
        check(!weak.expired());
        auto worker = std::async(std::launch::async, std::move(queued));
        worker.get();
        check(future.get() == region.str() + "/" + agent.str() + "/null");
        check(weak.expired());
        std::cout << "PASS: queued agent close owns service after handler destruction\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    }
}
