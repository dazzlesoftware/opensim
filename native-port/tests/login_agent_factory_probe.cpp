#include "opensim/services/login_agent_factory.hpp"
#include "osd_fixture_value.hpp"
#include <iostream>
using namespace opensim;using services::LoginAgentFactory;using framework::AgentCircuitData;
std::string hx(const std::string& value){constexpr char digits[]="0123456789abcdef";std::string text;for(unsigned char c:value){text+=digits[c>>4];text+=digits[c&15];}return text;}
std::string urls(const std::shared_ptr<AgentCircuitData::ServiceUrls>& entries){if(!entries)return "null";std::string text;for(auto& [key,value]:*entries){text+=hx(key)+"=";if(auto s=std::any_cast<std::string>(&value))text+="s"+hx(*s);else if(auto n=std::any_cast<int>(&value))text+="i"+std::to_string(*n);else text+="null";text+=';';}return text;}
int main(int argc,char** argv){
    if(argc>1&&std::string(argv[1])=="suffixes"){
        int start=-1,last=-1;for(int n=0;n<=0x10ffff;++n){
            bool blocked=false;if(n<0xd800||n>0xdfff){std::u16string key=u"SRV_";if(n<0x10000)key+=static_cast<char16_t>(n);else{auto p=n-0x10000;key+=static_cast<char16_t>(0xd800+(p>>10));key+=static_cast<char16_t>(0xdc00+(p&1023));}key+=u"Foo";blocked=!LoginAgentFactory::is_service_key(key);}
            if(blocked){if(start<0)start=n;last=n;}else if(start>=0){std::cout<<std::hex<<start<<' '<<last<<'\n';start=-1;}
        }
        if(start>=0)std::cout<<std::hex<<start<<' '<<last<<'\n';return 0;
    }
    int seed;while(std::cin>>seed){
    services::UserAccount account;account.principal_id="00000000-0000-0000-0000-000000000001";account.first_name="First";account.last_name="Last";
    auto mode=seed%6;if(mode){account.service_urls=std::make_shared<AgentCircuitData::ServiceUrls>();if(mode>=2){(*account.service_urls)["z"]=std::string("https://old/z");(*account.service_urls)["Foo"]=mode==3?std::any(7):mode==4?std::any{}:std::any(std::string(mode==5?"https://new/":"https://old/"));(*account.service_urls)["Empty"]=std::string{};}}
    LoginAgentFactory::Config config;switch(seed/6%12){
    case 1:config={{"Other","value"}};break;case 2:config={{"SRV_Foo","https://new"},{"SRV_New","https://new/"}};break;
    case 3:config={{"SRV_Foo","https://new/"}};break;case 4:config={{"SRV_",""},{"srv_Foo","ignored"}};break;
    case 5:config={{"S\xc2\xad" "RV_Foo","unicode"}};break;case 6:config={{"SRV_\xe8\xa9\xa6\xe9\xa8\x93","unicode"}};break;
    case 7:config={{"SRV_GatekeeperURI","configured"}};break;
    case 8:config={{"SRV_\xcc\x81" "Foo","combining"}};break;
    case 9:config={{std::string("SRV\0_Foo",8),"zero"}};break;
    case 10:config={{"SRV\xcd\x8f_Foo","joining"}};break;
    case 11:config={{"SRV_\xf0\x9f\x98\x80","supplementary"}};break;
    }
    std::optional<std::string> gate=seed/72%3==0?std::nullopt:std::optional<std::string>(seed/72%3==1?"":"https://gatekeeper");
    int stored=0,caps=0,keys=0,reads=0;
    LoginAgentFactory factory([&]{++keys;if(seed%19==0)throw std::runtime_error("keys");std::vector<std::string> result;for(auto& [k,v]:config)result.push_back(k);return result;},[&](const auto& key,const auto& fallback){++reads;if(seed%17==0)throw std::runtime_error("read");for(auto& [k,v]:config)if(k==key)return v;return fallback;},gate,[&](auto&){++stored;if(seed%23==0)throw std::runtime_error("store");return seed%2==0;},[&]{++caps;if(seed%29==0)throw std::runtime_error("caps");return "00000000-0000-0000-0000-000000001111";});
    framework::AvatarAppearance appearance;appearance.serial=42;appearance.texture->get_face(0)->texture_id(framework::Uuid("00000000-0000-0000-0000-000000000002"));appearance.texture->get_face(44)->texture_id(framework::Uuid("00000000-0000-0000-0000-000000000003"));
    services::LoginAgentDetails details;details.session=framework::Uuid("00000000-0000-0000-0000-000000000004");details.secure_session=framework::Uuid("00000000-0000-0000-0000-000000000005");details.circuit=4294967295u;details.position={128,64,25.5f};details.ip_address="127.0.0.1";details.viewer="1.0";details.channel="Viewer";details.mac="mac";details.id0="id0";
    for(int i=0;i<2;++i){
        std::string result="EX";try{auto c=factory.make_agent(account,(seed/216%2)?&appearance:nullptr,details);framework::EntityTransferContext context;result=fixture::base64(network::llsd_xml(c->pack(&context)));}catch(const std::exception&){}
        std::cout<<result<<'|'<<stored<<','<<caps<<','<<keys<<','<<reads<<'|'<<urls(account.service_urls);if(i==0)std::cout<<'|';
    }
    std::cout<<'\n';
}}
