// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/scene_capability_events.hpp"
namespace opensim::services {
// Scene data/policies are supplied by the world host. Capability construction,
// source event dispatch, seed negotiation and queue lifecycle are native here.
class SceneCapabilityBackend : public EventQueueBackend {
  public:
    virtual std::string scene_name() const = 0;
    virtual std::optional<std::string> region_name() const = 0;
    virtual std::optional<std::string> external_host() const = 0;
    virtual std::shared_ptr<CapabilityRuntimeListener> listener() const = 0;
    virtual std::shared_ptr<CapabilityRuntimeListener> main_server() const {
        return listener();
    }
    virtual bool check_client(framework::Uuid agent, const network::HttpRequest& request) = 0;
    virtual std::uint32_t region_size_x() const {
        return 256;
    }
    virtual std::uint32_t region_size_y() const {
        return 256;
    }
    virtual bool selected_for_console() const {
        return true;
    }
    virtual bool console_has_selected_scene() const {
        return false;
    }
    virtual std::optional<std::string> presence_name(framework::Uuid) {
        return {};
    }
    virtual void info(const std::string& text) {
        debug(text);
    }
    virtual void event_error(const std::string& prefix, const std::exception& exception) {
        error(prefix + exception.what());
    }
};
class SceneCapabilities {
  public:
    explicit SceneCapabilities(std::shared_ptr<SceneCapabilityBackend> backend);
    ~SceneCapabilities();
    SceneCapabilities(const SceneCapabilities&) = delete;
    SceneCapabilities& operator=(const SceneCapabilities&) = delete;
    CapabilitiesModule& capabilities() {
        return *capabilities_;
    }
    std::shared_ptr<EventQueueModule> event_queue() const {
        return queue_;
    }
    SceneCapabilityEvents& events();
    std::shared_ptr<SceneCapabilityEvents> shared_events() const;
    std::shared_ptr<EventQueueScene> event_scene() const {
        return queue_scene_;
    }
    void client_closed(framework::Uuid agent);
    // Native owner cleanup; full Scene.RemoveClient does substantially more.
    void shutdown();

  private:
    struct State;
    class CapsScene;
    class QueueScene;
    std::shared_ptr<State> state_;
    std::shared_ptr<CapsScene> caps_scene_;
    std::shared_ptr<EventQueueScene> queue_scene_;
    std::unique_ptr<CapabilitiesModule> capabilities_;
    std::shared_ptr<EventQueueModule> queue_;
    std::atomic<bool> stopped_{false};
};
}
