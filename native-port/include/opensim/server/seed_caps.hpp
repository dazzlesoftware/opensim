// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/caps.hpp"
#include "opensim/network/rpc.hpp"
namespace opensim::server {
struct SdkLlsdNode {std::optional<network::Value> value;std::vector<SdkLlsdNode> children;std::vector<std::string> keys;std::string tag;};
SdkLlsdNode deserialize_sdk_llsd_node(std::string_view xml);
// Null root/array entries differ from OSD.Undefined in the managed SDK.
std::optional<services::Caps::Requested> deserialize_seed_caps(std::string_view xml);
class SeedCapsBackend {
public:
    virtual ~SeedCapsBackend()=default;
    virtual services::Caps& caps()=0;
    virtual std::optional<std::string> region_name()const=0;
    virtual bool check_client(const framework::Uuid& agent,const network::HttpRequest& request)=0;
    virtual std::uint32_t region_size_x()const=0;
    virtual std::uint32_t region_size_y()const=0;
    virtual void debug(const std::string& message)=0;
    virtual void warn(const std::string& message)=0;
    virtual std::optional<services::Caps::Requested> deserialize(const network::HttpRequest& request){return deserialize_seed_caps(request.body);}
};
class SeedCapsHandler:public CapabilitySimpleHandler {
public:
    explicit SeedCapsHandler(SeedCapsBackend& backend):backend_(backend),path_("/CAPS/"+backend.caps().caps_path().value_or("")+"0000"){}
    std::optional<std::string> path()const override{return path_;}
    network::HttpResponse handle(const network::HttpRequest& request)override{network::HttpResponse response;process_request(request,response);return response;}
    void process_request(const network::HttpRequest& request,network::HttpResponse& response);
private:
    SeedCapsBackend& backend_;
    std::string path_;
};
}
