// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/task_inventory_item.hpp"
#include <chrono>
#include <limits>
namespace opensim::framework {
TaskInventoryItem::TaskInventoryItem(std::function<std::int32_t()> unix_time){
 if(unix_time)creation_date=static_cast<std::uint32_t>(unix_time());
 else{double seconds=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();auto value=seconds>=2147483648.0||seconds< -2147483648.0?std::numeric_limits<std::int32_t>::min():static_cast<std::int32_t>(seconds);creation_date=static_cast<std::uint32_t>(value);}
}
std::string TaskInventoryItem::creator_identification()const{return creator_id.str()+((creator_data&&!creator_data->empty())?";"+*creator_data:std::string{});}
void TaskInventoryItem::creator_identification(const std::optional<std::string>& value){
 if(!value||value->empty()){creator_data=std::string{};return;}
 auto delimiter=value->find(';');
 if(delimiter==std::string::npos){try_parse_sdk_uuid(*value,creator_id);return;}
 try_parse_sdk_uuid(std::string_view(*value).substr(0,delimiter),creator_id);
 auto second=value->find(';',delimiter+1);
 if(second==std::string::npos){creator_data=value->substr(delimiter+1)+";Unknown User";return;}
 auto third=value->find(';',second+1);
 creator_data=value->substr(delimiter+1,second-delimiter-1)+";"+value->substr(second+1,third==std::string::npos?std::string::npos:third-second-1);
}
TaskInventoryItem TaskInventoryItem::clone()const{return *this;}
void TaskInventoryItem::reset_ids(Uuid part,std::function<Uuid()> random_uuid){loaded_item_id=old_item_id;old_item_id=item_id;item_id=random_uuid();parent_part_id=part;parent_id=part;}
}
