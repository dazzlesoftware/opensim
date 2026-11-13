// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/atomic_shared_ptr.hpp"
#include "opensim/services/scene_capability_events.hpp"
#include "opensim/server/get_assets.hpp"
#include "opensim/framework/object_job_engine.hpp"
#include <unordered_set>
namespace opensim::services {
class AssetsUserManagement {
  public:
    virtual ~AssetsUserManagement() = default;
    virtual std::optional<std::string> user_server_url(framework::Uuid agent, const std::string &key) = 0;
};
class AssetsExternalCaps {
  public:
    virtual ~AssetsExternalCaps() = default;
    virtual void register_user(framework::Uuid agent, std::shared_ptr<Caps> caps, const std::string &name,
                               const std::string &path) = 0;
};
class AssetsModuleScene {
  public:
    virtual ~AssetsModuleScene() = default;
    virtual SceneCapabilityEvents &events() = 0;
    virtual bool shutting_down() const = 0;
    virtual std::shared_ptr<server::AssetRetrievalService> asset_service() = 0;
    virtual std::shared_ptr<AssetsUserManagement> user_management() = 0;
    virtual std::shared_ptr<AssetsExternalCaps> external_caps() = 0;
};
class AssetsModuleBackend : public std::enable_shared_from_this<AssetsModuleBackend> {
  public:
    virtual ~AssetsModuleBackend() = default;
    virtual framework::Uuid random_uuid() {
        return framework::Uuid::random();
    }
    virtual void warn(const std::string &text) = 0;
    virtual void job_error(const std::string &name, const std::exception &error) = 0;
    virtual std::shared_ptr<framework::ObjectJobQueue>
    make_worker(framework::ObjectJobEngine::Callback callback, const std::string &name, int hold,
                int concurrency);
    virtual server::PollResponsePtr make_response(const network::HttpRequest &request);
    virtual std::shared_ptr<server::HttpResponseState>
    response_state(const server::PollResponsePtr &response);
};
struct AssetsModuleState {
    std::recursive_mutex load_mutex;
    opensim::framework::AtomicSharedPtr<server::AssetRetrievalService> service;
    opensim::framework::AtomicSharedPtr<server::GetAssetsHandler> handler;
    opensim::framework::AtomicSharedPtr<framework::ObjectJobQueue> worker;
    framework::AtomicInt32Property scenes;
};
class AssetPollHandler;
struct AssetPollRequest {
    std::shared_ptr<AssetPollHandler> poll;
    framework::Uuid request_id;
    network::HttpRequest request;
};
class AssetPollHandler : public CapabilityPollHandler, public std::enable_shared_from_this<AssetPollHandler> {
  public:
    static std::shared_ptr<AssetPollHandler> create(std::string path, framework::Uuid agent,
                                                    std::shared_ptr<AssetsModuleScene> scene,
                                                    std::optional<std::string> foreign,
                                                    std::shared_ptr<AssetsModuleState> state,
                                                    std::shared_ptr<AssetsModuleBackend> backend);
    bool has(framework::Uuid request, framework::Uuid agent);
    void dropped(framework::Uuid request, framework::Uuid agent);
    server::PollTablePtr events(framework::Uuid request, framework::Uuid agent);
    static server::PollTablePtr timeout(framework::Uuid request, framework::Uuid agent);
    server::PollResponsePtr receive(framework::Uuid request, const network::HttpRequest &native);
    void process(std::shared_ptr<AssetPollRequest> request);

  private:
    friend struct GetAssetsModuleProbe;
    std::shared_ptr<AssetsModuleScene> scene_;
    std::optional<std::string> foreign_;
    std::shared_ptr<AssetsModuleState> state_;
    std::shared_ptr<AssetsModuleBackend> backend_;
    std::recursive_mutex responses_mutex_, dropped_mutex_;
    framework::OrderedDictionary<std::string, server::PollResponsePtr> responses_;
    std::unordered_set<std::string> dropped_;
};
class GetAssetsModule : public std::enable_shared_from_this<GetAssetsModule> {
  public:
    explicit GetAssetsModule(std::shared_ptr<AssetsModuleBackend> backend,
                             std::shared_ptr<AssetsModuleState> state = {});
    static constexpr std::string_view name = "GetAssetsModule";
    std::nullptr_t replaceable_interface() const {
        return nullptr;
    }
    void initialise(const server::IniConfig *source);
    void add_region(std::shared_ptr<AssetsModuleScene> scene);
    void remove_region(std::shared_ptr<AssetsModuleScene> scene);
    void region_loaded(std::shared_ptr<AssetsModuleScene> scene);
    void close();
    void register_caps(framework::Uuid agent, std::shared_ptr<Caps> caps);
    static void do_requests(std::any job, const std::shared_ptr<AssetsModuleState> &state);

  private:
    friend struct GetAssetsModuleProbe;
    std::shared_ptr<AssetsModuleBackend> backend_;
    std::shared_ptr<AssetsModuleState> state_;
    opensim::framework::AtomicSharedPtr<AssetsModuleScene> scene_;
    opensim::framework::AtomicSharedPtr<AssetsUserManagement> users_;
    bool enabled_ = false;
    std::array<std::optional<std::string>, 4> urls_;
};
}
