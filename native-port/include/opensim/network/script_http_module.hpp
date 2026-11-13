// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/script_http_transport.hpp"
#include "opensim/script/http_request_throttle.hpp"
#include <condition_variable>
#include <deque>
#include <thread>

namespace opensim::network {

struct ScriptHttpOption {
    std::optional<std::int32_t> integer;
    std::string text;
};

struct ScriptHttpSubmission {
    std::string key;
    std::vector<std::string> errors;
    bool invalid_flag = false;
};

struct ScriptHttpCompletion {
    framework::Uuid part, request;
    std::uint32_t local_id;
    ScriptHttpReply reply;
    framework::Uuid item;
};

class ScriptHttpService {
public:
    ScriptHttpService(std::shared_ptr<const framework::OutboundUrlFilter> filter,
                      int max_body_bytes = 16384, int timeout_ms = 30000, int worker_count = 8,
                      std::shared_ptr<framework::ConfigXmlProxy> proxy = {});
    ~ScriptHttpService();
    std::size_t active_workers() const;

private:
    friend class ScriptHttpModule;
    struct Job;
    struct RegionState;
    struct Worker {
        std::jthread thread;
        bool running = false;
    };
    void enqueue(const std::shared_ptr<Job>& job);
    void cancel(const RegionState& region, std::optional<framework::Uuid> item = {});
    void queue_locked(const std::shared_ptr<Job>& job);
    void work(Worker& worker, std::stop_token stop);

    std::shared_ptr<const framework::OutboundUrlFilter> filter_;
    std::shared_ptr<framework::ConfigUriObjectBackend> uris_;
    ScriptHttpTransport transport_;
    int max_body_bytes_;
    mutable std::mutex mutex_;
    std::condition_variable_any wake_;
    std::deque<std::shared_ptr<Job>> waiting_;
    std::unordered_map<framework::Uuid, std::shared_ptr<Job>, framework::UuidHash> pending_;
    std::size_t active_workers_ = 0;
    std::vector<Worker> workers_;
};

class ScriptHttpModule {
public:
    explicit ScriptHttpModule(std::shared_ptr<ScriptHttpService> service,
                              script::HttpRequestThrottleSettings throttle = {});
    ~ScriptHttpModule();
    ScriptHttpSubmission submit(framework::Uuid part, std::uint32_t local_id, framework::Uuid owner,
                                framework::Uuid item, std::string url, const std::vector<ScriptHttpOption>& options,
                                std::string body,
                                const std::function<void(ScriptHttpRequest&)>& add_scene_headers);
    void stop(std::uint32_t local_id, framework::Uuid item);
    void stop_all();
    std::deque<ScriptHttpCompletion> take_completed();

private:
    std::shared_ptr<ScriptHttpService> service_;
    std::shared_ptr<ScriptHttpService::RegionState> state_;
    script::HttpRequestThrottle throttle_;
};

}
