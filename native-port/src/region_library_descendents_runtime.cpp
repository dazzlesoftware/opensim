// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/region_library_descendents_runtime.hpp"
#include "opensim/framework/object_job_engine.hpp"
#include "opensim/server/poll_http_service.hpp"
#include <bit>
#include <chrono>
#include <condition_variable>
#include <map>
#include <set>
#include <thread>
namespace opensim::services {
struct RegionLibraryDescendentsRuntime::State:std::enable_shared_from_this<State> {
 using Clock=std::chrono::steady_clock;
 struct Poll;
 struct Job{std::shared_ptr<Poll> poll;framework::Uuid id;network::HttpRequest request;};
 struct Poll:CapabilityPollHandler,std::enable_shared_from_this<Poll> {
  std::shared_ptr<State> state;std::mutex mutex;std::map<framework::Uuid,server::PollTablePtr> responses;std::set<framework::Uuid> dropped;
  static std::shared_ptr<Poll> create(std::shared_ptr<State> state,framework::Uuid agent){
   auto p=std::make_shared<Poll>();p->state=std::move(state);p->id=agent;p->url="/"+framework::Uuid::random().str();p->timeout_ms=INT32_MAX;std::weak_ptr<Poll> weak=p;
   p->has_events=[weak](auto id,auto){auto self=weak.lock();if(!self)return false;std::lock_guard lock(self->mutex);return self->responses.contains(id);};
   p->drop=[weak](auto id,auto){if(auto self=weak.lock()){std::lock_guard lock(self->mutex);self->responses.erase(id);self->dropped.insert(id);}};
   p->get_events=[weak](auto id,auto){auto self=weak.lock();if(!self)throw std::invalid_argument("Expired inventory poll");std::lock_guard lock(self->mutex);auto result=self->responses.at(id);self->responses.erase(id);return result;};
   p->no_events=[](auto,auto){auto table=std::make_shared<server::PollTable>();table->set("int_response_code",int32_t(500));table->set("str_response_string",std::string("Script timeout"));table->set("content_type",std::string("text/plain"));table->set("keepalive",false);return table;};
   p->request=[weak](auto id,const auto& request)->server::PollResponsePtr{auto self=weak.lock();if(!self)throw std::invalid_argument("Expired inventory poll");auto state=self->state;std::lock_guard lock(state->mutex);if(state->stopped||!state->worker)throw std::invalid_argument("Inventory worker stopped");state->worker->enqueue(Job{self,id,request});return {};};return p;
  }
  void process(Job& job){
   {std::lock_guard lock(state->mutex);if(state->stopped)return;}
   if(state->shutting_down&&state->shutting_down())return;
   {std::lock_guard lock(mutex);if(dropped.erase(job.id))return;}
   auto response=server::PollHttpService::response(job.request);auto model=server::PollHttpService::response_state(response);
   auto result=state->core->handle(job.request,state->bad.get(),id);model->status(result.status);
   // Source creates OSHttpResponse before invoking handler, whose default
   // content type/status remain unless the handler changes them.
   auto bytes=std::make_shared<std::vector<std::uint8_t>>(result.body.begin(),result.body.end());model->raw_start(0);model->raw_length(static_cast<int32_t>(bytes->size()));model->raw_buffer(bytes);
   {std::lock_guard lock(mutex);if(dropped.erase(job.id)){++state->processed;return;}auto table=std::make_shared<server::PollTable>();table->set("h",response);responses[job.id]=table;}
   ++state->processed;
  }
 };
 struct Alarm{std::mutex mutex;std::condition_variable changed;std::optional<Clock::time_point> deadline;bool stopped=false;};
 std::shared_ptr<Alarm> alarm=std::make_shared<Alarm>();
 mutable std::mutex mutex;std::mutex stop_mutex;
 std::shared_ptr<SceneCapabilityEvents> events;std::string url;
 std::shared_ptr<FetchLibraryDescendentsCore> core;std::shared_ptr<InventoryBadRequests> bad;
 std::shared_ptr<framework::ObjectJobEngine> worker;std::thread timer;
 std::function<bool()> shutting_down;std::function<void(std::string)> log;
 std::atomic<std::uint64_t> processed=0;bool stopped=false,subscribed=false;
 SceneDelegateIdentity identity()const{return {this,"FetchLibDesc.RegisterCaps"};}
 void error()noexcept{try{if(log)log("library-inventory-worker-error");}catch(...) {}}
 void register_caps(framework::Uuid agent,std::shared_ptr<Caps> caps){std::lock_guard lock(mutex);if(stopped)return;if(!caps)throw std::invalid_argument("Null inventory Caps");if(url=="localhost")caps->register_poll("FetchLibDescendents2",Poll::create(shared_from_this(),agent));else caps->register_external("FetchLibDescendents2",url);}
 void stop(){std::lock_guard serial(stop_mutex);std::shared_ptr<framework::ObjectJobEngine> pool;{std::lock_guard lock(mutex);if(stopped)return;stopped=true;if(subscribed){events->on_register_caps.remove(identity());subscribed=false;}pool=worker;}{std::lock_guard lock(alarm->mutex);alarm->stopped=true;alarm->deadline.reset();}alarm->changed.notify_all();if(timer.joinable())timer.join();if(pool){pool->dispose();while(pool->active_threads()!=0)std::this_thread::sleep_for(std::chrono::milliseconds(1));}std::lock_guard lock(mutex);worker.reset();}
 ~State(){try{stop();}catch(...) {}}
};
RegionLibraryDescendentsRuntime::RegionLibraryDescendentsRuntime(const server::IniConfig& config,std::shared_ptr<SceneCapabilityEvents> events,LibraryDescendentsBackend backend,std::function<bool()> shutting_down):state_(std::make_shared<State>()){
 if(!events)throw std::invalid_argument("Null inventory descendents events");state_->events=std::move(events);state_->url=config.get("ClientStack.LindenCaps","Cap_FetchLibDescendents2","");if(state_->url.empty())return;
 state_->log=backend.log;state_->core=std::make_shared<FetchLibraryDescendentsCore>(std::move(backend));state_->shutting_down=std::move(shutting_down);const auto origin=State::Clock::now();auto alarm=state_->alarm;state_->bad=std::make_shared<InventoryBadRequests>([origin]{return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(State::Clock::now()-origin).count()));},[alarm](auto event,int32_t delay){std::lock_guard lock(alarm->mutex);if(alarm->stopped)return;if(event==InventoryBadRequests::TimerEvent::Disarm)alarm->deadline.reset();else alarm->deadline=State::Clock::now()+std::chrono::milliseconds(delay);alarm->changed.notify_all();});
 std::weak_ptr<State> weak=state_;state_->worker=std::make_shared<framework::ObjectJobEngine>([weak](std::any value){if(auto state=weak.lock())try{auto job=std::any_cast<State::Job>(std::move(value));job.poll->process(job);}catch(...){state->error();}},"LibInventoryWorker",2000,2);
 // One-shot source timer starts only on first Add, rearming after Purge only
 // while keys remain. Never acquire the cache lock while holding Alarm's lock.
 auto bad=state_->bad;state_->timer=std::thread([alarm,bad]{std::unique_lock lock(alarm->mutex);while(!alarm->stopped){if(!alarm->deadline){alarm->changed.wait(lock,[&]{return alarm->stopped||alarm->deadline.has_value();});continue;}auto deadline=*alarm->deadline;if(alarm->changed.wait_until(lock,deadline,[&]{return alarm->stopped||!alarm->deadline||*alarm->deadline!=deadline;}))continue;alarm->deadline.reset();lock.unlock();bad->purge();lock.lock();}});
 state_->events->on_register_caps.add({state_->identity(),[weak](auto agent,auto caps){if(auto state=weak.lock())state->register_caps(agent,std::move(caps));}});state_->subscribed=true;
}
RegionLibraryDescendentsRuntime::~RegionLibraryDescendentsRuntime(){try{stop();}catch(...) {}}
void RegionLibraryDescendentsRuntime::stop(){state_->stop();}
std::uint64_t RegionLibraryDescendentsRuntime::processed_requests()const{return state_->processed.load();}
std::size_t RegionLibraryDescendentsRuntime::queued_requests()const{std::lock_guard lock(state_->mutex);return state_->worker?state_->worker->count():0;}
}
