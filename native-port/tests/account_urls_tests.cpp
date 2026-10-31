#include "opensim/services/login_agent_factory.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/identity.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace opensim;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string path_text(const std::filesystem::path& p){auto text=p.u8string();return {text.begin(),text.end()};}
struct Files {
    std::filesystem::path root=std::filesystem::temp_directory_path()/framework::utf8_path("account-urls-"+framework::Uuid::random().str()+"-\xe8\xa9\xa6\xe9\xa8\x93");
    std::filesystem::path accounts=root/"accounts.db",auth=root/"auth.db";
    Files(){std::filesystem::create_directory(root);}
    ~Files(){std::error_code error;std::filesystem::remove(accounts,error);std::filesystem::remove(auth,error);std::filesystem::remove(root,error);}
};
struct Raw {
    sqlite3* db=nullptr;
    explicit Raw(const std::string& path){if(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)!=SQLITE_OK)throw std::runtime_error("Raw account database open failed");}
    ~Raw(){sqlite3_close(db);}
    void execute(const char* sql){if(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));}
    std::string scalar(const char* sql){sqlite3_stmt* s=nullptr;if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db));std::string result;if(sqlite3_step(s)==SQLITE_ROW){auto p=sqlite3_column_text(s,0);if(p)result.assign(reinterpret_cast<const char*>(p),sqlite3_column_bytes(s,0));}sqlite3_finalize(s);return result;}
};
int main(){try{
    Files files;const auto accounts=path_text(files.accounts),auth=path_text(files.auth);std::string principal,token,encoded;
    {
        services::AccountStore store(accounts,auth,true);principal=store.create_user("Url","User","secret",framework::ZeroUuid,7);
        Raw raw(accounts);raw.execute("UPDATE UserAccounts SET Email='kept@example',UserTitle='Kept',UserFlags=123,active=0,ServiceURLs=NULL");
        auto user=store.find(framework::ZeroUuid,"Url","User");check(user&&user->service_urls&&user->service_urls->empty(),"SQL NULL loads as an empty URL dictionary");
        auto auth_result=store.authenticate(principal,framework::md5("secret"),30);check(static_cast<bool>(auth_result),"Authentication before URL update");token=auth_result.token;
        int persisted=0;services::LoginAgentFactory factory({{"SRV_InventoryServerURI","https://inventory/a?x=1&y=two words"},{"SRV_試験","https://unicode"}},std::nullopt,[&](auto& changed){++persisted;return store.store_service_urls(changed);});
        services::LoginAgentDetails details;details.session=framework::Uuid::random();details.secure_session=framework::Uuid::random();
        auto circuit=factory.make_agent(*user,nullptr,details);check(circuit&&persisted==1,"Factory URL override persisted through the real account store");
        encoded=raw.scalar("SELECT ServiceURLs FROM UserAccounts");check(encoded.find("two+words%2f")!=std::string::npos&&encoded.find("%e8%a9%a6%e9%a8%93")!=std::string::npos,"Original form encoding stored in SQLite");
        check(raw.scalar("SELECT Email||':'||UserTitle||':'||UserFlags||':'||active FROM UserAccounts")=="kept@example:Kept:123:0","Unrelated account fields preserved");
        check(store.verify(principal,token,30),"Authentication token preserved by URL persistence");
        auto missing=*user;missing.principal_id=framework::Uuid::random().str();check(!store.store_service_urls(missing),"Missing account update reports failure");
        (*user->service_urls)["invalid"]=std::any{};bool rejected=false;try{store.store_service_urls(*user);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Null URL objects reject serialization like the source");
        check(raw.scalar("SELECT ServiceURLs FROM UserAccounts")==encoded,"Failed serialization leaves stored URLs unchanged");
        user->service_urls->erase("invalid");
        (*user->service_urls)["Identity"]=framework::ServiceUrlStringifiable{[&user]{user->principal_id=framework::Uuid::random().str();return "captured";}};
        check(store.store_service_urls(*user),"Record identity captured before object serialization callbacks");
        check(store.find(framework::ZeroUuid,"Url","User")->principal_id==principal,"Callback identity mutation does not retarget the stored record");
    }
    {
        services::AccountStore store(accounts,auth);auto user=store.find(framework::ZeroUuid,"Url","User");check(user&&user->user_level==7&&user->user_flags==123,"Account metadata survives reopening");
        check(std::any_cast<std::string>(user->service_urls->at("InventoryServerURI"))=="https://inventory/a?x=1&y=two words/","URL survives SQLite close and reopen");
        check(store.verify(principal,token,30),"Token survives reopening account/auth databases");
        int writes=0;services::LoginAgentFactory factory({{"SRV_InventoryServerURI","https://inventory/a?x=1&y=two words"},{"SRV_試験","https://unicode"}},std::nullopt,[&](auto& changed){++writes;return store.store_service_urls(changed);});
        services::LoginAgentDetails details;factory.make_agent(*user,nullptr,details);check(writes==0,"Unchanged persisted overrides do not cause another write");
    }
    {
        Files full;const auto database=path_text(full.accounts),authentication=path_text(full.auth);
        std::string id;
        {
            services::AccountStore store(database,authentication,true);
            id=store.create_user("Full","Account","secret",framework::ZeroUuid,9);
            Raw raw(database);raw.execute("UPDATE UserAccounts SET Email=NULL,UserTitle='Old',active=0,UserFlags=17");
            auto account=store.find(framework::ZeroUuid,"Full","Account");
            check(account&&account->email==""&&account->user_title=="Old"&&account->created>0,"Full metadata loaded with source null conversion");
            auto credentials=store.authenticate(id,framework::md5("secret"),30);check(static_cast<bool>(credentials),"Full store authentication fixture");
            Raw auth_raw(authentication);auth_raw.execute("ALTER TABLE auth ADD COLUMN Custom TEXT");auth_raw.execute("UPDATE auth SET accountType='ServiceAccount',webLoginKey='00000000-0000-0000-0000-000000000008'");
            const auto salt_id=framework::Uuid("00000000-0000-0000-0000-000000000007");
            check(store.set_password(id,std::string("changed"),[&]{return salt_id;}),"Existing authentication password replaced");
            const auto salt=framework::md5(salt_id.str());
            check(auth_raw.scalar("SELECT passwordHash FROM auth")==framework::md5(framework::md5("changed")+":"+salt),"Password uses source double MD5 and UUID salt");
            check(auth_raw.scalar("SELECT accountType||':'||webLoginKey||':'||typeof(Custom) FROM auth")=="ServiceAccount:00000000-0000-0000-0000-000000000008:text","Password replacement preserves auth metadata and source null-to-empty conversion");
            check(store.verify(id,credentials.token,30),"Password replacement preserves existing tokens");
            auto standalone=framework::Uuid::random().str();check(store.set_password(standalone,std::string{},[&]{return salt_id;}),"Password creation accepts empty password without an account row");
            check(static_cast<bool>(store.authenticate(standalone,framework::md5(""),30)),"Created password authenticates");
            const auto before_null=auth_raw.scalar("SELECT passwordHash FROM auth WHERE accountType='ServiceAccount'");int random_calls=0;bool null_rejected=false;
            try{store.set_password(id,std::nullopt,[&]{++random_calls;return salt_id;});}catch(const std::invalid_argument&){null_rejected=true;}
            check(null_rejected&&random_calls==1&&auth_raw.scalar("SELECT passwordHash FROM auth WHERE accountType='ServiceAccount'")==before_null,"Null password fails after salt generation without changing authentication");
            auth_raw.execute("CREATE TRIGGER reject_password BEFORE UPDATE ON auth BEGIN SELECT RAISE(ABORT,'rejected');END");
            check(!store.set_password(id,std::string("rejected")),"Authentication provider write errors return false");
            account->email=std::nullopt;account->user_title="";account->created=123;account->user_level=-4;account->user_flags=2147483647;
            services::LoginAgentFactory factory({{"SRV_InventoryServerURI","https://inventory"}},std::nullopt,[&](auto& changed){return store.store_account(changed);});
            services::LoginAgentDetails details;factory.make_agent(*account,nullptr,details);
            check(raw.scalar("SELECT active||':'||UserTitle||':'||typeof(Email)||':'||Created||':'||UserLevel||':'||UserFlags FROM UserAccounts")=="1::null:123:-4:2147483647","Full replacement resets omitted defaults and binds nullable email");
            check(store.verify(id,credentials.token,30),"Full replacement preserves authentication tokens");
            (*account->service_urls)["callback"]=framework::ServiceUrlStringifiable{[&]{account->first_name="Mutated";account->created=999;return "value";}};
            check(store.store_account(*account),"Full replacement accepts URL callbacks");
            auto captured=store.find(framework::ZeroUuid,"Full","Account");check(captured&&captured->created==123,"Full replacement snapshots metadata before callbacks");
            account=*captured;account->first_name=std::nullopt;bool rejected=false;try{store.store_account(*account);}catch(const std::invalid_argument&){rejected=true;}
            check(rejected&&store.find(framework::ZeroUuid,"Full","Account").has_value(),"Null row names fail without removing existing account");
            account=*captured;account->user_country="CA";rejected=false;try{store.store_account(*account);}catch(const std::runtime_error&){rejected=true;}
            check(rejected,"Original schema rejects nonempty unsupported country column");
            raw.execute("ALTER TABLE UserAccounts ADD COLUMN UserCountry TEXT DEFAULT ''");
            check(store.store_account(*account),"Extended schema accepts country values");
            services::UserAccount fresh(framework::Uuid(framework::ZeroUuid),framework::Uuid::random(),"New","Account","new@example",[]{return 456;});
            check(store.store_account(fresh),"Full store inserts new accounts");
            check(!store.authenticate(fresh.principal_id,framework::md5("secret"),30),"Account insertion does not invent authentication credentials");
        }
        {
            services::AccountStore store(database,authentication);auto account=store.find(framework::ZeroUuid,"Full","Account");
            check(account&&account->principal_id==id&&account->created==123&&account->user_country=="CA"&&account->email=="","Full metadata survives reopen");
            check(store.find(framework::ZeroUuid,"New","Account")->created==456,"Inserted account survives reopen");
        }
    }
    std::cout<<"PASS: SQLite account URLs, full record replacement, factory persistence, metadata and auth isolation\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
