// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/capabilities_module.hpp"
#include "opensim/server/caps_handlers.hpp"
#include "opensim/server/poll_types.hpp"
#include <condition_variable>
#include <atomic>
namespace opensim::services {
// Match the source's separate uint property reads/writes, including a possible
// lost update across read/modify/write, without a C++ data race between requests.
class CapabilityFlags {
public:
    CapabilityFlags(std::uint32_t value=0):value_(value){}
    CapabilityFlags(const CapabilityFlags& other):value_(static_cast<std::uint32_t>(other)){}
    CapabilityFlags& operator=(const CapabilityFlags& other)noexcept{return *this=static_cast<std::uint32_t>(other);}
    operator std::uint32_t()const noexcept{return value_.load(std::memory_order_relaxed);}
    CapabilityFlags& operator=(std::uint32_t value)noexcept{value_.store(value,std::memory_order_relaxed);return *this;}
    CapabilityFlags& operator|=(std::uint32_t bits)noexcept{const auto previous=value_.load(std::memory_order_relaxed);value_.store(previous|bits,std::memory_order_relaxed);return *this;}
private:
    std::atomic<std::uint32_t> value_;
};
class CapabilityRuntimeListener:public server::CapabilityListener {
public:
    virtual std::uint32_t port()const=0;
    virtual std::uint32_t ssl_port()const=0;
    virtual std::optional<std::string> ssl_common_name()const=0;
    virtual void add_poll(const std::shared_ptr<CapabilityPollHandler>& handler)=0;
    virtual void remove_poll(const std::optional<std::string>& url)=0;
};
// Injectable wait primitive keeps the source 120000 ms deadline observable in
// reference comparisons without making the suite sleep for two minutes.
class CapabilityActivation {
public:
    virtual ~CapabilityActivation()=default;
    virtual void reset()=0;
    virtual void set()=0;
    virtual bool wait(int milliseconds)=0;
    virtual void dispose()=0;
};
class Caps:public CapabilityObject {
public:
    using Name=server::CapsHandlers::Name;
    using Requested=server::CapsHandlers::Requested;
    using Polls=framework::OrderedDictionary<std::string,std::shared_ptr<CapabilityPollHandler>>;
    using MainServer=std::function<std::shared_ptr<CapabilityRuntimeListener>()>;
    enum Flag:std::uint32_t {None=0,SentSeeds=1,ObjectAnim=0x100,WLEnv=0x200,AdvEnv=0x400,PBR=0x800,ViewerBenefits=0x1000,TPBR=0x2000};
    Caps(std::shared_ptr<CapabilityRuntimeListener> listener,Name host,std::uint32_t port,Name path,framework::Uuid agent,Name region,MainServer main_server,std::shared_ptr<CapabilityActivation> activation={},std::function<void(const std::string&)> error={});
    CapabilityFlags flags{None};
    Name caps_path()const override{return path_;}
    framework::Uuid agent_id()const override{return agent_;}
    Name region_name()const{return region_;}
    Name host_name()const{return host_;}
    std::uint32_t port()const{return port_;}
    std::shared_ptr<CapabilityRuntimeListener> http_listener()const{return listener_;}
    bool ssl_caps()const;
    Name ssl_common_name()const;
    server::CapsHandlers& handlers(){return *handlers_;}
    CapabilityPaths& external_handlers(){return external_;}
    void register_handler(const Name& name,std::shared_ptr<server::CapabilityStreamHandler> handler);
    void register_simple(const Name& name,std::shared_ptr<server::CapabilitySimpleHandler> handler,bool add_to_listener=true);
    void register_poll(const Name& name,std::shared_ptr<CapabilityPollHandler> handler);
    void register_external(const Name& name,Name url);
    bool try_get_poll(const Name& name,std::shared_ptr<CapabilityPollHandler>& handler)const;
    Polls get_poll_handlers()const;
    CapabilityPaths details(bool exclude_seed,const Requested* requested)const;
    void append_details_xml(const Requested* requested,std::string* output)const;
    void deregister_handlers()override;
    void dispose()override{dispose(true);}
    void dispose(bool disposing);
    void activate()override;
    bool wait_for_activation();
    Name base_url()const override{return handlers_->base_url();}
    CapabilityPaths local_paths()const override{return handlers_->local_paths();}
    CapabilityPaths poll_paths()const override;
    CapabilityPaths external_paths()const override{return external_;}
private:
    std::shared_ptr<CapabilityRuntimeListener> listener_;
    Name host_,path_,region_;
    std::uint32_t port_;
    framework::Uuid agent_;
    MainServer main_;
    std::string base_caps_url_;
    std::unique_ptr<server::CapsHandlers> handlers_;
    mutable std::recursive_mutex poll_mutex_,activation_mutex_;
    Polls polls_;
    CapabilityPaths external_;
    std::shared_ptr<CapabilityActivation> activation_;
    std::function<void(const std::string&)> error_;
    std::shared_ptr<CapabilityActivation> activation()const;
};
}
