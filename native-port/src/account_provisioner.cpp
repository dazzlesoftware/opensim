// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/account_provisioner.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::services {
std::shared_ptr<UserAccount> AccountProvisioner::create(framework::Uuid scope,framework::Uuid principal,
    std::optional<std::string> first,std::optional<std::string> last,std::optional<std::string> password,
    std::optional<std::string> email,std::optional<std::string> model){
    // CreateUser intentionally ignores the incoming scope in the reference.
    (void)scope;if(!first||!last)throw std::invalid_argument("Null account name");
    *first=framework::trim_utf8(*first);*last=framework::trim_utf8(*last);
    if(operations_.by_name({},*first,*last))return {};
    auto account=std::make_shared<UserAccount>(framework::Uuid{},principal,*first,*last,std::move(email),clock_);
    (*account->service_urls)["HomeURI"]=std::string{};(*account->service_urls)["InventoryServerURI"]=std::string{};(*account->service_urls)["AssetServerURI"]=std::string{};
    if(!operations_.store(*account))return {};
    if(operations_.set_password)operations_.set_password(framework::Uuid(account->principal_id),std::move(password));
    if(operations_.default_regions){
        auto regions=operations_.default_regions({});
        if(!regions.empty()&&regions.front())operations_.set_home(account->principal_id,regions.front()->id,{128,128,0},{0,1,0});
    }
    if(operations_.create_inventory){
        operations_.create_inventory(framework::Uuid(account->principal_id));
        // A false inventory result does not suppress default appearance setup.
        if(create_default_entries_){
            if(!model||model->empty())operations_.default_appearance(framework::Uuid(account->principal_id));
            else operations_.model_appearance(framework::Uuid(account->principal_id),*model);
        }
    }
    return account;
}
}
