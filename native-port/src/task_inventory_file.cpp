// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/task_inventory_file.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <stdexcept>
namespace opensim::scene {
namespace {
std::string mask(std::uint32_t value){constexpr char hex[]="0123456789abcdef";std::string out(8,'0');for(int n=7;n>=0;--n){out[n]=hex[value&15];value>>=4;}return out;}
std::string_view type_name(int value,bool inventory){
 // Actual bundled SDK Utils tables, including empty slots and array bounds.
 static const auto assets=[] {std::array<std::string_view,58> a{};a[0]="texture";a[1]="sound";a[2]="callcard";a[3]="landmark";a[4]="script";a[5]="clothing";a[6]="object";a[7]="notecard";a[8]="category";a[10]="lsltext";a[11]="lslbyte";a[12]="txtr_tga";a[13]="bodypart";a[17]="snd_wav";a[18]="img_tga";a[19]="jpeg";a[20]="animatn";a[21]="gesture";a[22]="simstate";a[24]="link";a[25]="link_f";a[49]="mesh";a[56]="settings";a[57]="material";return a;}();
 static const auto types=[] {std::array<std::string_view,27> a{};a[0]="texture";a[1]="sound";a[2]="callcard";a[3]="landmark";a[6]="object";a[7]="notecard";a[8]="category";a[9]="root";a[10]="script";a[15]="snapshot";a[17]="attach";a[18]="wearable";a[19]="animation";a[20]="gesture";a[22]="mesh";a[25]="settings";a[26]="material";return a;}();
 auto index=std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value));if(index<0)throw std::out_of_range("SDK inventory type table index");return inventory?types.at(static_cast<std::size_t>(index)):assets.at(static_cast<std::size_t>(index));
}
void line(std::string& out,std::string_view name,std::string_view value){out+="\t\t";out+=name;out+='\t';out+=value;out+='\n';}
// SDK's unbounded count includes embedded-NUL suffixes, but its encoder stops
// at the first NUL, leaving that allocated suffix zero-filled. A final NUL is
// replaced. The bounded filename helper sees only our ASCII UUID filename.
std::vector<std::uint8_t> bytes(std::string_view value){if(!value.empty()&&value.back()=='\0')value.remove_suffix(1);if(value.empty())return {};std::vector<std::uint8_t> out(value.size()+1,0);auto count=value.find('\0');if(count==value.npos)count=value.size();std::copy_n(value.begin(),count,out.begin());return out;}
}
TaskInventoryFile::TaskInventoryFile(framework::Uuid part,TaskInventoryFileBackend backend):part_(part),backend_(std::move(backend)){if(!backend_.can_edit_inventory||!backend_.random_uuid||!backend_.add_file||!backend_.reply)throw std::invalid_argument("Null task inventory file backend");}
void TaskInventoryFile::request(framework::Uuid agent,std::uint32_t serial,std::span<const TaskInventoryFileItem> items,bool god){std::lock_guard lock(mutex_);if(!serial||items.empty()){backend_.reply(part_,0,{});return;}bool changed=false;if(filename_serial_!=serial){filename_serial_=serial;changed=true;}if(data_->size()<2)changed=true;
 int privileged=0;bool include;if(god){privileged=3;include=true;}else{include=backend_.can_edit_inventory(part_,agent);if(include)privileged=1;}if(privileged_!=privileged)changed=true;
 auto send=[&]{backend_.add_file(filename_,data_);backend_.reply(part_,std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(filename_serial_)),filename_bytes_);};
 if(!changed){send();return;}privileged_=privileged;
 std::string out="\tinv_object\t0\n\t{\n";line(out,"obj_id",part_.str());line(out,"parent_id",framework::Uuid{}.str());line(out,"type","category");line(out,"name","Contents|\n\t}");
 for(const auto& item:items){out+="\tinv_item\t0\n\t{\n";line(out,"item_id",item.item.str());line(out,"parent_id",part_.str());out+="\tpermissions 0\n\t{\n";line(out,"base_mask",mask(item.base_permissions));line(out,"owner_mask",mask(item.current_permissions));line(out,"group_mask",mask(item.group_permissions));line(out,"everyone_mask",mask(item.everyone_permissions));line(out,"next_owner_mask",mask(item.next_permissions));line(out,"creator_id",item.creator.str());line(out,"last_owner_id",item.last_owner.str());line(out,"group_id",item.group.str());bool group_owned=!item.group.is_zero()&&item.owner==item.group;line(out,"owner_id",group_owned?framework::Uuid{}.str():item.owner.str());line(out,"group_owned",group_owned?"1":"0");out+="\t}\n";
  bool allow=include;if(include&&!god){constexpr std::uint32_t modify_copy=0x0000c000u;switch(item.inventory_type){case 7:allow=(item.current_permissions&modify_copy)!=0;break;case 10:case 25:case 26:allow=(item.current_permissions&modify_copy)==modify_copy;break;default:break;}}
  line(out,"asset_id",allow?item.asset.str():framework::Uuid{}.str());line(out,"type",type_name(item.type,false));line(out,"inv_type",type_name(item.inventory_type,true));line(out,"flags",mask(item.flags));out+="\tsale_info\t0\n\t{\n";line(out,"sale_type","not");line(out,"sale_price","0");out+="\t}\n";line(out,"name",item.name+"|");line(out,"desc",item.description+"|");line(out,"creation_date",std::to_string(item.creation_date));out+="\t}\n";
 }
 data_=std::make_shared<const std::vector<std::uint8_t>>(bytes(out));if(data_->size()>2){filename_="inventory_"+backend_.random_uuid().str()+".tmp";filename_bytes_=bytes(filename_);send();return;}backend_.reply(part_,0,{});
}
}
