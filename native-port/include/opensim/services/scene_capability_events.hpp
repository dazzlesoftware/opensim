// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/event_queue.hpp"
#include <algorithm>
namespace opensim::services {
// Explicit delegate identity supports source multicast -= (last matching
// invocation subsequence). Invocation snapshots permit subscription/reentry
// during dispatch without changing the current invocation list.
struct SceneDelegateIdentity {const void* target=nullptr;std::string method;bool operator==(const SceneDelegateIdentity&)const=default;};
template<class... Args> class SceneCapabilityEvent {
public:
    struct Handler {SceneDelegateIdentity identity;std::function<void(Args...)> invoke;};
    void add(Handler handler){if(!handler.invoke)return;std::lock_guard lock(mutex_);handlers_.push_back(std::move(handler));}
    void add(const std::vector<Handler>& handlers){std::lock_guard lock(mutex_);for(auto& h:handlers)if(h.invoke)handlers_.push_back(h);}
    void remove(const SceneDelegateIdentity& identity){remove(std::vector<SceneDelegateIdentity>{identity});}
    void remove(const std::vector<SceneDelegateIdentity>& identities){if(identities.empty())return;std::lock_guard lock(mutex_);if(identities.size()>handlers_.size())return;for(size_t i=handlers_.size()-identities.size()+1;i-->0;){bool matches=true;for(size_t j=0;j<identities.size();++j)if(handlers_[i+j].identity!=identities[j]){matches=false;break;}if(matches){handlers_.erase(handlers_.begin()+static_cast<std::ptrdiff_t>(i),handlers_.begin()+static_cast<std::ptrdiff_t>(i+identities.size()));return;}}}
    std::vector<Handler> snapshot()const{std::lock_guard lock(mutex_);return handlers_;}
private:
    mutable std::mutex mutex_;std::vector<Handler> handlers_;
};
class SceneCapabilityEvents {
public:
    using CapsEvent=SceneCapabilityEvent<framework::Uuid,std::shared_ptr<Caps>>;
    using ClosedEvent=SceneCapabilityEvent<framework::Uuid,std::shared_ptr<EventQueueScene>>;
    using Error=std::function<void(const std::string&,const std::exception&)>;
    explicit SceneCapabilityEvents(Error error):error_(std::move(error)){}
    CapsEvent on_register_caps,on_deregister_caps;
    ClosedEvent on_client_closed;
    void trigger_register(framework::Uuid agent,std::shared_ptr<Caps> caps){trigger(on_register_caps,"TriggerOnRegisterCaps",agent,std::move(caps));}
    void trigger_deregister(framework::Uuid agent,std::shared_ptr<Caps> caps){trigger(on_deregister_caps,"TriggerOnDeregisterCaps",agent,std::move(caps));}
    void trigger_closed(framework::Uuid agent,std::shared_ptr<EventQueueScene> scene){trigger(on_client_closed,"TriggerClientClosed",agent,std::move(scene));}
private:
    template<class E,class... Args>void trigger(E& event,const char* name,Args... args){for(auto& handler:event.snapshot()){try{handler.invoke(args...);}catch(const std::exception& exception){if(error_)error_(std::string("[EVENT MANAGER]: Delegate for ")+name+" failed - continuing.  ",exception);}}}
    Error error_;
};
}
