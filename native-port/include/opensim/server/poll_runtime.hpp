// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/poll_manager.hpp"
namespace opensim::server {
// Standard C++ worker/watcher host. The backend must outlive its manager.
class PollRuntimeBackend final:public PollManagerBackend {
public:
    using Error=std::function<void(const std::exception&)>;
    explicit PollRuntimeBackend(Error error,std::function<void()> watchdog={});
    void sleep(std::int32_t milliseconds)override;
    void watchdog_update()override;
    void error(const std::exception& error)override;
    std::shared_ptr<PollJobEngine> make_pool(Worker worker,const std::string& name,std::int32_t hold_ms,std::int32_t concurrency)override;
    std::shared_ptr<PollWatcher> start_watcher(std::function<void()> watcher)override;
private:
    Error error_;
    std::function<void()> watchdog_;
};
}
