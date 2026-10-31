// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include <functional>
namespace opensim::services {struct InventoryItem;}
namespace opensim::framework {
// The original constructor prints the data without parsing it. Do not infer
// members from that input or repair the source's literal serialization header.
class AnimationSet {
public:
 static constexpr std::uint32_t create_base_permissions=0x8e000,create_next_permissions=0xc000,allowed_base_permissions=0xc000,allowed_next_permissions=0;
 static void set_create_item_permissions(services::InventoryItem*);
 static void enforce_item_permissions(services::InventoryItem*,bool creator);
 explicit AnimationSet(AssetBytes,std::function<void(const std::string&)> write_line={});
 int animation_count()const{return 0;}
 Uuid animation(const std::optional<std::string>& index)const;
 std::optional<std::string> animation_name(const std::optional<std::string>& index)const;
 void set_animation(const std::optional<std::string>& index,std::optional<std::string> name,Uuid animation);
 AssetBytes to_bytes()const;
 std::uint32_t validate(const std::function<std::uint32_t(Uuid)>&);
 static const AssetBytes zero_count_bytes;
private:
 OrderedDictionary<std::string,std::pair<std::optional<std::string>,Uuid>> animations_;
};
}
