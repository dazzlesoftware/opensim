// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/event_queue.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <algorithm>
#include <charconv>
#include <random>
namespace opensim::services {
namespace {
template<class T>std::shared_ptr<T> held(std::shared_ptr<T> value){if(!value)throw EventQueueNullReference();return value;}
int32_t add(int32_t a,int32_t b){return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a)+std::bit_cast<uint32_t>(b));}
int32_t negative(int32_t value){return std::bit_cast<int32_t>(0u-std::bit_cast<uint32_t>(value));}
bool parse_integer(const std::optional<std::string>& text,int32_t& value){if(!text)return false;auto trimmed=framework::trim_utf8(*text);if(trimmed.starts_with('+')){trimmed.erase(0,1);if(trimmed.starts_with('-')||trimmed.starts_with('+'))return false;}if(trimmed.empty())return false;auto [at,error]=std::from_chars(trimmed.data(),trimmed.data()+trimmed.size(),value);return error==std::errc{}&&at==trimmed.data()+trimmed.size();}
class CapsBinding final:public EventQueueCaps {
public:
    explicit CapsBinding(std::shared_ptr<Caps> value):caps_(std::move(value)){}
    void register_poll(const std::string& name,std::shared_ptr<CapabilityPollHandler> handler)override{held(caps_)->register_poll(name,std::move(handler));}
private:
    std::shared_ptr<Caps> caps_;
};
void escape(std::string& out,const std::string& text){for(char c:framework::utf8(framework::utf16_replacing(text))){switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '\"':out+="&quot;";break;case '\'':out+="&apos;";break;default:out+=c;}}}
void inner(std::string& out,const network::Value& input,unsigned depth){if(depth>512)throw std::out_of_range("Event OSD XML nesting");const auto& value=input.view();using K=network::Value::Kind;
    switch(value.kind){
    case K::Map:out+="<map>";for(const auto& key:value.ordered_keys()){out+="<key>"+key+"</key>";inner(out,value.map.at(key),depth+1);}out+="</map>";return;
    case K::Array:out+="<array>";for(const auto& item:value.array)inner(out,item,depth+1);out+="</array>";return;
    case K::Undefined:out+="<undef />";return;
    case K::String:out+="<string>";escape(out,network::osd_string(value));out+="</string>";return;
    case K::Binary:case K::Long:out+="<binary>"+network::binary_base64(network::osd_from_binary(network::osd_binary(value)))+"</binary>";return;
    default:break;
    }
    const char* tag=value.kind==K::Boolean?"boolean":value.kind==K::Integer?"integer":value.kind==K::Real?"real":value.kind==K::Uuid?"uuid":value.kind==K::Date?"date":"uri";
    out+='<';out+=tag;out+='>';out+=value.kind==K::Boolean?(value.boolean?"1":"0"):network::osd_string(value);out+="</";out+=tag;out+='>';
}
}
std::string event_queue_inner_xml(const network::Value& value){std::string out;inner(out,value,0);return out;}
std::shared_ptr<EventQueueCaps> event_queue_caps(std::shared_ptr<Caps> caps){if(!caps)return {};return std::make_shared<CapsBinding>(std::move(caps));}
framework::Uuid EventQueueBackend::random_uuid(){return framework::Uuid::random();}
int32_t EventQueueBackend::random_next(int32_t maximum){if(maximum<=0)throw std::out_of_range("Random upper bound");thread_local std::mt19937 engine(std::random_device{}());return std::uniform_int_distribution<int32_t>(0,maximum-1)(engine);}
server::PollBytes EventQueueBackend::serialize_inner(std::shared_ptr<network::Value> value){auto text=event_queue_inner_xml(*held(value));return std::make_shared<std::vector<uint8_t>>(text.begin(),text.end());}
std::shared_ptr<EventQueueScene> EventQueueModule::scene()const{std::lock_guard lock(scene_mutex_);return scene_;}
void EventQueueModule::add_region(std::shared_ptr<EventQueueScene> value){
    [[maybe_unused]]const auto lifetime=shared_from_this();{std::lock_guard lock(scene_mutex_);scene_=value;}
    held(value)->register_module(this);
    auto weak=weak_from_this();held(value)->subscribe_closed(this,[weak](framework::Uuid agent,std::shared_ptr<EventQueueScene> scene){if(auto self=weak.lock())self->client_closed(agent,std::move(scene));});
    held(value)->subscribe_register(this,[weak](framework::Uuid agent,std::shared_ptr<EventQueueCaps> caps){if(auto self=weak.lock())self->on_register_caps(agent,std::move(caps));});
    CapabilityCommand debug{"debug eq","debug eq [0|1|2]","Turn on event queue debugging\n  <= 0 - turns off all event queue logging\n  >= 1 - turns on event queue setup and outgoing event logging\n  >= 2 - turns on poll notification",{},[weak](const std::vector<std::string>& args){if(auto self=weak.lock()){std::vector<std::optional<std::string>> values;for(const auto& a:args)values.push_back(a);self->handle_debug(&values);}},"Debug",false};backend_.add_command(std::move(debug));
    CapabilityCommand show{"show eq","show eq","Show contents of event queues for logged in avatars.  Used for debugging.",{},[weak](const std::vector<std::string>&){if(auto self=weak.lock())self->handle_show();},"Debug",false};backend_.add_command(std::move(show));
}
void EventQueueModule::remove_region(std::shared_ptr<EventQueueScene> value){if(scene()!=value)return;held(value)->unsubscribe_closed(this);held(value)->unsubscribe_register(this);held(value)->unregister_module(this);std::lock_guard lock(scene_mutex_);scene_.reset();}
void EventQueueModule::handle_debug(const std::vector<std::optional<std::string>>* args){if(!args)throw EventQueueNullReference();int32_t level;if(args->size()!=3||!parse_integer((*args)[2],level)){backend_.console("Usage: debug eq [0|1|2]");return;}debug_level=level;auto region=held(scene())->region_name();backend_.console("Set event queue debug level to "+std::to_string(level)+" in "+region);}
void EventQueueModule::handle_show(){auto name=held(scene())->name();backend_.console("Events in Scene "+name+" agents queues :");std::lock_guard lock(queues_mutex_);for(const auto& [agent,queue]:queues_)backend_.console("    "+agent+"  "+std::to_string(held(queue)->count()));}
std::shared_ptr<EventQueueModule::Queue> EventQueueModule::try_get_queue(framework::Uuid agent){std::lock_guard lock(queues_mutex_);auto key=agent.str();if(queues_.contains(key))return queues_.at(key);if(int32_t(debug_level)>0){auto region=held(scene())->region_name();backend_.debug("[EVENTQUEUE]: Adding new queue for agent "+key+" in region "+region);}auto queue=std::make_shared<Queue>();queues_[key]=queue;return queue;}
std::shared_ptr<EventQueueModule::Queue> EventQueueModule::get_queue(framework::Uuid agent)const{std::lock_guard lock(queues_mutex_);auto key=agent.str();return queues_.contains(key)?queues_.at(key):nullptr;}
bool EventQueueModule::enqueue(server::PollBytes bytes,framework::Uuid agent){try{auto queue=get_queue(agent);if(queue){std::lock_guard lock(queue->monitor);queue->enqueue(std::move(bytes));}else{auto name=held(scene())->name();backend_.warn("[EVENTQUEUE]: (Enqueue) No queue found for agent "+agent.str()+" in region "+name);}}catch(const EventQueueNullReference& error){backend_.error(std::string("[EVENTQUEUE] Caught exception: ")+error.what());return false;}return true;}
bool EventQueueModule::enqueue_osd(std::shared_ptr<network::Value> value,framework::Uuid agent){try{auto queue=get_queue(agent);if(queue){auto bytes=backend_.serialize_inner(std::move(value));std::lock_guard lock(queue->monitor);queue->enqueue(std::move(bytes));}else{auto name=held(scene())->name();backend_.warn("[EVENTQUEUE]: (Enqueue) No queue found for agent "+agent.str()+" in region "+name);}}catch(const EventQueueNullReference& error){backend_.error(std::string("[EVENTQUEUE] Caught exception: ")+error.what());return false;}return true;}
bool EventQueueModule::enqueue_utf8(std::shared_ptr<EventQueueUtf8> value,framework::Uuid agent){try{auto queue=get_queue(agent);if(queue){std::lock_guard lock(queue->monitor);queue->enqueue(held(value)->to_array());}else{auto name=held(scene())->name();backend_.warn("[EVENTQUEUE]: (Enqueue) No queue found for agent "+agent.str()+" in region "+name);}}catch(const EventQueueNullReference& error){backend_.error(std::string("[EVENTQUEUE] Caught exception: ")+error.what());return false;}return true;}
void EventQueueModule::client_closed(framework::Uuid agent,std::shared_ptr<EventQueueScene>){std::lock_guard lock(queues_mutex_);auto key=agent.str();queues_.erase(key);{std::lock_guard paths(paths_mutex_);paths_.erase(key);}{std::lock_guard ids(ids_mutex_);ids_.erase(key);}}
void EventQueueModule::on_register_caps(framework::Uuid agent,std::shared_ptr<EventQueueCaps> caps){
    [[maybe_unused]]const auto lifetime=shared_from_this();const auto key=agent.str();if(int32_t(debug_level)>0){const auto description=caps?caps->describe():std::string{};auto name=held(scene())->name();backend_.debug("[EVENTQUEUE]: OnRegisterCaps: agentID "+key+" caps "+description+" region "+name);}
    framework::Uuid path;
    {std::lock_guard lock(queues_mutex_);auto queue=queues_.contains(key)?queues_.at(key):nullptr;
        if(!queue){queue=std::make_shared<Queue>();queues_[key]=queue;std::lock_guard paths(paths_mutex_);path=backend_.random_uuid();paths_[key]=path;std::lock_guard ids(ids_mutex_);if(ids_.contains(key))ids_[key]=add(ids_.at(key),1);else ids_[key]=backend_.random_next(30000000);}
        else{queue->enqueue(nullptr);std::lock_guard paths(paths_mutex_);if(paths_.contains(key)){path=paths_.at(key);backend_.debug("[EVENTQUEUE]: Found Existing UUID!");std::lock_guard ids(ids_mutex_);if(ids_.contains(key))ids_[key]=negative(ids_.at(key));else ids_[key]=negative(backend_.random_next(30000000));}
            else{path=backend_.random_uuid();paths_[key]=path;std::lock_guard ids(ids_mutex_);if(ids_.contains(key))ids_[key]=add(ids_.at(key),1);else ids_[key]=backend_.random_next(30000000);}}
    }
    auto self=shared_from_this();auto poll=std::make_shared<CapabilityPollHandler>(CapabilityPollHandler::RequestMethod{},generate_path(path),[self](auto request,auto agent){return self->has_events(request,agent);},[self](auto request,auto agent){return self->get_events(request,agent);},[self](auto request,auto agent){return self->no_events(request,agent);},[self](auto request,auto agent){self->drop(request,agent);},agent,58000);
    held(caps)->register_poll("EventQueueGet",poll);
}
bool EventQueueModule::has_events(framework::Uuid,framework::Uuid agent){auto queue=get_queue(agent);if(!queue)return true;std::lock_guard lock(queue->monitor);return queue->count()>0;}
server::PollTablePtr EventQueueModule::no_agent(){auto result=std::make_shared<server::PollTable>();result->set("int_response_code",int32_t(404));return result;}
server::PollTablePtr EventQueueModule::no_events(framework::Uuid,framework::Uuid agent){auto result=std::make_shared<server::PollTable>();result->set("int_response_code",int32_t(get_queue(agent)?502:404));return result;}
server::PollTablePtr EventQueueModule::get_events(framework::Uuid request,framework::Uuid agent){
    [[maybe_unused]]const auto lifetime=shared_from_this();auto key=agent.str();if(int32_t(debug_level)>=2){auto name=held(scene())->name();backend_.warn("POLLED FOR EQ MESSAGES BY "+key+" in "+name);}
    auto queue=get_queue(agent);if(!queue)return no_agent();server::PollBytes element;std::vector<server::PollBytes> elements;int32_t total=0,id=0;bool negative_id=false;
    {std::lock_guard lock(queue->monitor);if(queue->count()==0)return no_events(request,agent);{std::lock_guard ids(ids_mutex_);id=ids_.at(key);}if(id<0){negative_id=true;id=negative(id);}elements.reserve(queue->count()+2);
        static const auto header=[](){std::string text="<llsd><map><key>events</key><array>";return std::make_shared<std::vector<uint8_t>>(text.begin(),text.end());}();elements.push_back(header);
        while(queue->count()>0){element=queue->dequeue();if(!element)break;elements.push_back(element);total=add(total,static_cast<int32_t>(element->size()));}
    }
    {std::lock_guard ids(ids_mutex_);ids_[key]=!element&&negative_id?backend_.random_next(30000000):add(id,1);}
    if(total==0)return no_events(request,agent);total=add(total,static_cast<int32_t>(elements.front()->size()));std::string suffix="</array><key>id</key>";suffix+=id==0?"<integer />":"<integer>"+std::to_string(id)+"</integer>";suffix+="</map></llsd>";element=std::make_shared<std::vector<uint8_t>>(suffix.begin(),suffix.end());elements.push_back(element);total=add(total,static_cast<int32_t>(element->size()));
    auto result=std::make_shared<server::PollTable>();result->set("int_response_code",int32_t(200));result->set("content_type",std::string("application/xml"));if(total<0)throw std::length_error("Event response size overflow");auto bytes=std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(total));size_t position=0;for(const auto& item:elements){if(item->size()>bytes->size()-position)throw std::out_of_range("Event array copy");std::copy(item->begin(),item->end(),bytes->begin()+static_cast<std::ptrdiff_t>(position));position+=item->size();}result->set("bin_response_data",bytes);result->set("keepaliveTimeout",int32_t(60));return result;
}
}
