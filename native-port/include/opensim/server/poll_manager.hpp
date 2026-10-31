// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/poll_request.hpp"
#include <atomic>
#include <deque>
#include <mutex>
namespace opensim::server {
using PollRequestPtr=std::shared_ptr<PollServiceHttpRequest>;
class PollJobEngine {
public:
    virtual ~PollJobEngine()=default;
    virtual void enqueue(PollRequestPtr request)=0;
    virtual std::size_t count()const=0;
    virtual void dispose()=0;
    virtual void cancel(){}
    virtual void join(){}
};
class PollWatcher {
public:
    virtual ~PollWatcher()=default;
    // Native lifetime fence only; source Stop does not explicitly join.
    virtual void join()=0;
};
class PollManagerBackend {
public:
    using Worker=std::function<void(PollRequestPtr)>;
    virtual ~PollManagerBackend()=default;
    virtual std::int32_t tick_count()const;
    virtual void sleep(std::int32_t milliseconds)=0;
    virtual void watchdog_update()=0;
    virtual void error(const std::exception& error)=0;
    virtual std::shared_ptr<PollJobEngine> make_pool(Worker worker,const std::string& name,std::int32_t hold_ms,std::int32_t concurrency)=0;
    virtual std::shared_ptr<PollWatcher> start_watcher(std::function<void()> watcher)=0;
};
class PollServiceRequestManager {
public:
    PollServiceRequestManager(bool perform_responses_async,std::uint32_t worker_count,std::int32_t timeout,PollManagerBackend& backend);
    ~PollServiceRequestManager();
    PollServiceRequestManager(const PollServiceRequestManager&)=delete;
    PollServiceRequestManager& operator=(const PollServiceRequestManager&)=delete;
    void start();
    void enqueue(PollRequestPtr request);
    void stop();
    // Worker and watcher entry points also permit deterministic host scheduling.
    void pool_worker_job(PollRequestPtr request);
    void check_retries();
    bool running()const noexcept{return running_.load(std::memory_order_relaxed);}
    std::size_t retry_count()const;
private:
    void requeue(PollRequestPtr request);
    bool take_retry(PollRequestPtr& request);
    std::shared_ptr<PollJobEngine> pool()const;
    PollManagerBackend& backend_;
    const std::int32_t worker_count_;
    std::atomic<bool> running_{false};
    std::atomic<std::shared_ptr<PollJobEngine>> worker_pool_;
    std::vector<std::shared_ptr<PollWatcher>> watchers_;
    std::vector<std::shared_ptr<PollJobEngine>> retired_pools_;
    mutable std::mutex retry_mutex_;
    std::deque<PollRequestPtr> retries_;
};
}
