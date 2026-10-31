// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/caps.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <functional>
namespace opensim::server {
class HttpRouteRegistry {
public:
    using Name=std::optional<std::string>;
    using Stream=std::shared_ptr<CapabilityStreamHandler>;
    using Simple=std::shared_ptr<CapabilitySimpleHandler>;
    using Poll=std::shared_ptr<services::CapabilityPollHandler>;
    using Prefix=std::function<bool(std::string_view,std::string_view)>;
    explicit HttpRouteRegistry(Prefix prefix=framework::invariant_starts_with):prefix_(std::move(prefix)){}
    static std::string handler_key(const Name& method,const Name& path){return method.value_or("")+":"+path.value_or("");}
    void add_stream(Stream handler);
    void add_generic_stream(Stream handler);
    void add_simple(Simple handler,bool variable_path=false);
    bool add_poll(const Name& url,const Poll& handler);
    bool add_poll(Poll handler);
    bool add_poll_variable(Poll handler);
    void remove_stream(const Name& method,const Name& path);
    void remove_stream(const Name& path);
    void remove_simple(const Name& path);
    void remove_poll(const Name& path);
    void remove_poll(const Name&,const Name& path){remove_poll(path);}
    bool try_stream(const Name& key,Stream& handler)const;
    bool try_simple(const Name& path,Simple& handler)const;
    bool try_poll(const Name& path,Poll& handler)const;
    Stream root_get()const;
    std::vector<std::string> stream_keys()const;
    std::vector<std::string> simple_keys()const;
    std::vector<std::string> poll_keys()const;
private:
    template<class V>struct Map {mutable std::recursive_mutex mutex;std::unordered_map<std::string,V> values;};
    Map<Stream> streams_;Map<Simple> simple_,simple_variable_;Map<Poll> poll_,poll_variable_;
    mutable std::recursive_mutex stream_scan_mutex_,root_mutex_;
    Stream root_;
    Prefix prefix_;
    template<class V>static std::vector<std::string> keys(const Map<V>& map);
    template<class V>static bool get(const Map<V>& map,const Name& key,V& value);
    template<class V>static bool add(Map<V>& map,const Name& key,const V& value);
    template<class V>static bool erase(Map<V>& map,const Name& key);
};
struct CapabilityListenerInfo {bool ssl=false;std::uint32_t port=0,ssl_port=0;std::optional<std::string> ssl_common_name;};
// Source route ownership/selection for native stream/simple capabilities.
// Root RPC, WebSocket, generic-HTTP and poll scheduling remain host callbacks.
class RegistryCapabilityListener:public services::CapabilityRuntimeListener {
public:
    using PollRequest=std::function<network::HttpResponse(const std::shared_ptr<services::CapabilityPollHandler>&,const network::HttpRequest&)>;
    explicit RegistryCapabilityListener(CapabilityListenerInfo info,HttpRouteRegistry::Prefix prefix=framework::invariant_starts_with):routes(std::move(prefix)),info_(std::move(info)){}
    HttpRouteRegistry routes;
    bool use_ssl()const override{return info_.ssl;}
    std::uint32_t port()const override{return info_.port;}
    std::uint32_t ssl_port()const override{return info_.ssl_port;}
    std::optional<std::string> ssl_common_name()const override{return info_.ssl_common_name;}
    void add_stream(const std::shared_ptr<CapabilityStreamHandler>& handler)override{routes.add_stream(handler);}
    void remove_stream(const std::string& method,const std::optional<std::string>& path)override{routes.remove_stream(method,path);}
    void add_simple(const std::shared_ptr<CapabilitySimpleHandler>& handler)override{routes.add_simple(handler);}
    void remove_simple(const std::optional<std::string>& path)override{routes.remove_simple(path);}
    void add_poll(const std::shared_ptr<services::CapabilityPollHandler>& handler)override{routes.add_poll(handler);}
    void remove_poll(const std::optional<std::string>& url)override{routes.remove_poll(url);}
    network::HttpResponse dispatch(const network::HttpRequest& request,const network::HttpServer::Handler& fallback={},const PollRequest& poll_request={})const;
private:
    CapabilityListenerInfo info_;
};
}
