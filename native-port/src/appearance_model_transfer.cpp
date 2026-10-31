// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/appearance_provisioner.hpp"
#include <bit>
namespace opensim::services {
void AppearanceProvisioner::apply_next_owner_permissions(InventoryItem& item){
    item.current_permissions&=item.next_permissions;item.base_permissions&=item.next_permissions;item.everyone_permissions&=item.next_permissions;
}
void AppearanceProvisioner::copy_model(framework::Uuid destination,framework::Uuid source,std::shared_ptr<framework::AvatarAppearance> appearance){
    if(!appearance||!appearance->wearables)throw std::invalid_argument("Null model wearables");
    auto wearables=appearance->wearables;if(wearables->empty())throw std::runtime_error("Model does not have wearables");
    auto folder=operations_.folder(destination,5);if(!folder)throw std::runtime_error("Cannot locate new clothing folder(s)");
    auto outfit=operations_.folder(destination,46);
    if(folder->type!=5){
        folder=std::make_shared<InventoryFolder>();folder->id=random_();folder->name="Clothing";folder->owner=destination;folder->type=5;
        auto root=operations_.root(destination);if(!root)throw std::invalid_argument("Null inventory root");folder->parent=root->id;folder->version=1;operations_.add_folder(folder);
    }
    auto copied_wearable=std::make_shared<framework::AvatarWearable>();
    auto copy=[&](const std::shared_ptr<InventoryItem>& item){
        auto result=new_item(destination);auto id=result->id;*result=*item;result->id=id;result->owner=destination;result->parent=folder->id;apply_next_owner_permissions(*result);return result;
    };
    for(std::size_t i=0;i<wearables->size();++i){
        auto base=wearables->at(i);if(!base||base->count()==0)continue;copied_wearable->clear();
        for(std::size_t j=0;j<base->count();++j){
            auto wearable=base->at(static_cast<int>(j));if(wearable.item.is_zero())continue;
            auto item=operations_.item(source,wearable.item);
            if(item&&item->asset_type==24)item=item->asset_id.is_zero()?nullptr:operations_.item(source,item->asset_id);
            if(item){
                auto created=copy(item);operations_.add_item(created);copied_wearable->add(created->id,wearable.asset);
                if(!outfit)throw std::invalid_argument("Null Current Outfit folder");
                create_outfit_link(18,std::bit_cast<std::uint32_t>(item->flags),item->name,created->id,destination,outfit->id);
            }
        }
        appearance->set_wearable(static_cast<int>(i),copied_wearable);
    }
    auto attachments=appearance->attachments();appearance->clear_attachments();
    for(const auto& attachment:attachments){
        auto point=attachment.point;auto item_id=attachment.item;if(item_id.is_zero())continue;
        auto item=operations_.item(source,item_id);if(item){
            auto created=copy(item);operations_.add_item(created);appearance->set_attachment(point,created->id,created->asset_id);
            if(!outfit)throw std::invalid_argument("Null Current Outfit folder");
            create_outfit_link(created->inventory_type,std::bit_cast<std::uint32_t>(item->flags),item->name,created->id,destination,outfit->id);
        }
    }
}
}
