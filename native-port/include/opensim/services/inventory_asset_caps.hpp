// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_caps.hpp"
#include <span>
namespace opensim::services {
class InventoryAssetUpload;
struct InventoryAssetCapsBackend {
 std::function<bool(framework::Uuid)> object_exists;
 std::function<bool(framework::Uuid,framework::Uuid)> can_edit_inventory;
 std::function<framework::Uuid(framework::Uuid,framework::Uuid,framework::Uuid,std::span<const std::uint8_t>)> update;
 std::function<std::string()> new_path;
 std::function<void(std::shared_ptr<InventoryAssetUpload>)> register_upload;
 std::function<void(const std::string&)> remove_upload;
 std::function<void(const std::string&,int)> timer;
 std::function<void(std::span<const std::uint8_t>)> dump;
 std::function<void(const std::string&)> log;
};
class InventoryAssetUpload final:public server::CapabilitySimpleHandler {
public:
 InventoryAssetUpload(framework::Uuid agent,framework::Uuid item,framework::Uuid object,std::uint8_t asset_type,std::string path,std::string remote_ip,InventoryAssetCapsBackend);
 ~InventoryAssetUpload();
 std::optional<std::string> path()const override{return path_;}
 network::HttpResponse handle(const network::HttpRequest&)override;
 void expire();bool active()const;
private:
 bool stop();framework::Uuid agent_,item_,object_;std::uint8_t asset_type_;
 std::string path_,remote_ip_;InventoryAssetCapsBackend backend_;mutable std::mutex mutex_;bool active_=true;
};
class UpdateInventoryAssetCore {
public:
 // Null asset type represents the source animation-set no-op wrapper.
 UpdateInventoryAssetCore(framework::Uuid,std::string origin,std::optional<std::uint8_t>,InventoryAssetCapsBackend,std::string region_name={});
 network::HttpResponse handle(const network::HttpRequest&,const network::Value&)const;
private:framework::Uuid agent_;std::string origin_;std::optional<std::uint8_t> type_;InventoryAssetCapsBackend backend_;std::string region_name_;
};
class UpdateInventoryAssetRoute final:public server::CapabilitySimpleHandler {
public:
 using Deserialize=FetchInventory2Route::Deserialize;
 UpdateInventoryAssetRoute(std::string,std::shared_ptr<UpdateInventoryAssetCore>,Deserialize={});
 std::optional<std::string> path()const override{return path_;}
 network::HttpResponse handle(const network::HttpRequest&)override;
 std::atomic<std::uint32_t> requests_received=0,requests_handled=0;
private:std::string path_;std::shared_ptr<UpdateInventoryAssetCore> core_;Deserialize deserialize_;
};
}
