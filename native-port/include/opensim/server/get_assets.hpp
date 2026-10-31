// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include "opensim/network/http_server.hpp"
#include "opensim/server/http_response.hpp"
namespace opensim::server {
using AssetQueries=framework::OrderedDictionary<std::string,std::optional<std::string>>;
std::shared_ptr<AssetQueries> asset_query_dictionary(std::string_view target);
class AssetHttpRequest {
public:
    explicit AssetHttpRequest(network::HttpRequest value):native(std::move(value)){}
    virtual ~AssetHttpRequest()=default;
    virtual std::shared_ptr<AssetQueries> queries();
    virtual std::optional<std::string> range();
    network::HttpRequest native;
private:
    std::mutex query_mutex_;std::shared_ptr<AssetQueries> queries_;
};
class AssetRetrievalService {
public:
    using Complete=std::function<void(std::shared_ptr<framework::AssetBase>)>;
    virtual ~AssetRetrievalService()=default;
    virtual void get(const std::string& id,const std::optional<std::string>& foreign_service,bool store_local,Complete complete)=0;
};
class GetAssetsHandler:public std::enable_shared_from_this<GetAssetsHandler> {
public:
    using Warn=std::function<void(const std::string&)>;
    explicit GetAssetsHandler(std::shared_ptr<AssetRetrievalService> service,Warn warn={}):service_(std::move(service)),warn_(std::move(warn)){}
    void handle(std::shared_ptr<AssetHttpRequest> request,std::shared_ptr<HttpResponseState> response,const std::optional<std::string>& service_url={});
private:
    std::shared_ptr<AssetRetrievalService> service_;
    Warn warn_;
};
}
