/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/sqlite_friends_plugin.hpp"
#include "opensim/services/sqlite_avatar_plugin.hpp"
#include "opensim/services/sqlite_grid_user_plugin.hpp"
#include "opensim/server/config.hpp"
#include "opensim/server/login_application.hpp"
#include "opensim/services/login_orchestration.hpp"
#include "opensim/services/login_recovery.hpp"
#include "opensim/services/login_completion.hpp"
#include "opensim/services/friends_service.hpp"
#include "opensim/services/friends_connector.hpp"
#include "opensim/services/authentication_connector.hpp"
#include "opensim/services/simulation_connector.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/services/account_provisioner.hpp"
#include "opensim/services/appearance_provisioner.hpp"
#include "opensim/services/avatar_service.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/scene/region_runtime.hpp"
#include "opensim/services/region_asset_runtime.hpp"
#include "opensim/services/region_script_task_runtime.hpp"
#include "opensim/services/region_inventory_asset_runtime.hpp"
#include "opensim/services/region_baked_texture_runtime.hpp"
#include "opensim/services/region_inventory_runtime.hpp"
#include "opensim/services/region_inventory_descendents_runtime.hpp"
#include "opensim/services/region_library_descendents_runtime.hpp"
#include "opensim/services/library_service.hpp"
#include "opensim/server/poll_http_service.hpp"
#include <cmath>
#include <fstream>
#include <algorithm>
#include <charconv>
#include <csignal>
#include <iostream>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <conio.h>
#else
#include <poll.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif
namespace {
struct ScriptQuiesceGuard {opensim::scene::RegionRuntime* runtime=nullptr;~ScriptQuiesceGuard(){if(runtime)try{runtime->quiesce_script_updates();}catch(...) {}}};
volatile std::sig_atomic_t shutdown_requested=0;
void shutdown_signal(int){shutdown_requested=1;}
bool console_quit(std::string& pending) {
    char bytes[256];int count=0;
#ifdef _WIN32
    auto input=GetStdHandle(STD_INPUT_HANDLE);
    const auto type=GetFileType(input);
    if(type==FILE_TYPE_PIPE){DWORD available=0,read=0;if(PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)&&available>0&&ReadFile(input,bytes,std::min<DWORD>(available,sizeof(bytes)),&read,nullptr))count=static_cast<int>(read);}
    else if(type==FILE_TYPE_CHAR){while(_kbhit()&&count<256){const auto c=_getch();if(c==0||c==224){_getch();continue;}bytes[count++]=static_cast<char>(c=='\r'?'\n':c);}}
#else
    pollfd input{STDIN_FILENO,POLLIN,0};
    if(poll(&input,1,0)>0&&(input.revents&POLLIN)){const auto read_count=read(STDIN_FILENO,bytes,sizeof(bytes));if(read_count>0)count=static_cast<int>(read_count);}
#endif
    pending.append(bytes,static_cast<std::size_t>(count));
    for(auto newline=pending.find('\n');newline!=std::string::npos;newline=pending.find('\n')){
        auto command=pending.substr(0,newline);pending.erase(0,newline+1);
        if(!command.empty()&&command.back()=='\r')command.pop_back();
        if(command=="quit"||command=="shutdown")return true;
    }
    if(pending.size()>4096)pending.clear();
    return false;
}
int parse_number(const std::string& text){int result;auto p=std::from_chars(text.data(),text.data()+text.size(),result);if(p.ec!=std::errc{}||p.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid command-line integer");return result;}
std::filesystem::path executable_directory(const std::string& argument){
#ifdef _WIN32
    std::vector<wchar_t> path(32768);auto count=GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()));if(count>0&&count<path.size())return std::filesystem::path(std::wstring(path.data(),count)).parent_path();
#elif defined(__APPLE__)
    std::uint32_t count=0;_NSGetExecutablePath(nullptr,&count);std::vector<char> path(count);if(_NSGetExecutablePath(path.data(),&count)==0)return opensim::framework::utf8_path(path.data()).parent_path();
#elif defined(__linux__)
    std::error_code error;auto path=std::filesystem::read_symlink("/proc/self/exe",error);if(!error)return path.parent_path();
#endif
    return std::filesystem::absolute(opensim::framework::utf8_path(argument)).parent_path();
}
std::optional<opensim::services::InventoryCollection> library_folder_content(const std::shared_ptr<opensim::services::InventoryFolderTree>& root,opensim::framework::Uuid id){
    if(!root)return {};auto folder=root->find_folder(id);if(!folder)return {};
    opensim::services::InventoryCollection result;result.folder=folder->id;result.owner=folder->owner;result.version=folder->version;
    for(const auto& child:folder->children()){if(!child)throw std::invalid_argument("Null library category");result.folders.push_back(*child);}
    for(const auto& item:folder->items()){if(!item)throw std::invalid_argument("Null library item");result.items.push_back(*item);}return result;
}
int run(const std::vector<std::string>& args) {
    using namespace opensim;
    std::string config_path="native-server.ini",host,first,last;
    std::optional<int> port;
    int level=0;
    bool initialize=false,create=false,console=false;
    for(std::size_t i=1;i<args.size();++i) {
        const auto& option=args[i];
        auto next=[&]()->const std::string&{if(i+1>=args.size())throw std::invalid_argument("Missing value for "+option);return args[++i];};
        if(option=="--features"){
            std::cout<<"{\"tls\":"<<(network::HttpServer::tls_available()?"true":"false")<<",\"gzip\":"<<(network::HttpServer::gzip_available()?"true":"false")<<"}\n";return 0;
        }
        if(option=="--help"||option=="-h") {
            std::cout<<"OpenSim native service server with experimental local region support\n"
                "  --config FILE       INI configuration (default native-server.ini)\n"
                "  --host ADDRESS      Override listener address\n"
                "  --port NUMBER       Override HTTP port; 0 selects an unused port\n"
                "  --init-db           Initialize new SQLite databases for enabled services\n"
                "  --create-user FIRST LAST  Read password from stdin, create user and exit\n"
                "  --user-level NUMBER Initial level for --create-user (default 0)\n"
                "  --console           Accept quit/shutdown on stdin\n"
                "  --features          Show compiled TLS and gzip support\n"
                "SIGINT/SIGTERM stop the listener and join its workers.\n";return 0;
        }
        if(option=="--config")config_path=next();
        else if(option=="--host")host=next();
        else if(option=="--port")port=parse_number(next());
        else if(option=="--init-db")initialize=true;
        else if(option=="--create-user"){first=next();last=next();create=true;}
        else if(option=="--user-level")level=parse_number(next());
        else if(option=="--console")console=true;
        else throw std::invalid_argument("Unknown option: "+option);
    }
    if(initialize&&create)throw std::invalid_argument("Initialize the databases before creating users");
    if(port&&(*port < 0 || *port > 65535))throw std::invalid_argument("HTTP port must be between 0 and 65535");
    auto config=server::IniConfig::load(framework::utf8_path(config_path));
    const auto authentication_provider=config.get("NativeServer","AuthenticationProvider","SQLite");
    if(authentication_provider!="SQLite"&&authentication_provider!="Remote")throw std::invalid_argument("AuthenticationProvider must be SQLite or Remote");
    const bool local_authentication=authentication_provider=="SQLite";
    const auto authentication_database=local_authentication?server::database_path(config,"AuthenticationService","AuthenticationDatabase"):std::string{};
    std::unique_ptr<services::AuthenticationConnector> remote_authentication;
    if(!local_authentication)remote_authentication=std::make_unique<services::AuthenticationConnector>(config);
    const bool remote_simulation=config.boolean("NativeServer","EnableRemoteSimulationConnector",false);
    const bool local_region=config.boolean("NativeServer","EnableLocalRegion",false);
    const auto region_physics=config.get("NativeRegion","Physics","None");
    if(local_region&&region_physics!="None"&&region_physics!="ubODE")throw std::invalid_argument("NativeRegion Physics must be None or ubODE");
    const bool physical_region=local_region&&region_physics=="ubODE";
    if(local_region&&remote_simulation)throw std::invalid_argument("Select either the local region or remote simulation connector");
    const bool connected_login=local_region||remote_simulation||config.boolean("NativeServer","EnableLoginOrchestration",false);
    const bool need_friends=config.boolean("NativeServer","EnableFriendsService",false)||config.boolean("NativeServer","EnableFriendsEndpoint",false);
    const auto friends_provider=config.get("NativeServer","FriendsProvider","SQLite");
    if(need_friends&&friends_provider!="SQLite"&&friends_provider!="Remote")throw std::invalid_argument("FriendsProvider must be SQLite or Remote");
    const bool local_friends=need_friends&&friends_provider=="SQLite";
    const bool need_avatar=config.boolean("NativeServer","EnableAvatarService",false)||config.boolean("NativeServer","EnableAvatarEndpoint",false);
    const bool need_grid=connected_login||config.boolean("NativeServer","EnableGridEndpoint",false);
    const bool need_grid_user=connected_login||config.boolean("NativeServer","EnableGridUserEndpoint",false);
    const auto region_provider=config.get("NativeServer","GridRegionProvider","Null");
    const bool durable_regions=need_grid&&region_provider=="SQLite";
    if(need_grid&&region_provider!="Null"&&region_provider!="SQLite")throw std::invalid_argument("GridRegionProvider must be Null or SQLite");
    if(initialize&&connected_login){
        const auto path=server::database_path(config,"InventoryService","InventoryDatabase");
        if(path!=":memory:"&&std::filesystem::exists(framework::utf8_path(path))&&std::filesystem::file_size(framework::utf8_path(path))>0)throw std::runtime_error("Inventory initialization requires an empty destination");
        if(path==server::database_path(config,"UserAccountService","UserAccountDatabase")||path==authentication_database||path==server::database_path(config,"GridUserService","GridUserDatabase")||(durable_regions&&path==server::database_path(config,"GridService","GridRegionDatabase")))throw std::runtime_error("Inventory initialization requires a separate database file");
    }
    if(initialize&&durable_regions){
        const auto path=server::database_path(config,"GridService","GridRegionDatabase");
        if(path!=":memory:"&&std::filesystem::exists(framework::utf8_path(path))&&std::filesystem::file_size(framework::utf8_path(path))>0)throw std::runtime_error("Region initialization requires an empty destination");
        if(path==server::database_path(config,"UserAccountService","UserAccountDatabase")||path==authentication_database||(need_grid_user&&path==server::database_path(config,"GridUserService","GridUserDatabase")))throw std::runtime_error("Region initialization requires a separate database file");
    }
    if(initialize&&need_grid_user){
        const auto path=server::database_path(config,"GridUserService","GridUserDatabase");
        if(path!=":memory:"&&std::filesystem::exists(framework::utf8_path(path))&&std::filesystem::file_size(framework::utf8_path(path))>0)throw std::runtime_error("Grid-user initialization requires an empty destination");
        if(path==server::database_path(config,"UserAccountService","UserAccountDatabase")||path==authentication_database)throw std::runtime_error("Grid-user initialization requires a separate database file");
    }
    if(initialize&&need_avatar){
        const auto path=server::database_path(config,"AvatarService","AvatarDatabase");
        if(path!=":memory:"&&std::filesystem::exists(framework::utf8_path(path))&&std::filesystem::file_size(framework::utf8_path(path))>0)throw std::runtime_error("Avatar initialization requires an empty destination");
        if(path==server::database_path(config,"UserAccountService","UserAccountDatabase")||path==authentication_database||(need_grid_user&&path==server::database_path(config,"GridUserService","GridUserDatabase"))||(connected_login&&path==server::database_path(config,"InventoryService","InventoryDatabase"))||(durable_regions&&path==server::database_path(config,"GridService","GridRegionDatabase")))throw std::runtime_error("Avatar initialization requires a separate database file");
    }
    if(initialize&&local_friends){
        const auto path=server::database_path(config,"FriendsService","FriendsDatabase");
        if(path!=":memory:"&&std::filesystem::exists(framework::utf8_path(path))&&std::filesystem::file_size(framework::utf8_path(path))>0)throw std::runtime_error("Friends initialization requires an empty destination");
        if(path==server::database_path(config,"UserAccountService","UserAccountDatabase")||path==authentication_database||(connected_login&&path==server::database_path(config,"InventoryService","InventoryDatabase"))||(need_grid_user&&path==server::database_path(config,"GridUserService","GridUserDatabase"))||(durable_regions&&path==server::database_path(config,"GridService","GridRegionDatabase"))||(need_avatar&&path==server::database_path(config,"AvatarService","AvatarDatabase")))throw std::runtime_error("Friends initialization requires a separate database file");
    }
    services::AccountStore accounts(server::database_path(config,"UserAccountService","UserAccountDatabase"),
        authentication_database,initialize,true,config.get("UserAccountService","Realm","UserAccounts"),config.get("AuthenticationService","Realm","auth"),local_authentication);
    if(initialize){
        if(local_friends){services::SqliteFriendsStore friends(server::database_path(config,"FriendsService","FriendsDatabase"),true,config.get("FriendsService","Realm","Friends"));}
        if(need_avatar){services::SqliteAvatarStore avatars(server::database_path(config,"AvatarService","AvatarDatabase"),true,config.get("AvatarService","Realm","Avatars"));}
        if(need_grid_user){services::SqliteGridUserPlugin grid_users(services::sqlite_data_source_connection(server::database_path(config,"GridUserService","GridUserDatabase")),config.get("GridUserService","Realm","GridUser"));}
        if(connected_login){services::InventoryFolderStore inventory(server::database_path(config,"InventoryService","InventoryDatabase"),true);}
        if(durable_regions){services::SqliteRegionStore regions(server::database_path(config,"GridService","GridRegionDatabase"),true,config.get("GridService","Realm","regions"));}
        std::cout<<"Initialized selected service databases\n";return 0;
    }
    if(create){
        std::string password;std::cerr<<"Password (read from stdin): ";
        if(!std::getline(std::cin,password))throw std::runtime_error("No password received on stdin");
        auto principal=accounts.create_user(first,last,password,framework::ZeroUuid,level);
        if(remote_authentication)remote_authentication->set_password(framework::Uuid(principal),password);
        std::fill(password.begin(),password.end(),'\0');
        if(connected_login){
            services::InventoryFolderStore store(server::database_path(config,"InventoryService","InventoryDatabase"));
            services::InventoryService inventory(store);
            if(!inventory.create_user_inventory(framework::Uuid(principal)))std::cerr<<"Inventory creation failed for "<<principal<<'\n';
        }
        std::cout<<"Created user "<<principal<<'\n';return 0;
    }
    network::HttpServerOptions options;
    options.host=host.empty()?config.get("NativeServer","ListenAddress","127.0.0.1"):host;
    options.port=port.value_or(config.integer("Network","http_listener_port",9000));
    options.workers=config.integer("NativeServer","HttpWorkers",8);
    options.queued_requests=config.integer("NativeServer","HttpQueuedRequests",64);
    options.timeout_seconds=config.integer("NativeServer","HttpTimeoutSeconds",5);
    const auto max_body=config.integer("NativeServer","MaximumRequestBytes",local_region && config.boolean("NativeServer","EnableRegionAssets",false) ? 16*1024*1024 : 1048576);
    if(max_body<1||max_body>64*1024*1024)throw std::invalid_argument("MaximumRequestBytes must be between 1 and 67108864");
    options.maximum_body=static_cast<std::size_t>(max_body);
    auto resolve=[&](const std::string& key){auto value=config.get("NativeServer",key);if(value.empty())return value;auto path=framework::utf8_path(value);if(path.is_relative())path=config.directory/path;auto u8=path.u8string();return std::string(reinterpret_cast<const char*>(u8.data()),u8.size());};
    options.certificate=resolve("TlsCertificate");options.private_key=resolve("TlsPrivateKey");
    services::LoginService login(accounts,config);
    if(remote_authentication)login.set_authenticator([&](auto principal,auto password,int lifetime,bool){framework::Uuid real;auto token=remote_authentication->authenticate(framework::Uuid(std::string(principal)),std::string(password),lifetime,real);return services::AuthenticationResult{std::move(token),real.str()};});
    server::LoginApplication app(login,config);
    std::unique_ptr<server::AccountEndpoint> account_endpoint;
    std::unique_ptr<server::AuthenticationEndpoint> authentication_endpoint;
    if(config.boolean("NativeServer","EnableAuthenticationEndpoint",false)){
        server::AuthenticationOperations operations;
        operations.authenticate=[&](auto id,auto password,int lifetime)->std::optional<std::string>{return remote_authentication?remote_authentication->authenticate(id,password,lifetime):accounts.authenticate(id.str(),password,lifetime).token;};
        operations.verify=[&](auto id,auto token,int lifetime){return remote_authentication?remote_authentication->verify(id,token,lifetime):accounts.verify(id.str(),token,lifetime);};
        operations.release=[&](auto id,auto token){return remote_authentication?remote_authentication->release(id,token):accounts.release(id.str(),token);};
        operations.set_password=[&](auto id,auto password){return remote_authentication?remote_authentication->set_password(id,password):accounts.set_password(id.str(),password);};
        operations.get_info=[&](auto id){return remote_authentication?remote_authentication->get_auth_info(id):accounts.get_auth_info(id);};
        operations.set_info=[&](auto info){return remote_authentication?remote_authentication->set_auth_info(info):accounts.set_auth_info(info);};
        authentication_endpoint=std::make_unique<server::AuthenticationEndpoint>(std::move(operations),config);app.set_authentication_endpoint(authentication_endpoint.get());
    }
    std::unique_ptr<services::NullPresenceStore> presence_store;
    std::unique_ptr<services::PresenceService> presence;
    std::unique_ptr<server::PresenceEndpoint> presence_endpoint;
    if(connected_login||config.boolean("NativeServer","EnablePresenceEndpoint",false)){
        presence_store=std::make_unique<services::NullPresenceStore>();
        presence=std::make_unique<services::PresenceService>(*presence_store,config.boolean("PresenceService","AllowDuplicatePresences",false));
        if(config.boolean("NativeServer","EnablePresenceEndpoint",false)){presence_endpoint=std::make_unique<server::PresenceEndpoint>(*presence,config);app.set_presence_endpoint(presence_endpoint.get());}
    }
    std::unique_ptr<services::GridUserStore> grid_user_store;
    std::unique_ptr<services::GridUserService> grid_user;
    std::unique_ptr<server::GridUserEndpoint> grid_user_endpoint;
    if(need_grid_user){
        grid_user_store=std::make_unique<services::SqliteGridUserPlugin>(services::sqlite_data_source_connection(server::database_path(config,"GridUserService","GridUserDatabase")),config.get("GridUserService","Realm","GridUser"));
        grid_user=std::make_unique<services::GridUserService>(*grid_user_store);
        if(config.boolean("NativeServer","EnableGridUserEndpoint",false)){grid_user_endpoint=std::make_unique<server::GridUserEndpoint>(*grid_user,config);app.set_grid_user_endpoint(grid_user_endpoint.get());}
    }
    std::unique_ptr<services::RegionStore> region_store;
    std::unique_ptr<services::GridService> grid;
    std::unique_ptr<server::GridEndpoint> grid_endpoint;
    if(need_grid){
        if(durable_regions)region_store=std::make_unique<services::SqliteRegionStore>(server::database_path(config,"GridService","GridRegionDatabase"),false,config.get("GridService","Realm","regions"));else region_store=std::make_unique<services::NullRegionStore>();grid=std::make_unique<services::GridService>(*region_store,config);
        if(config.boolean("NativeServer","EnableGridEndpoint",false)){grid_endpoint=std::make_unique<server::GridEndpoint>(*grid,config,[&config]{return server::grid_extra_features(config);});app.set_grid_endpoint(grid_endpoint.get());}
    }
    std::unique_ptr<services::AvatarStore> avatar_store;
    std::unique_ptr<services::AvatarService> avatars;
    if(need_avatar){avatar_store=std::make_unique<services::SqliteAvatarPlugin>(services::sqlite_data_source_connection(server::database_path(config,"AvatarService","AvatarDatabase")),config.get("AvatarService","Realm","Avatars"));avatars=std::make_unique<services::AvatarService>(*avatar_store);}
    std::unique_ptr<services::FriendsStore> friends_store;
    std::unique_ptr<services::IFriendsService> friends;
    if(local_friends){friends_store=std::make_unique<services::SqliteFriendsPlugin>(services::sqlite_data_source_connection(server::database_path(config,"FriendsService","FriendsDatabase")),config.get("FriendsService","Realm","Friends"));friends=std::make_unique<services::FriendsService>(*friends_store);}
    if(need_friends&&!local_friends)friends=std::make_unique<services::FriendsConnector>(config);
    std::unique_ptr<server::FriendsEndpoint> friends_endpoint;
    if(config.boolean("NativeServer","EnableFriendsEndpoint",false)){
        server::FriendsOperations operations;auto package=[](const std::vector<services::FriendInfo>& infos)->server::FriendsOperations::Friends{std::vector<std::shared_ptr<services::FriendInfo>> result;for(const auto& info:infos)result.push_back(std::make_shared<services::FriendInfo>(info));return result;};
        operations.get=[&,package](auto id){return package(friends->get(id));};operations.get_string=[&,package](const std::string& id){return package(friends->get(std::optional<std::string>(id)));};operations.store=[&](const auto& id,const auto& friend_id,auto flags){return friends->store(id,friend_id,flags);};operations.erase=[&](auto id,const auto& friend_id){return friends->erase(id,friend_id);};operations.erase_string=[&](const std::string& id,const auto& friend_id){return friends->erase(std::optional<std::string>(id),friend_id);};
        friends_endpoint=std::make_unique<server::FriendsEndpoint>(std::move(operations),config);app.set_friends_endpoint(friends_endpoint.get());
    }
    std::unique_ptr<services::InventoryFolderStore> inventory_store;
    std::unique_ptr<services::InventoryService> inventory;
    std::shared_ptr<services::LibraryService> library_service;
    std::unique_ptr<services::LoginOrchestrator> orchestrator;
    std::unique_ptr<services::SimulationConnector> simulation_connector;
    std::unique_ptr<scene::RegionRuntime> region_runtime;
    std::shared_ptr<server::RegistryCapabilityListener> capability_listener;
    server::PollHttpService capability_polls(2);
    std::unique_ptr<services::RegionAssetRuntime> region_assets;
    std::unique_ptr<services::RegionScriptTaskRuntime> region_scripts;
    std::unique_ptr<services::RegionInventoryAssetRuntime> region_inventory_uploads;
    std::unique_ptr<services::RegionBakedTextureRuntime> region_bakes;
    std::unique_ptr<services::RegionInventoryRuntime> region_inventory;
    std::unique_ptr<services::RegionInventoryDescendentsRuntime> region_folders;
    std::unique_ptr<services::RegionLibraryDescendentsRuntime> region_library_folders;
    std::unique_ptr<services::AgentLauncher> agent_launcher;
    std::unique_ptr<services::LoginAgentFactory> agent_factory;
    std::unique_ptr<services::LoginCompletion> login_completion;
    if(connected_login){
        inventory_store=std::make_unique<services::InventoryFolderStore>(server::database_path(config,"InventoryService","InventoryDatabase"));
        inventory=std::make_unique<services::InventoryService>(*inventory_store);
        services::LoginDependencies dependencies;
        dependencies.grid_user=[&](const std::string& user){return grid_user->info(user);};
        dependencies.logged_out=[&](const std::string& user,const framework::Uuid& session,const framework::Uuid& region,const framework::Vector3& position,const framework::Vector3& look){return grid_user->logged_out(user,session,region,position,look);};
        dependencies.inventory=[&](const framework::Uuid& user){return inventory->skeleton(user);};
        if(avatars)dependencies.appearance=[&](const framework::Uuid& id){return avatars->get_appearance(id);};
        dependencies.grid=grid.get();dependencies.presence=presence.get();
        dependencies.recover_duplicate=[&,key=config.get("Messaging","MessageKey")](const framework::Uuid& scope,const framework::Uuid& user,const services::GridUserInfo& info){return services::recover_duplicate_session(*grid,*grid_user,scope,user,info,key);};
        orchestrator=std::make_unique<services::LoginOrchestrator>(std::move(dependencies),config);
        login.set_orchestrator(orchestrator.get());
        if(remote_simulation||local_region){
            if(local_region){
                services::GridRegion region;region.id=framework::Uuid(config.get("NativeRegion","ID","11111111-1111-4111-8111-111111111111"));region.scope=framework::Uuid(config.get("NativeRegion","ScopeID","00000000-0000-0000-0000-000000000000"));region.name=config.get("NativeRegion","Name","Native Region");region.external_host=config.get("NativeRegion","ExternalHost","127.0.0.1");region.estate_owner=framework::Uuid(config.get("NativeRegion","OwnerID","00000000-0000-0000-0000-000000000000"));region.x=config.integer("NativeRegion","LocationX",1000)*256;region.y=config.integer("NativeRegion","LocationY",1000)*256;region.access=13;
                auto udp_port=config.integer("NativeRegion","UDPPort",9000);if(udp_port<0||udp_port>65535)throw std::invalid_argument("NativeRegion UDPPort must be between 0 and 65535");region.internal_endpoint=services::RegionEndpoint{config.get("NativeRegion","BindAddress","0.0.0.0"),static_cast<std::uint16_t>(udp_port)};
                scene::RegionPolicies policies;
                auto runtime_file=[&](const char* key,const std::filesystem::path& original,const std::filesystem::path& installed){auto explicit_file=config.get("NativeRegion",key);if(!explicit_file.empty()){auto path=framework::utf8_path(explicit_file);return path.is_relative()?config.directory/path:path;}for(const auto& relative:{original,installed}){auto path=config.directory/relative;if(std::filesystem::is_regular_file(path))return path;}auto root=executable_directory(args[0]);for(int depth=0;depth<4;++depth){for(const auto& relative:{original,installed}){auto path=root/relative;if(std::filesystem::is_regular_file(path))return path;}root=root.parent_path();}throw std::runtime_error(std::string("Configure NativeRegion.")+key+"; bundled runtime file was not found");};
                policies.animations=std::make_shared<scene::DefaultAnimations>(runtime_file("DefaultAnimationsFile","bin/data/avataranimations.xml","share/opensim-native/avataranimations.xml"));
                if(physical_region){
#ifdef _WIN32
                    const char* native_library="ubode.dll";
#elif defined(__APPLE__)
                    const char* native_library="libubode.dylib";
#elif defined(__aarch64__) || defined(__arm64__)
                    const char* native_library="libubode-arm64.so";
#else
                    const char* native_library="libubode-x86_64.so";
#endif
                    policies.physics_library=runtime_file("PhysicsLibrary",std::filesystem::path("bin/lib64")/native_library,std::filesystem::path("lib/opensim-native")/native_library);
                }
                policies.access=[&,scope=region.scope](auto id,const auto& position,std::string& reason){if(!std::isfinite(position.x)||!std::isfinite(position.y)||!std::isfinite(position.z)){reason="Invalid agent position";return false;}if(!accounts.find_by_id(scope.str(),id.str())){reason="Unknown local account";return false;}reason.clear();return true;};
                policies.authorize=[&,scope=region.scope](const auto& circuit,std::string& reason){if(!accounts.find_by_id(scope.str(),circuit->agent_id.str())){reason="Unknown local account";return false;}reason.clear();return true;};
                scene::LegacyInventoryBackend legacy_inventory;
                legacy_inventory.item=[&](auto owner,auto id){return inventory->item(owner,id);};
                legacy_inventory.content=[&](auto owner,auto id){return inventory->content(owner,id);};
                legacy_inventory.folder=[&](auto owner,auto id){return inventory->folder(owner,id);};
                legacy_inventory.library_folder=[&](auto id)->std::optional<services::InventoryCollection>{return library_service?library_folder_content(library_service->root(),id):std::nullopt;};
                policies.inventory=std::move(legacy_inventory);
                policies.inventory_service=inventory.get();
                policies.library_inventory_item=[&](auto id)->std::optional<services::InventoryItem>{if(!library_service)return {};auto item=library_service->item(id);return item?std::optional(*item):std::nullopt;};
                policies.library_inventory_owner=[&]()->std::optional<framework::Uuid>{if(!library_service)return {};auto root=library_service->root();return root?std::optional(root->owner):std::nullopt;};
                policies.can_rez=policies.access;
                policies.can_edit=[](auto id,const auto& object){return id==object.owner;};
                if(config.boolean("NativeServer","EnableRegionAssets",false)){
                    policies.store_inventory_asset=[&](auto asset){if(!region_assets)throw std::runtime_error("Region asset service unavailable");return region_assets->store(std::move(asset));};
                    policies.store_asset=[&](auto asset){if(!region_assets)throw std::runtime_error("Region asset service unavailable");region_assets->store(std::move(asset));};
                    policies.load_asset=[&](auto id){if(!region_assets)throw std::runtime_error("Region asset service unavailable");return region_assets->get(id);};
                }
                policies.client_flags=[](auto id,const auto& object){return object.flags|(object.inventory_empty?0x800u:0u)|0x10u|(id==object.owner?0x1002012cu:0u);};
                if(auto filename=config.get("NativeRegion","DefaultScriptFile");!filename.empty()){auto path=framework::utf8_path(filename);if(path.is_relative())path=config.directory/path;std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Cannot open NativeRegion default script asset");policies.default_script.assign(std::istreambuf_iterator<char>(input),{});if(!config.boolean("NativeServer","EnableRegionAssets",false))throw std::runtime_error("NativeRegion.DefaultScriptFile requires EnableRegionAssets and a configured asset service");policies.create_script_asset=[&](framework::Uuid agent,const services::InventoryItem& item,const std::string& text){if(!region_assets)throw std::runtime_error("Region asset service unavailable");auto asset=std::make_shared<framework::AssetBase>(framework::Uuid::random(),item.name,std::int8_t(10),agent.str());asset->description(item.description);asset->data=std::make_shared<std::vector<std::uint8_t>>(text.begin(),text.end());region_assets->store(asset);return asset->full_id();};}
                policies.entered=[&](const auto& a){grid_user->set_last_position(a.circuit->agent_id.str(),a.circuit->session_id,region_runtime->descriptor().id,a.position,a.look_at);};
                policies.logged_out=[&](const auto& a){grid_user->logged_out(a.circuit->agent_id.str(),a.circuit->session_id,region_runtime->descriptor().id,a.position,a.look_at);};
                policies.log=[](const auto& text){std::clog<<text<<'\n';};
                region_runtime=std::make_unique<scene::RegionRuntime>(std::move(region),*presence,std::move(policies));
            }else simulation_connector=std::make_unique<services::SimulationConnector>();
            agent_launcher=std::make_unique<services::AgentLauncher>(grid.get(),region_runtime.get(),simulation_connector.get(),nullptr);
            services::LoginAgentFactory::Config urls;for(const auto& pair:config.section("LoginService"))urls.push_back(pair);
            agent_factory=std::make_unique<services::LoginAgentFactory>(std::move(urls),std::nullopt,[&](auto& account){return accounts.store_account(account);});
            services::LoginCompletionDependencies completion;completion.launcher=agent_launcher.get();completion.factory=agent_factory.get();completion.logged_in=[&](const std::string& user)->std::optional<services::GridUserInfo>{return grid_user->logged_in(user);};
            if(friends)completion.friends=[&](const framework::Uuid& id)->std::optional<std::vector<std::shared_ptr<services::LoginFriendInfo>>>{std::vector<std::shared_ptr<services::LoginFriendInfo>> result;for(const auto& item:friends->get(id)){auto friend_info=std::make_shared<services::LoginFriendInfo>();friend_info->friend_id=item.friend_id;friend_info->my_flags=item.my_flags;friend_info->their_flags=item.their_flags;result.push_back(std::move(friend_info));}return result;};
            completion.gestures=[&](const framework::Uuid& id)->std::optional<std::vector<std::shared_ptr<services::InventoryItem>>>{std::vector<std::shared_ptr<services::InventoryItem>> result;for(const auto& item:inventory->active_gestures(id))result.push_back(std::make_shared<services::InventoryItem>(item));return result;};
            auto library_path=framework::utf8_path(config.get("LibraryService","DefaultLibrary","../bin/inventory/Libraries.xml"));if(library_path.is_relative())library_path=config.directory/library_path;auto library_u8=library_path.u8string();library_service=std::make_shared<services::LibraryService>(std::string(reinterpret_cast<const char*>(library_u8.data()),library_u8.size()),config.get("LibraryService","LibraryName","OpenSim Library"));completion.library=services::LoginResponseLibrary::bind(*library_service);
            services::bind_login_response_network(completion.environment);services::bind_login_response_timezones(completion.environment);
            services::LoginCompletionOptions settings;auto read=[&](const char* key,const std::string& fallback=std::string{}){return config.get("LoginService",key,fallback);};settings.welcome=read("WelcomeMessage","Welcome to OpenSim!");for(std::size_t at=0;(at=settings.welcome.find("\\n",at))!=std::string::npos;++at)settings.welcome.replace(at,2,"\n");settings.map_url=read("MapTileURL");if(!framework::trim_utf8(settings.map_url).empty()){settings.map_url=framework::trim_utf8(settings.map_url);if(!settings.map_url.ends_with('/'))settings.map_url+='/';}settings.profile_url=read("ProfileServerURL");settings.openid_url=read("OpenIDServerURL");settings.search_url=read("SearchURL");settings.currency=read("Currency");settings.classified_fee=read("ClassifiedFee");settings.destinations_url=read("DestinationGuide");settings.avatars_url=read("AvatarPicker");settings.dst_zone=read("DSTZone","America/Los_Angeles;Pacific Standard Time");settings.max_agent_groups=config.integer("Groups","MaxAgentGroups",60);
            login_completion=std::make_unique<services::LoginCompletion>(std::move(completion),std::move(settings));login.set_completion(login_completion.get());
        }
    }
    std::unique_ptr<server::AvatarEndpoint> avatar_endpoint;
    if(config.boolean("NativeServer","EnableAvatarEndpoint",false)){
        server::AvatarOperations operations;
        operations.get=[&](auto id){return std::make_shared<services::AvatarData>(avatars->get_avatar(id));};
        operations.set=[&](auto id,auto& value){return avatars->set_avatar(id,value);};
        operations.reset=[&](auto id){return avatars->reset(id);};
        operations.set_items=[&](auto id,const auto& names,const auto& values){return avatars->set_items(id,&names,&values);};
        operations.remove_items=[&](auto id,const auto& names){return avatars->remove_items(id,&names);};
        avatar_endpoint=std::make_unique<server::AvatarEndpoint>(std::move(operations),config);app.set_avatar_endpoint(avatar_endpoint.get());
    }
    std::unique_ptr<services::AppearanceProvisioner> appearance_provisioner;
    std::unique_ptr<services::AccountProvisioner> provisioner;
    if(config.boolean("NativeServer","EnableAccountEndpoint",false)){
        services::AccountProvisioning creation;
        creation.by_name=[&accounts](auto scope,auto first,auto last){auto a=accounts.find(scope.str(),first,last);return a?std::make_shared<services::UserAccount>(std::move(*a)):nullptr;};
        creation.store=[&accounts](auto& account){return accounts.store_account(account);};
        creation.set_password=[&accounts,&remote_authentication](auto id,auto password){return remote_authentication?remote_authentication->set_password(id,password):accounts.set_password(id.str(),password);};
        if(grid)creation.default_regions=[&grid](auto scope){std::vector<std::optional<services::GridRegion>> result;for(auto& region:grid->defaults(scope))result.push_back(std::move(region));return result;};
        if(grid_user)creation.set_home=[&grid_user](auto id,auto region,auto position,auto look){return grid_user->set_home(id,region,position,look);};
        if(inventory){
            creation.create_inventory=[&inventory](auto id){return inventory->create_user_inventory(id);};
            services::AppearanceProvisioning appearance;
            appearance.folder=[&inventory](auto id,int type){auto folder=inventory->for_type(id,type);return folder?std::make_shared<services::InventoryFolder>(*folder):nullptr;};
            appearance.add_item=[&inventory](auto item){return inventory->add_item(*item);};
            appearance.root=[&inventory](auto id){auto folder=inventory->root(id);return folder?std::make_shared<services::InventoryFolder>(*folder):nullptr;};
            appearance.add_folder=[&inventory](auto folder){return inventory->add_folder(*folder);};
            appearance.item=[&inventory](auto principal,auto id){auto item=inventory->item(principal,id);return item?std::make_shared<services::InventoryItem>(*item):nullptr;};
            appearance.by_name=creation.by_name;
            if(avatars){appearance.get_appearance=[&avatars](auto id){return avatars->get_appearance(id);};appearance.set_appearance=[&avatars](auto id,auto value){return avatars->set_appearance(id,value.get());};}
            appearance_provisioner=std::make_unique<services::AppearanceProvisioner>(std::move(appearance));
            creation.default_appearance=[&appearance_provisioner](auto id){appearance_provisioner->create_defaults(id);};
            creation.model_appearance=[&appearance_provisioner](auto id,auto model){appearance_provisioner->establish(id,model);};
        }
        provisioner=std::make_unique<services::AccountProvisioner>(std::move(creation),config.boolean("UserAccountService","CreateDefaultAvatarEntries",false));
        server::AccountOperations operations;
        operations.by_id=[&accounts](auto scope,auto id){return accounts.find_by_id(scope.str(),id.str());};
        operations.by_email=[&accounts](auto scope,auto email){return accounts.find_by_email(scope.str(),email);};
        operations.by_name=[&accounts](auto scope,auto first,auto last){return accounts.find(scope.str(),first,last);};
        operations.search=[&accounts](auto scope,auto query){server::AccountOperations::Accounts result;for(auto& account:accounts.search_accounts(scope.str(),query))result.push_back(std::move(account));return result;};
        // SQLiteUserAccountData.GetUsersWhere returns null in the source.
        operations.multi=[](auto,auto){return server::AccountOperations::Accounts{};};
        operations.store=[&accounts](auto& account){return accounts.store_account(account);};
        operations.create=[&provisioner](auto scope,auto id,auto first,auto last,auto password,auto email,auto model)->server::AccountOperations::Account{auto a=provisioner->create(scope,id,first,last,password,email,model);return a?server::AccountOperations::Account(*a):std::nullopt;};
        account_endpoint=std::make_unique<server::AccountEndpoint>(std::move(operations),config);app.set_account_endpoint(account_endpoint.get());
    }
    std::unique_ptr<network::HttpServer> http;
    std::unique_ptr<network::AsyncHttpServer> region_http;
    ScriptQuiesceGuard script_quiesce{region_runtime.get()};
    if(local_region)region_http=std::make_unique<network::AsyncHttpServer>(options,[&](const auto& request,auto channel){capability_polls.dispatch(*capability_listener,request,std::move(channel),[&](const auto& r){return app.handle(r);});});
    else http=std::make_unique<network::HttpServer>(options,[&app](const network::HttpRequest& request){return app.handle(request);});
    const std::string scheme=options.certificate.empty()?"http":"https";
    std::signal(SIGINT,shutdown_signal);std::signal(SIGTERM,shutdown_signal);
#ifdef SIGBREAK
    std::signal(SIGBREAK,shutdown_signal);
#endif
    auto on_bound=[&](int actual_port){
        app.set_default_login_uri(scheme+"://"+(options.host.find(':')!=std::string::npos?"["+options.host+"]":options.host)+":"+std::to_string(actual_port)+"/");
        if(local_region){
            auto region=region_runtime->descriptor();region.http_port=actual_port;auto advertised=region.external_host.value_or("127.0.0.1");region.set_server_uri(scheme+"://"+(advertised.find(':')!=std::string::npos?"["+advertised+"]":advertised)+":"+std::to_string(actual_port)+"/");
            capability_listener=std::make_shared<server::RegistryCapabilityListener>(server::CapabilityListenerInfo{scheme=="https",static_cast<std::uint32_t>(actual_port),static_cast<std::uint32_t>(actual_port),advertised});
            auto udp=region_runtime->start(capability_listener);region.internal_endpoint=services::RegionEndpoint{udp.address,udp.port};
            if(config.boolean("NativeServer","EnableRegionAssets",false))region_assets=std::make_unique<services::RegionAssetRuntime>(config,region_runtime->capabilities()->shared_events(),region.name,[](const auto& message){std::clog<<message<<'\n';},region.id);
            if(region_assets){region_bakes=std::make_unique<services::RegionBakedTextureRuntime>(region_runtime->capabilities()->shared_events(),region_assets->asset_cache(),config.get("ClientStack.LindenCaps","Cap_UploadBakedTexture"),[](const auto& text){std::clog<<text<<'\n';});services::RegionScriptTaskBackend scripts;
                scripts.object_exists=[&](auto id){return region_runtime->world().object(id).has_value();};
                scripts.can_edit_inventory=[&](auto object,auto agent){return region_runtime->can_edit_task_inventory(object,agent);};
                scripts.can_edit_script=[&](auto item,auto object,auto agent){return region_runtime->can_edit_task_script(item,object,agent);};
                scripts.update=[&](auto agent,auto item,auto object,bool running,auto bytes){return region_runtime->update_task_script(agent,item,object,running,bytes);};
                scripts.log=[](const auto& text){std::clog<<text<<'\n';};
                region_scripts=std::make_unique<services::RegionScriptTaskRuntime>(region_runtime->capabilities()->shared_events(),std::move(scripts));
                services::RegionInventoryAssetBackend uploads;uploads.object_exists=[&](auto id){return region_runtime->world().object(id).has_value();};uploads.can_edit_inventory=[&](auto object,auto agent){return region_runtime->can_edit_task_inventory(object,agent);};uploads.update=[&](auto agent,auto item,auto object,auto bytes){return region_runtime->update_inventory_asset(agent,item,object,bytes);};uploads.log=[](const auto& text){std::clog<<text<<'\n';};uploads.region_name=region.name.value_or("");region_inventory_uploads=std::make_unique<services::RegionInventoryAssetRuntime>(region_runtime->capabilities()->shared_events(),std::move(uploads));
            }
            if(!config.get("ClientStack.LindenCaps","Cap_FetchInventory2").empty()){
                if(!inventory)throw std::runtime_error("Region inventory capabilities require native InventoryService");
                std::optional<services::LibraryItemBackend> library_backend;
                if(library_service){services::LibraryItemBackend backend;if(auto root=library_service->root())backend.root_owner=root->owner;backend.get_items=[library=library_service](const auto& ids)->std::optional<services::FetchInventory2Core::Items>{auto items=library->multiple_items(ids);if(!items)return {};services::FetchInventory2Core::Items result;for(auto& item:*items)result.push_back(item?std::optional(*item):std::nullopt);return result;};library_backend=std::move(backend);}
                region_inventory=std::make_unique<services::RegionInventoryRuntime>(config,region_runtime->capabilities()->shared_events(),[&](auto owner,const auto& ids)->std::optional<services::FetchInventory2Core::Items>{return inventory->multiple_items(owner,ids);},std::move(library_backend));
            }
            if(!config.get("ClientStack.LindenCaps","Cap_FetchInventoryDescendents2").empty()){
                if(!inventory)throw std::runtime_error("Region folder capabilities require native InventoryService");
                services::InventoryDescendentsBackend backend;
                backend.multiple_content=[&](auto owner,const auto& ids)->std::optional<services::InventoryDescendentsBackend::Collections>{services::InventoryDescendentsBackend::Collections result;for(auto& collection:inventory->multiple_content(owner,ids))result.push_back(std::move(collection));return result;};
                backend.folder=[&](auto owner,auto id){return inventory->folder(owner,id);};
                backend.multiple_items=[&](auto owner,const auto& ids)->std::optional<services::FetchInventory2Core::Items>{return inventory->multiple_items(owner,ids);};
                backend.log=[](const auto& message){std::clog<<message<<'\n';};
                if(library_service)if(auto root=library_service->root()){backend.library_owner=root->owner;backend.library_content=[root](auto id){return library_folder_content(root,id);};}
                region_folders=std::make_unique<services::RegionInventoryDescendentsRuntime>(config,region_runtime->capabilities()->shared_events(),std::move(backend));
            }
            if(!config.get("ClientStack.LindenCaps","Cap_FetchLibDescendents2").empty()){
                if(!library_service)throw std::runtime_error("Library folder capabilities require native LibraryService");
                services::LibraryDescendentsBackend backend;
                backend.root_owner=[library=library_service]()->std::optional<framework::Uuid>{auto root=library->root();return root?std::optional(root->owner):std::nullopt;};
                backend.folder_content=[library=library_service](auto id){return library_folder_content(library->root(),id);};
                backend.log=[](const auto& message){std::clog<<message<<'\n';};
                region_library_folders=std::make_unique<services::RegionLibraryDescendentsRuntime>(config,region_runtime->capabilities()->shared_events(),std::move(backend));
            }
            auto error=grid->register_region(region.scope,region);if(!error.empty())throw std::runtime_error("Local region registration failed: "+error);
            capability_polls.start();std::cout<<"REGION_UDP "<<udp.address<<':'<<udp.port<<'\n'<<std::flush;
        }
    };
    const auto bound=region_http?region_http->start(on_bound):http->start(on_bound);
    std::cout<<"LISTENING "<<scheme<<"://"<<(options.host.find(':')!=std::string::npos?"["+options.host+"]":options.host)<<':'<<bound<<"/\n"<<std::flush;
    std::cout<<(local_region?"Experimental local region enabled; full scene features remain incomplete.\n":"Authentication endpoints are running; simulator sessions are not available yet.\n")<<std::flush;
    if(presence_endpoint)std::cout<<"Presence endpoint enabled with the standalone Null provider.\n"<<std::flush;
    if(grid_endpoint)std::cout<<"Grid endpoint enabled with "<<(durable_regions?"SQLite":"volatile Null")<<" region storage.\n"<<std::flush;
    std::string pending;
    while(!shutdown_requested&&(region_http?region_http->is_running():http->is_running())){
        if(console&&console_quit(pending))break;
        if(region_runtime)region_runtime->poll();
        if(region_inventory)region_inventory->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(physical_region?5:50));
    }
    if(region_runtime)region_runtime->quiesce_script_updates();
    capability_polls.stop();if(region_http)region_http->stop();if(http)http->stop();if(region_library_folders)region_library_folders->stop();if(region_folders)region_folders->stop();if(region_inventory)region_inventory->stop();if(region_scripts)region_scripts->stop();if(region_inventory_uploads)region_inventory_uploads->stop();if(region_bakes)region_bakes->stop();if(region_assets)region_assets->stop();if(region_runtime){region_runtime->stop();grid->deregister_region(region_runtime->descriptor().id);}std::cout<<"STOPPED\n"<<std::flush;return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    std::vector<std::string> args;
    for(int i=0;i<argc;++i){const auto size=WideCharToMultiByte(CP_UTF8,0,argv[i],-1,nullptr,0,nullptr,nullptr);std::string text(size,'\0');WideCharToMultiByte(CP_UTF8,0,argv[i],-1,text.data(),size,nullptr,nullptr);text.pop_back();args.push_back(std::move(text));}
    try{return run(args);}catch(const std::exception& error){std::cerr<<"OpenSimNative: "<<error.what()<<'\n';return 1;}
}
#else
int main(int argc,char** argv){try{return run({argv,argv+argc});}catch(const std::exception& error){std::cerr<<"OpenSimNative: "<<error.what()<<'\n';return 1;}}
#endif
