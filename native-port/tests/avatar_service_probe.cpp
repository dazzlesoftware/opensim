#include "opensim/services/avatar_service.hpp"
#include "avatar_data_fixture.hpp"
using services::AvatarBaseData;using services::AvatarData;
std::string optional_text(const std::optional<std::string>& s){return s?"s"+hex(*s):"null";}
std::string data_text(const AvatarData& a){std::string s=std::to_string(a.avatar_type)+":";if(!a.data)return s+"null";for(auto [k,v]:*a.data)s+=hex(k)+"="+optional_text(v)+";";return s;}
struct Database:services::AvatarStore{
    std::map<framework::Uuid,std::map<std::string,std::optional<std::string>>> rows;
    std::vector<std::shared_ptr<AvatarBaseData>> held;std::string trace;int outcome=0,target=0,mutation=0,step=0,stores=0;AvatarData* avatar=nullptr;
    bool touch(){++step;if(outcome==2&&step==target)throw std::runtime_error("provider");return !(outcome==1&&step==target);}
    auto get(framework::Uuid principal)->std::vector<std::shared_ptr<AvatarBaseData>> override{
        trace+="get:"+principal.str()+";";if(!touch())return {};if(mutation==6)return {nullptr};if(mutation==7){auto r=std::make_shared<AvatarBaseData>();return {r};}
        std::vector<std::shared_ptr<AvatarBaseData>> result;for(auto [name,value]:rows[principal]){auto r=std::make_shared<AvatarBaseData>();r->principal=principal;r->data=std::make_shared<AvatarData::Fields>();(*r->data)["Name"]=name;(*r->data)["Value"]=value;result.push_back(r);}return result;
    }
    bool store(std::shared_ptr<AvatarBaseData> row)override{
        ++stores;held.push_back(row);auto name=row->data->at("Name"),value=row->data->at("Value");trace+="store:"+row->principal.str()+":"+optional_text(name)+":"+optional_text(value)+";";if(!touch())return false;rows[row->principal][name.value_or("<null>")]=value;
        if(mutation==2&&stores==1){avatar->data=std::make_shared<AvatarData::Fields>();(*avatar->data)["replacement"]="new";}
        if(mutation==3&&stores==2)(*avatar->data)["added"]="late";
        if(mutation==4&&stores==2)avatar->data->erase("Custom");if(mutation==5&&stores==2)avatar->data->clear();if(mutation==6)row->principal=id(8);if((mutation==8&&stores==1)||(mutation==10&&stores==2))row->data.reset();if(mutation==9&&stores==1)avatar->data.reset();return true;
    }
    bool erase_all(framework::Uuid principal)override{trace+="delete:"+principal.str()+";";if(!touch())return false;bool result=!rows[principal].empty();rows[principal].clear();if(mutation==1){avatar->avatar_type=44;avatar->data=std::make_shared<AvatarData::Fields>();(*avatar->data)["replacement"]="new";}return result;}
    bool erase(framework::Uuid principal,const std::optional<std::string>& name)override{trace+="remove:"+principal.str()+":"+optional_text(name)+";";if(!touch())return false;return rows[principal].erase(name.value_or("<null>"))!=0;}
    std::string snapshot(){std::string s;for(auto [principal,fields]:rows)for(auto [key,value]:fields)s+=principal.str()+":"+hex(key)+"="+optional_text(value)+";";s+='|';for(auto r:held)s+=r->principal.str()+":"+(r->data?optional_text(r->data->at("Name"))+":"+optional_text(r->data->at("Value")):"null")+";";return s;}
};
int main(){std::string line;while(std::getline(std::cin,line)){
    std::istringstream in(line);int mode;std::string h;Database db;in>>mode>>db.outcome>>db.target>>db.mutation>>h;auto height=h=="-"?std::optional<std::string>{}:std::optional(h=="."?std::string{}:fixture::unhex(h));
    AvatarData a;a.avatar_type=2;a.data=std::make_shared<AvatarData::Fields>();(*a.data)["Serial"]="7";(*a.data)["AvatarHeight"]=height;(*a.data)["Custom"]="value";db.avatar=&a;db.rows[id(7)]={{"AvatarType","9"},{"old","kept"}};
    if(mode==4||mode==6){if(db.mutation==1)db.rows[id(7)]["AvatarType"]="bad";if(db.mutation==2)db.rows[id(7)]["AvatarType"]=std::nullopt;if(db.mutation==3)db.rows[id(7)].erase("AvatarType");if(db.mutation==4)db.rows[id(7)]["custom"]=std::nullopt;if(db.mutation==5)db.rows[id(7)].clear();}
    services::AvatarService service(db);std::string result="EX";
    try{
        if(mode==0){if(db.mutation==7)a.data.reset();result=service.set_avatar(id(7),a)?"1":"0";}
        else if(mode==1){services::AvatarService::Strings names={"first","AvatarHeight","nullField"},values={"value",height,std::nullopt};if(db.mutation==8)values.pop_back();result=service.set_items(id(7),db.mutation==7?nullptr:&names,&values)?"1":"0";}
        else if(mode==2){services::AvatarService::Strings names={"old","missing",std::nullopt};result=service.remove_items(id(7),db.mutation==7?nullptr:&names)?"1":"0";}
        else if(mode==3)result=service.reset(id(7))?"1":"0";
        else if(mode==4)result=data_text(service.get_avatar(id(7)));
        else if(mode==5){framework::AvatarAppearance ap;ap.serial=12;result=service.set_appearance(id(7),db.mutation==7?nullptr:&ap)?"1":"0";}
        else if(mode==6){auto ap=service.get_appearance(id(7));result=ap?appearance(*ap):"null";}
    }catch(...){result="EX";}
    std::cout<<hex(result)<<'|'<<hex(db.trace)<<'|'<<hex(db.snapshot())<<'|'<<hex(data_text(a))<<'\n';
}}
