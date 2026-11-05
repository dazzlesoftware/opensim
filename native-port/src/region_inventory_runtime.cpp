// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_inventory_runtime.hpp"
#include <bit>
#include <chrono>
#include <mutex>
#include <stdexcept>
namespace opensim::services {
struct RegionInventoryRuntime::State {
    using Clock = std::chrono::steady_clock;
    std::mutex mutex;
    std::shared_ptr<SceneCapabilityEvents> events;
    FetchInventory2Core::GetMultipleItems provider;
    std::optional<LibraryItemBackend> library;
    std::shared_ptr<InventoryBadRequests> bad;
    Clock::time_point next_purge;
    std::string url, library_url;
    bool subscribed = false, stopped = false;
    SceneDelegateIdentity identity() const {
        return {this, "FetchInventory2.RegisterCaps"};
    }
    void register_caps(framework::Uuid agent, const std::shared_ptr<Caps> &caps) {
        std::lock_guard lock(mutex);
        if (stopped)
            return;
        if (!caps)
            throw std::invalid_argument("Null inventory Caps");
        if (url == "localhost") {
            auto core = std::make_shared<FetchInventory2Core>(agent, provider);
            caps->register_simple("FetchInventory2",
                                  std::make_shared<FetchInventory2Route>(
                                      "/" + framework::Uuid::random().str(), std::move(core), bad));
        }
        if (library_url == "localhost") {
            auto core = std::make_shared<FetchLibrary2Core>(agent, library);
            caps->register_simple("FetchLib2",
                                  std::make_shared<FetchLibrary2Route>("/" + framework::Uuid::random().str(),
                                                                       std::move(core), bad));
        }
    }
    void stop() {
        std::lock_guard lock(mutex);
        if (stopped)
            return;
        stopped = true;
        if (subscribed) {
            events->on_register_caps.remove(identity());
            subscribed = false;
        }
    }
};
RegionInventoryRuntime::RegionInventoryRuntime(const server::IniConfig &config,
                                               std::shared_ptr<SceneCapabilityEvents> events,
                                               FetchInventory2Core::GetMultipleItems provider,
                                               std::optional<LibraryItemBackend> library)
    : state_(std::make_shared<State>()) {
    if (!events)
        throw std::invalid_argument("Null inventory capability events");
    state_->events = std::move(events);
    state_->url = config.get("ClientStack.LindenCaps", "Cap_FetchInventory2", "");
    if (state_->url.empty())
        return;
    state_->library_url = config.get("ClientStack.LindenCaps", "Cap_FetchLib2", "localhost");
    state_->library = std::move(library);
    if (!provider)
        throw std::invalid_argument("Enabled inventory capability requires inventory provider");
    state_->provider = std::move(provider);
    const auto origin = State::Clock::now();
    state_->next_purge = origin + std::chrono::milliseconds(30000);
    state_->bad = std::make_shared<InventoryBadRequests>([origin] {
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(State::Clock::now() - origin).count();
        return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(elapsed));
    });
    std::weak_ptr<State> weak = state_;
    state_->events->on_register_caps.add({state_->identity(), [weak](auto agent, auto caps) {
                                              if (auto state = weak.lock())
                                                  state->register_caps(agent, caps);
                                          }});
    state_->subscribed = true;
}
RegionInventoryRuntime::~RegionInventoryRuntime() {
    try {
        stop();
    } catch (...) {
    }
}
void RegionInventoryRuntime::stop() {
    state_->stop();
}
void RegionInventoryRuntime::poll() {
    std::lock_guard lock(state_->mutex);
    if (state_->stopped || !state_->bad)
        return;
    auto now = State::Clock::now();
    if (now >= state_->next_purge) {
        state_->bad->purge();
        state_->next_purge = now + std::chrono::milliseconds(30000);
    }
}
}
