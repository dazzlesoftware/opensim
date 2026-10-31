// Callback adapter for actual model-transfer source comparisons.
struct TransferFixture {
    std::shared_ptr<framework::AvatarAppearance> appearance=std::make_shared<framework::AvatarAppearance>();
    std::map<Id,std::shared_ptr<services::InventoryItem>> source_items;
    std::shared_ptr<framework::AvatarAttachment> second;
    int gets=0;
    TransferFixture(int variant){
        appearance->wearables=std::make_shared<framework::AvatarAppearance::Wearables>(3);auto w=std::make_shared<framework::AvatarWearable>();
        for(int n=11;n<=15;++n)w->add(id(n),id(190+n));(*appearance->wearables)[0]=w;(*appearance->wearables)[2]=std::make_shared<framework::AvatarWearable>();
        auto make=[&](int n,int type,int asset){auto a=std::make_shared<services::InventoryItem>();a->id=id(n);a->owner=id(8);a->asset_id=id(asset);a->asset_type=type;a->inventory_type=type==6?6:18;a->creator_id="creator;https://profile;Name";a->name="Source 試験 "+std::to_string(n);a->description="Description";a->next_permissions=0xa000;a->current_permissions=-1;a->base_permissions=std::bit_cast<std::int32_t>(0xf0f0f0f0u);a->everyone_permissions=0xfffffff;a->group_permissions=std::bit_cast<std::int32_t>(0xffff0000u);a->group_id=id(44);a->group_owned=1;a->sale_price=123;a->sale_type=200;a->creation_date=-123;a->flags=std::bit_cast<std::int32_t>(0x80000003u);source_items[id(n)]=a;};
        make(11,13,201);make(12,24,22);make(13,24,0);make(15,24,33);make(22,13,222);make(33,24,34);make(21,6,301);
        appearance->append_attachment(std::make_shared<framework::AvatarAttachment>(1,id(21),id(301)));second=std::make_shared<framework::AvatarAttachment>(2,id(22),id(302));appearance->append_attachment(second);appearance->append_attachment(std::make_shared<framework::AvatarAttachment>(3,Id{},id(303)));
        if(variant==1)appearance->wearables=std::make_shared<framework::AvatarAppearance::Wearables>();if(variant==2)appearance->wearables.reset();if(variant==3)appearance.reset();
        if(variant==4)(*appearance->wearables)[0]->clear();if(variant==5){appearance->clear_attachments();source_items.clear();}if(variant==6){appearance->clear_attachments();w->clear();w->add(id(15),id(205));}
    }
    void before_random(int n,int mutation){if(mutation==11&&n==1&&source_items.contains(id(11))){auto a=source_items.at(id(11));a->name="Random source";a->asset_id=id(777);a->next_permissions=32768;}}
    void before_clock(int n,int mutation){if(mutation==12&&n==1&&source_items.contains(id(11))){auto a=source_items.at(id(11));a->name="Clock source";a->creation_date=-999;}}
    void queried(Id item,int mutation){
        ++gets;if(!appearance)return;
        if(mutation==6&&gets==1)appearance->set_wearable(20,std::make_shared<framework::AvatarWearable>(id(99),id(199)));
        if(mutation==7&&gets==1&&appearance->wearables&&!appearance->wearables->empty()&&appearance->wearables->front())appearance->wearables->front()->remove_item(id(12));
        if(mutation==5&&item==id(21)){second->point=55;second->item=id(11);}
        if(mutation==8&&item==id(21))appearance->append_attachment(std::make_shared<framework::AvatarAttachment>(70,id(31),id(888)));
    }
};
