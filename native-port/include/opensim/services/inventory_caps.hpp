// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include "opensim/network/http_server.hpp"
#include "opensim/network/rpc.hpp"
#include "opensim/server/caps_handlers.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
namespace opensim::services {
// Source ExpiringKey membership is timer-purged, never lazily expired by lookup.
// Clock supplies signed milliseconds relative to construction (source int cast).
// Optional observer schedules source one-shot timer events; call purge() when
// the timer fires. The observer runs under the cache lock and must not reenter.
class InventoryBadRequests {
public:
 enum class TimerEvent {Arm,Rearm,Disarm};
 using TimerObserver=std::function<void(TimerEvent,std::int32_t)>;
 explicit InventoryBadRequests(std::function<std::int32_t()> clock,TimerObserver timer={});
 ~InventoryBadRequests();
 void add(framework::Uuid key,std::int32_t expire_ms=30000);
 bool contains(framework::Uuid key)const;
 void purge();
private:
 std::function<std::int32_t()> clock_;
 TimerObserver timer_;
 bool timer_active_=false;
 void disarm();
 mutable std::mutex mutex_;
 std::map<framework::Uuid,std::int32_t> entries_;
};
// InventoryItemBase.ToLLSDxml, including original field order/zero spelling.
std::string inventory_item_llsd(const InventoryItem&,std::uint32_t flags_mask=0xffffffffu);
class FetchInventory2Core {
public:
 using Items=std::vector<std::optional<InventoryItem>>;
 using GetMultipleItems=std::function<std::optional<Items>(framework::Uuid,const std::vector<framework::Uuid>&)>;
 FetchInventory2Core(framework::Uuid agent,GetMultipleItems get);
 // Implements FetchInventorySimpleRequest after SimpleOSDMapHandler parsing.
 // Malformed array/map casts throw; HTTP method/body parsing belongs to host.
 network::HttpResponse handle(const network::Value& request_map,InventoryBadRequests* bad_requests)const;
private:framework::Uuid agent_;GetMultipleItems get_;
};
class FetchInventory2Route final:public server::CapabilitySimpleHandler {
public:
 using Deserialize=std::function<network::Value(std::string_view)>;
 FetchInventory2Route(std::string path,std::shared_ptr<FetchInventory2Core> core,
  std::shared_ptr<InventoryBadRequests> bad_requests,Deserialize deserialize={});
 std::optional<std::string> path()const override{return path_;}
 network::HttpResponse handle(const network::HttpRequest&)override;
 std::atomic<std::uint32_t> requests_received=0,requests_handled=0;
 // Existing native parsers cover XML/JSON. SDK notation/binary need an injected
 // decoder; they are not silently interpreted as JSON by this profile.
 static network::Value deserialize_xml_json(std::string_view);
private:
 std::string path_;std::shared_ptr<FetchInventory2Core> core_;
 std::shared_ptr<InventoryBadRequests> bad_requests_;Deserialize deserialize_;
};
}
