#include "avatar_data_fixture.hpp"
std::string value(const std::string& s){return s=="."?std::string{}:fixture::unhex(s);}
std::optional<std::string> nullable(const std::string& s){return s=="-"?std::nullopt:std::optional(value(s));}
std::string view(const services::AvatarData& a){
    std::string s=std::to_string(a.avatar_type)+"|";if(!a.data)s+="null";else for(auto [key,v]:*a.data)s+=hex(key)+"="+(v?"s"+hex(*v):"null")+";";s+='|';
    try{auto pairs=a.to_key_value_pairs();for(auto [k,v]:*pairs)s+=hex(k)+"="+hex(std::any_cast<std::string>(v))+";";}catch(...){s+="EX";}s+='|';try{auto ap=a.to_appearance();s+=ap?appearance(*ap):"null";}catch(...){s+="EX";}return s;
}
int main(){services::AvatarData a;std::string line;while(std::getline(std::cin,line)){
    std::istringstream in(line);std::string op,key,v,kind,status="OK";in>>op;
    try{
        if(op=="default")a=services::AvatarData{};
        else if(op=="empty"){a=services::AvatarData{};a.data=std::make_shared<services::AvatarData::Fields>();}
        else if(op=="type"){in>>a.avatar_type;}
        else if(op=="set"){in>>key>>v;if(!a.data)a.data=std::make_shared<services::AvatarData::Fields>();(*a.data)[value(key)]=nullable(v);}
        else if(op=="remove"){in>>key;if(!a.data)throw std::invalid_argument("Null avatar fields");a.data->erase(value(key));}
        else if(op=="kv"){in>>key>>kind>>v;services::AccountValues fields;std::any object;if(kind=="string")object=value(v);else if(kind=="int")object=std::stoi(v);else if(kind=="bool")object=v=="1";else if(kind=="float")object=std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(v,nullptr,16)));fields[value(key)]=object;a=services::AvatarData(fields);}
        else if(op=="export")a=services::AvatarData(*a.to_key_value_pairs());
        else if(op=="round"){auto ap=a.to_appearance();a=services::AvatarData(ap.get());}
        else if(op=="ap"){
            int mode;in>>mode>>v;framework::AvatarAppearance ap;if(mode==1){ap.serial=-3;ap.avatar_height=std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(v,nullptr,16)));}
            if(mode==2)ap.visual_params.reset();if(mode==3)ap.visual_params=std::make_shared<std::vector<std::uint8_t>>();if(mode==4)ap.wearables.reset();if(mode==5)ap.wearables=std::make_shared<framework::AvatarAppearance::Wearables>();if(mode==6)ap.wearables->at(0).reset();if(mode==7)ap.wearables->resize(6);
            if(mode==8)ap.set_wearable(16,std::make_shared<framework::AvatarWearable>(id(7),id(8)));
            if(mode==9){ap.wearables->at(0)->add(id(7),id(8));for(int n:{1,2})ap.append_attachment(std::make_shared<framework::AvatarAttachment>(5,id(n),id(n+10)));ap.append_attachment(std::make_shared<framework::AvatarAttachment>(129,id(3),id(13)));}
            if(mode==10)ap.visual_params=std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{255});if(mode==11)ap.clear_wearables();a=services::AvatarData(&ap);
        }
    }catch(...){status="EX";}
    std::cout<<status<<'|'<<view(a)<<'\n';
}}
