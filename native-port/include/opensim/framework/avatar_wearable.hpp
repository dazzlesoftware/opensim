// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <memory>
#include <optional>
#include <vector>
namespace opensim::network {struct Value;}
namespace opensim::framework {
struct WearableItem {Uuid item,asset;bool operator==(const WearableItem&)const=default;};
class AvatarWearable {
public:
    enum : int {Body=0,Skin=1,Hair=2,Eyes=3,Shirt=4,Pants=5,Shoes=6,Socks=7,Jacket=8,Gloves=9,Undershirt=10,Underpants=11,Skirt=12,MaxBasicWearables=13,Alpha=13,Tattoo=14,LegacyVersionMaxWearables=15,Physics=15,Universal=16};
    inline static int max_wearables_pv7=16,max_wearables=17;
    inline static const Uuid default_body_item{"66c41e39-38f9-f75a-024e-585989bfaba9"},default_body_asset{"66c41e39-38f9-f75a-024e-585989bfab73"};
    inline static const Uuid default_hair_item{"d342e6c1-b9d2-11dc-95ff-0800200c9a66"},default_hair_asset{"d342e6c0-b9d2-11dc-95ff-0800200c9a66"};
    inline static const Uuid default_skin_item{"77c41e39-38f9-f75a-024e-585989bfabc9"},default_skin_asset{"77c41e39-38f9-f75a-024e-585989bbabbb"};
    inline static const Uuid default_eyes_item{"cdc31054-eed8-4021-994f-4e0c6e861b50"},default_eyes_asset{"4bb6fa4d-1cd2-498a-a84c-95c1a0e745a7"};
    inline static const Uuid default_shirt_item{"77c41e39-38f9-f75a-0000-585989bf0000"},default_shirt_asset{"00000000-38f9-1111-024e-222222111110"};
    inline static const Uuid default_pants_item{"77c41e39-38f9-f75a-0000-5859892f1111"},default_pants_asset{"00000000-38f9-1111-024e-222222111120"};
    inline static const Uuid default_alpha_item{"bfb9923c-4838-4d2d-bf07-608c5b1165c8"},default_alpha_asset{"1578a2b1-5179-4b53-b618-fe00ca5a5594"};
    inline static const Uuid default_tattoo_item{"c47e22bd-3021-4ba4-82aa-2b5cb34d35e1"},default_tattoo_asset{"00000000-0000-2222-3333-100000001007"};
    AvatarWearable()=default;
    AvatarWearable(const Uuid& item,const Uuid& asset){wear(item,asset);}
    explicit AvatarWearable(const network::Value& array);
    std::size_t count()const noexcept{return ids_.size();}
    void add(const Uuid& item,const Uuid& asset);
    void wear(const Uuid& item,const Uuid& asset){clear();add(item,asset);}
    void wear(const WearableItem& item){wear(item.item,item.asset);}
    void clear();
    void remove_item(const Uuid& item);
    void remove_asset(const Uuid& asset);
    WearableItem at(int index)const;
    Uuid asset(const Uuid& item)const;
    network::Value pack()const;
    void unpack(const network::Value& array);
    void unpack(const network::Value* array);
    static std::vector<std::shared_ptr<AvatarWearable>> defaults();
private:
    std::optional<std::size_t> slot(const Uuid& item)const;
    // .NET Dictionary enumeration reuses removed slots; RemoveAsset observes
    // that order rather than the independent wearable presentation order.
    std::vector<std::optional<WearableItem>> slots_;
    std::vector<std::size_t> free_;
    std::vector<Uuid> ids_;
};
}
