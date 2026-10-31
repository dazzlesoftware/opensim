// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/inventory_store.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include "opensim/services/user_account.hpp"
#include <functional>
namespace opensim::services {
struct AppearanceProvisioning {
    using Id=framework::Uuid;
    std::function<std::shared_ptr<InventoryFolder>(Id,int)> folder;
    std::function<bool(std::shared_ptr<InventoryItem>)> add_item;
    std::function<bool(Id,std::shared_ptr<framework::AvatarAppearance>)> set_appearance;
    std::function<std::shared_ptr<UserAccount>(Id,std::string,std::string)> by_name;
    std::function<std::shared_ptr<framework::AvatarAppearance>(Id)> get_appearance;
    std::function<void(Id,Id,std::shared_ptr<framework::AvatarAppearance>)> copy_model;
    std::function<std::shared_ptr<InventoryFolder>(Id)> root;
    std::function<bool(std::shared_ptr<InventoryFolder>)> add_folder;
    std::function<std::shared_ptr<InventoryItem>(Id,Id)> item;
};
class AppearanceProvisioner {
public:
    using Random=std::function<framework::Uuid()>;
    using Clock=std::function<std::int32_t()>;
    AppearanceProvisioner(AppearanceProvisioning operations,Random random={},Clock clock={});
    void create_defaults(framework::Uuid principal);
    void establish(framework::Uuid principal,std::string model);
    void copy_model(framework::Uuid destination,framework::Uuid source,std::shared_ptr<framework::AvatarAppearance> appearance);
    static void apply_next_owner_permissions(InventoryItem& item);
    void create_outfit_link(int inventory_type,std::uint32_t flags,std::string name,
        framework::Uuid item,framework::Uuid principal,framework::Uuid folder);
private:
    std::shared_ptr<InventoryItem> new_item(const framework::Uuid& principal);
    AppearanceProvisioning operations_;
    Random random_;
    Clock clock_;
};
}
