// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/caps.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/ip_address.hpp"
#include "opensim/services/inventory_store.hpp"
#include <deque>
namespace opensim::services {
class EventQueueModule;
struct EventGroupMember {framework::Uuid agent;bool voice=false,moderator=false,muted_text=false,enter=false;EventGroupMember()=default;explicit EventGroupMember(framework::Uuid id,bool transition=true):agent(id),enter(transition){}EventGroupMember(framework::Uuid id,bool cv,bool mod,bool muted,bool transition):agent(id),voice(cv),moderator(mod),muted_text(muted),enter(transition){}};
struct EventGroupMembership {framework::Uuid group,picture;std::optional<std::string> name;std::uint64_t powers=0;bool notices=true,profile=false;std::int32_t contribution=0;};
struct EventPlace {framework::Uuid owner,snapshot;std::optional<std::string> name,description,sim_name;std::int32_t actual_area=0,billable_area=0,price=0;std::uint8_t flags=0;std::uint32_t x=0,y=0,z=0,dwell=0;};
class EventQueueCaps {
public:
    virtual ~EventQueueCaps()=default;
    virtual std::string describe()const{return "OpenSim.Framework.Capabilities.Caps";}
    virtual void register_poll(const std::string& name,std::shared_ptr<CapabilityPollHandler> handler)=0;
};
std::shared_ptr<EventQueueCaps> event_queue_caps(std::shared_ptr<Caps> caps);
class EventQueueScene {
public:
    using Closed=std::function<void(framework::Uuid,std::shared_ptr<EventQueueScene>)>;
    using Register=std::function<void(framework::Uuid,std::shared_ptr<EventQueueCaps>)>;
    virtual ~EventQueueScene()=default;
    virtual std::string name()const=0;
    virtual std::string region_name()const=0;
    virtual void register_module(EventQueueModule* module)=0;
    virtual void unregister_module(EventQueueModule* module)=0;
    virtual void subscribe_closed(EventQueueModule* owner,Closed callback)=0;
    virtual void unsubscribe_closed(EventQueueModule* owner)=0;
    virtual void subscribe_register(EventQueueModule* owner,Register callback)=0;
    virtual void unsubscribe_register(EventQueueModule* owner)=0;
};
class EventQueueUtf8 {
public:
    virtual ~EventQueueUtf8()=default;
    virtual server::PollBytes to_array()const=0;
};
class EventQueueByteBuffer:public EventQueueUtf8 {
public:
    std::string text;
    server::PollBytes to_array()const override{return std::make_shared<std::vector<std::uint8_t>>(text.begin(),text.end());}
};
class EventQueueNullReference:public std::runtime_error {
public:
    EventQueueNullReference():std::runtime_error("Object reference not set to an instance of an object."){}
};
std::string event_queue_inner_xml(const network::Value& value);
class EventQueueBackend {
public:
    virtual ~EventQueueBackend()=default;
    virtual framework::Uuid random_uuid();
    virtual std::int32_t random_next(std::int32_t maximum);
    virtual void debug(const std::string& text)=0;
    virtual void warn(const std::string& text)=0;
    virtual void error(const std::string& text)=0;
    virtual void console(const std::string& text)=0;
    virtual void add_command(CapabilityCommand command)=0;
    virtual server::PollBytes serialize_inner(std::shared_ptr<network::Value> value);
};
class EventQueueModule:public std::enable_shared_from_this<EventQueueModule> {
public:
    explicit EventQueueModule(EventQueueBackend& backend):backend_(backend){}
    explicit EventQueueModule(std::shared_ptr<EventQueueBackend> backend):backend_owner_(std::move(backend)),backend_(backend_owner_?*backend_owner_:throw std::invalid_argument("Null event queue backend")){}
    virtual ~EventQueueModule()=default;
    static constexpr std::string_view name="EventQueueGetModule";
    virtual std::string module_name()const{return std::string(name);}
    std::optional<std::string> replaceable_interface()const{return {};}
    framework::AtomicInt32Property debug_level;
    virtual void initialise(const server::IniConfig*){}
    void add_region(std::shared_ptr<EventQueueScene> scene);
    void remove_region(std::shared_ptr<EventQueueScene> scene);
    void region_loaded(std::shared_ptr<EventQueueScene>){}
    virtual void close(){}
    void handle_debug(const std::vector<std::optional<std::string>>* args);
    void handle_show();
    bool enqueue(server::PollBytes bytes,framework::Uuid agent);
    bool enqueue_osd(std::shared_ptr<network::Value> data,framework::Uuid agent);
    bool enqueue_utf8(std::shared_ptr<EventQueueUtf8> data,framework::Uuid agent);
    void client_closed(framework::Uuid agent,std::shared_ptr<EventQueueScene> scene);
    static std::string generate_path(framework::Uuid id){return "/CE/"+id.str();}
    void on_register_caps(framework::Uuid agent,std::shared_ptr<EventQueueCaps> caps);
    bool has_events(framework::Uuid request,framework::Uuid agent);
    server::PollTablePtr get_events(framework::Uuid request,framework::Uuid agent);
    server::PollTablePtr no_events(framework::Uuid request,framework::Uuid agent);
    static server::PollTablePtr no_agent();
    void drop(framework::Uuid,framework::Uuid){}
    static std::shared_ptr<EventQueueByteBuffer> start_event(const std::optional<std::string>& name,int capacity=0);
    static server::PollBytes end_event(std::shared_ptr<EventQueueByteBuffer> buffer);
    static server::PollBytes build_event(const std::optional<std::string>& name,std::shared_ptr<network::Value> body);
    static std::string keep_alive_event();
    virtual void enable_simulator(std::uint64_t handle,std::shared_ptr<framework::IpEndpoint> endpoint,framework::Uuid agent,int x,int y);
    virtual void establish_communication(framework::Uuid agent,std::shared_ptr<framework::IpEndpoint> endpoint,const std::optional<std::string>& seed,std::uint64_t handle,int x,int y);
    virtual void teleport_finish(std::uint64_t handle,std::uint8_t access,std::shared_ptr<framework::IpEndpoint> endpoint,std::uint32_t location,std::uint32_t flags,const std::optional<std::string>& seed,framework::Uuid agent,int x,int y);
    virtual void cross_region(std::uint64_t handle,framework::Vector3 position,framework::Vector3 look,std::shared_ptr<framework::IpEndpoint> endpoint,const std::optional<std::string>& seed,framework::Uuid agent,framework::Uuid session,int x,int y);
    void chatterbox_invitation(framework::Uuid session,const std::optional<std::string>& session_name,framework::Uuid from,const std::optional<std::string>& message,framework::Uuid to,const std::optional<std::string>& from_name,std::uint8_t dialog,std::uint32_t timestamp,bool offline,int estate,framework::Vector3 position,std::uint32_t ttl,framework::Uuid transaction,bool group,server::PollBytes bucket);
    void chatterbox_updates(framework::Uuid session,framework::Uuid to,const std::vector<EventGroupMember>* updates);
    void chatterbox_start(framework::Uuid session,const std::optional<std::string>& name,int type,bool voice,bool moderated,framework::Uuid temporary,bool success,const std::optional<std::string>& error,framework::Uuid to);
    void chatterbox_close(framework::Uuid to,framework::Uuid session,const std::optional<std::string>& reason);
    void group_membership(framework::Uuid agent,const std::vector<std::shared_ptr<EventGroupMembership>>* groups);
    void places_reply(framework::Uuid agent,framework::Uuid query,framework::Uuid transaction,const std::vector<std::shared_ptr<EventPlace>>* places);
    void script_running(framework::Uuid object,framework::Uuid item,bool running,framework::Uuid agent);
    void physics_properties(std::uint32_t local,std::uint8_t shape,float density,float friction,float bounce,float gravity,framework::Uuid agent);
    void windlight_refresh(int interpolate,framework::Uuid agent);
    void bulk_inventory(const InventoryItem* item,framework::Uuid agent,std::optional<framework::Uuid> transaction={});
    void large_message(framework::Uuid agent,std::optional<framework::Uuid> transaction,std::optional<framework::Uuid> session,const std::optional<std::string>& method,framework::Uuid invoice,const std::vector<std::optional<std::string>>* messages);
    void large_message_bytes(framework::Uuid agent,std::optional<framework::Uuid> transaction,std::optional<framework::Uuid> session,const std::optional<std::string>& method,framework::Uuid invoice,const std::vector<server::PollBytes>* messages);
private:
    friend struct EventQueueProbe;
    struct Queue {
        std::recursive_mutex monitor;
        mutable std::mutex storage_mutex;
        std::deque<server::PollBytes> values;
        std::size_t count()const{std::lock_guard lock(storage_mutex);return values.size();}
        void enqueue(server::PollBytes value){std::lock_guard lock(storage_mutex);values.push_back(std::move(value));}
        server::PollBytes dequeue(){std::lock_guard lock(storage_mutex);if(values.empty())throw std::out_of_range("Empty event queue");auto value=values.front();values.pop_front();return value;}
    };
    std::shared_ptr<Queue> try_get_queue(framework::Uuid agent);
    std::shared_ptr<Queue> get_queue(framework::Uuid agent)const;
    std::shared_ptr<EventQueueScene> scene()const;
    std::shared_ptr<EventQueueBackend> backend_owner_;
    EventQueueBackend& backend_;
    mutable std::recursive_mutex scene_mutex_,queues_mutex_,ids_mutex_,paths_mutex_;
    std::shared_ptr<EventQueueScene> scene_;
    framework::OrderedDictionary<std::string,std::shared_ptr<Queue>> queues_;
    framework::OrderedDictionary<std::string,std::int32_t> ids_;
    framework::OrderedDictionary<std::string,framework::Uuid> paths_;
};
}
