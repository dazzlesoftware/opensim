// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_caps.hpp"
#include <span>
namespace opensim::services {
class TaskScriptUpload;
struct ScriptTaskCapsBackend {
    std::function<bool(framework::Uuid)> object_exists;
    std::function<bool(framework::Uuid, framework::Uuid)> can_edit_inventory;
    std::function<bool(framework::Uuid, framework::Uuid, framework::Uuid)> can_edit_script;
    // Performs the real Scene.CapsUpdateTaskInventoryScriptAsset transaction:
    // permissions recheck, asset store, old instance removal, inventory update,
    // compilation/restart and notifications. Returned strings are actual errors.
    std::function<std::vector<std::string>(framework::Uuid, framework::Uuid, framework::Uuid, bool,
                                           std::span<const std::uint8_t>)>
        update;
    std::function<std::string()> new_path;
    std::function<void(std::shared_ptr<TaskScriptUpload>)> register_upload;
    std::function<void(const std::string&)> remove_upload;
    // Host timer owns scheduling; arm(path,30000), cancel(path,0).
    // Expiry calls TaskScriptUpload::expire(). Cancellation must not join the
    // calling timer thread. Bind callbacks weakly to the host registration state.
    std::function<void(const std::string&, int)> timer;
    std::function<void(std::span<const std::uint8_t>)> dump;
    std::function<void(std::string)> log;
};
class TaskScriptUpload final : public server::CapabilitySimpleHandler {
public:
    TaskScriptUpload(framework::Uuid agent, framework::Uuid item, framework::Uuid object, bool running,
                     std::string path, std::string remote_ip, ScriptTaskCapsBackend backend);
    ~TaskScriptUpload();
    std::optional<std::string> path() const override {
        return path_;
    }
    network::HttpResponse handle(const network::HttpRequest&) override;
    void expire();
    bool active() const;

private:
    bool stop();
    framework::Uuid agent_, item_, object_;
    bool running_;
    std::string path_, remote_ip_;
    ScriptTaskCapsBackend backend_;
    mutable std::mutex mutex_;
    bool active_ = true;
};
class UpdateScriptTaskCore {
public:
    // origin is source protocol://HostName:Port, without a trailing slash.
    UpdateScriptTaskCore(framework::Uuid agent, std::string origin, ScriptTaskCapsBackend);
    network::HttpResponse handle(const network::HttpRequest&, const network::Value& map) const;

private:
    framework::Uuid agent_;
    std::string origin_;
    ScriptTaskCapsBackend backend_;
};
class UpdateScriptTaskRoute final : public server::CapabilitySimpleHandler {
public:
    using Deserialize = FetchInventory2Route::Deserialize;
    UpdateScriptTaskRoute(std::string path, std::shared_ptr<UpdateScriptTaskCore>,
                          Deserialize deserialize = {});
    std::optional<std::string> path() const override {
        return path_;
    }
    network::HttpResponse handle(const network::HttpRequest&) override;
    std::atomic<std::uint32_t> requests_received = 0, requests_handled = 0;

private:
    std::string path_;
    std::shared_ptr<UpdateScriptTaskCore> core_;
    Deserialize deserialize_;
};
}
