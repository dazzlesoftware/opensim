// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/poll_manager.hpp"
#include <bit>
#include <chrono>
namespace opensim::server {
namespace {
template<class T>T& require(const std::shared_ptr<T>& value){if(!value)throw std::invalid_argument("Null poll manager dependency");return *value;}
}
std::int32_t PollManagerBackend::tick_count()const{const auto ticks=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(ticks));}
PollServiceRequestManager::PollServiceRequestManager(bool,std::uint32_t workers,std::int32_t,PollManagerBackend& backend):backend_(backend),worker_count_(std::bit_cast<std::int32_t>(workers)){}
PollServiceRequestManager::~PollServiceRequestManager(){try{stop();}catch(...){}for(const auto& watcher:watchers_)if(watcher)try{watcher->join();}catch(...){}for(const auto& retired:retired_pools_)if(retired)try{retired->join();}catch(...){}auto current=pool();if(current)try{current->dispose();current->join();}catch(...){} }
std::shared_ptr<PollJobEngine> PollServiceRequestManager::pool()const{return worker_pool_.load(std::memory_order_relaxed);}
void PollServiceRequestManager::start(){
    if(running())return;
    running_.store(true,std::memory_order_relaxed);
    worker_pool_.store(backend_.make_pool([this](PollRequestPtr request){pool_worker_job(std::move(request));},"PollServiceWorker",4000,worker_count_),std::memory_order_relaxed);
    // Retain watcher lifetimes without inserting a join into source Start/Stop.
    watchers_.push_back(backend_.start_watcher([this]{check_retries();}));
}
void PollServiceRequestManager::enqueue(PollRequestPtr request){if(running())require(pool()).enqueue(std::move(request));}
void PollServiceRequestManager::requeue(PollRequestPtr request){if(running()){std::lock_guard lock(retry_mutex_);retries_.push_back(std::move(request));}}
bool PollServiceRequestManager::take_retry(PollRequestPtr& request){std::lock_guard lock(retry_mutex_);if(retries_.empty())return false;request=std::move(retries_.front());retries_.pop_front();return true;}
std::size_t PollServiceRequestManager::retry_count()const{std::lock_guard lock(retry_mutex_);return retries_.size();}
void PollServiceRequestManager::check_retries(){
    while(running()){
        backend_.sleep(100);
        backend_.watchdog_update();
        PollRequestPtr request;
        while(running()&&take_retry(request))require(pool()).enqueue(std::move(request));
    }
}
void PollServiceRequestManager::stop(){
    if(!running())return;
    running_.store(false,std::memory_order_relaxed);
    backend_.sleep(100);
    try{PollRequestPtr request;while(take_retry(request))require(request).do_http_stop();}catch(...){}
    std::int32_t count=10;
    while(--count>0&&require(pool()).count()>0)backend_.sleep(100);
    auto disposed=pool();
    require(disposed).dispose();
    retired_pools_.push_back(disposed);
    worker_pool_.store({},std::memory_order_relaxed);
}
void PollServiceRequestManager::pool_worker_job(PollRequestPtr request){
    if(!request)return;
    try{
        auto context=require(request->request).context();
        if(!require(context).can_send()){
            auto& args=require(request->poll_args);
            auto callback=args.drop;
            const auto id=args.id;
            callback(request->request_id,id);
            return;
        }
        if(!running()){request->do_http_stop();return;}
        context=require(request->request).context();
        if(require(context).is_sending()){requeue(request);return;}
        auto& args=require(request->poll_args);
        auto has=args.has_events;
        auto id=args.id;
        if(has(request->request_id,id)){
            try{auto get=args.get_events;id=args.id;auto data=get(request->request_id,id);request->do_http_grunt_work(data);}catch(...){}
        }else{
            const auto elapsed=std::bit_cast<std::int32_t>(std::bit_cast<std::uint32_t>(backend_.tick_count())-std::bit_cast<std::uint32_t>(request->request_time));
            if(elapsed>args.timeout_ms){try{auto no=args.no_events;id=args.id;request->do_http_grunt_work(no(request->request_id,id));}catch(...){} }
            else requeue(request);
        }
    }catch(const std::exception& error){backend_.error(error);}
}
}
