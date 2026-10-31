// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/animation_set.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/services/inventory_store.hpp"
#include <bit>
#include <iostream>
namespace opensim::framework {
namespace {
const std::string& key(const std::optional<std::string>& index){if(!index)throw std::invalid_argument("Null animation index");return *index;}
AssetBytes bytes(const std::string& value){auto result=std::make_shared<std::vector<std::uint8_t>>();for(auto unit:utf16(value))result->push_back(static_cast<std::uint8_t>(unit));return result;}
}
const AssetBytes AnimationSet::zero_count_bytes=bytes("version 1\ncount 0\n");
void AnimationSet::set_create_item_permissions(services::InventoryItem* item){if(!item)return;item->base_permissions=item->current_permissions=create_base_permissions;item->next_permissions=create_next_permissions;item->group_permissions=item->everyone_permissions=0;}
void AnimationSet::enforce_item_permissions(services::InventoryItem* item,bool creator){if(!item)return;auto mask=[&](int value,std::uint32_t allowed){return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value)&allowed);};auto base=creator?create_base_permissions:allowed_base_permissions,next=creator?create_next_permissions:allowed_next_permissions;item->base_permissions=mask(item->base_permissions,base);item->current_permissions=mask(item->current_permissions,base);item->next_permissions=mask(item->next_permissions,next);item->group_permissions=item->everyone_permissions=0;}
AnimationSet::AnimationSet(AssetBytes data,std::function<void(const std::string&)> write_line){
 if(!data)throw std::invalid_argument("Null animation set data");
 std::string ascii;ascii.reserve(data->size());for(auto byte:*data)ascii.push_back(byte<128?static_cast<char>(byte):'?');
 if(!write_line)write_line=[](const auto& line){std::cout<<line<<std::endl;};
 write_line("--------------------");write_line("AnimationSet length "+std::to_string(ascii.size())+" bytes");write_line(ascii);write_line("--------------------");
}
Uuid AnimationSet::animation(const std::optional<std::string>& index)const{auto it=animations_.find(key(index));return it==animations_.end()?Uuid{}:it->second.second;}
std::optional<std::string> AnimationSet::animation_name(const std::optional<std::string>& index)const{auto it=animations_.find(key(index));return it==animations_.end()?std::optional<std::string>(std::string{}):it->second.first;}
void AnimationSet::set_animation(const std::optional<std::string>& index,std::optional<std::string> name,Uuid animation){auto& name_key=key(index);if(animation.is_zero()){animations_.erase(name_key);return;}animations_[name_key]={std::move(name),animation};}
AssetBytes AnimationSet::to_bytes()const{
 if(animations_.empty())return zero_count_bytes;
 std::string text="$version 1\ncount {m_animations.Count}\n";
 for(const auto& [index,value]:animations_)text+=index+" "+value.second.str()+" "+value.first.value_or("")+"\n";
 return bytes(text);
}
std::uint32_t AnimationSet::validate(const std::function<std::uint32_t(Uuid)>& validator){
 std::uint32_t result=0x7fffffff;
 for(const auto& entry:animations_){auto id=entry.second.second;auto mask=validator(id);if(mask==0)return 0;result&=mask;}
 return result;
}
}
