// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include "opensim/network/lludp.hpp"
#include <functional>
namespace opensim::scene {
using UserInventoryItem=std::shared_ptr<services::InventoryItem>;
// List<T> enumerator mutation semantics at the scene/service boundary.
class UserInventoryMoveList {
public:
 std::size_t size()const{return values_.size();}std::uint32_t version()const{return version_;}
 UserInventoryItem at(std::size_t i)const{return values_.at(i);}
 void add(UserInventoryItem item){values_.push_back(std::move(item));++version_;}
 void clear(){values_.clear();++version_;}
 void set(std::size_t index,UserInventoryItem item){values_.at(index)=std::move(item);++version_;}
private:std::vector<UserInventoryItem> values_;std::uint32_t version_=0;
};
struct UserInventoryBackend {
 std::function<UserInventoryItem(framework::Uuid,framework::Uuid)> item;
 std::function<bool(UserInventoryItem)> update;
 std::function<bool(framework::Uuid,UserInventoryMoveList&)> move;
 std::function<void(framework::Uuid,const std::vector<framework::Uuid>&)> delete_items,delete_folders;
 std::function<void(framework::Uuid,UserInventoryItem)> bulk;
 std::function<void(framework::Uuid,framework::Uuid,UserInventoryItem)> transaction;
 std::function<std::int32_t()> unix_time;
 std::function<framework::Uuid(const services::InventoryItem&)> creator;
 std::function<void(std::string)> debug,warn,error;
 // Empty uses the native invariant request-thread culture.
 std::function<bool(std::string_view,std::string_view)> starts_with;
 std::function<std::optional<std::size_t>(std::string_view,std::string_view)> last_index;
};
class UserInventoryOperations {
public:
 explicit UserInventoryOperations(UserInventoryBackend);
 void update(framework::Uuid agent,framework::Uuid transaction,framework::Uuid key,UserInventoryItem incoming)const;
 void move(framework::Uuid agent,UserInventoryMoveList*)const;
 void remove_items(framework::Uuid,const std::vector<framework::Uuid>&)const;
 void remove_folders(framework::Uuid,const std::vector<framework::Uuid>&)const;
private:UserInventoryBackend backend_;
};
// InventoryItemBase Name/Description property projection through osUTF8. Input
// is valid native UTF8; arbitrary invalid CLR UTF16 remains a separate boundary.
std::string user_inventory_text(std::string_view,bool description=false);
std::int32_t user_inventory_unix_time();
struct UserInventoryUpdateRequest {framework::Uuid transaction,key;UserInventoryItem item;};
std::vector<UserInventoryUpdateRequest> user_inventory_update_requests(const network::lludp::Packet&);
UserInventoryMoveList user_inventory_move_request(framework::Uuid agent,const network::lludp::Packet&);
}
