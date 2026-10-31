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
#include "opensim/services/account_store.hpp"
#include "opensim/services/account_service_urls.hpp"
#include "opensim/services/sqlite_user_account_plugin.hpp"
#include "opensim/services/sqlite_authentication_plugin.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/text.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <ctime>

namespace opensim::services {
using namespace opensim::framework;
namespace {
// The native host has no CLR finalizer thread. Collect unreachable provider
// readers at completed service calls so idle requests do not pin file locks.
// Standalone provider callers retain explicit collection/lifetime control.
struct AuthenticationCollection {
    SqliteAuthenticationTable* table;
    ~AuthenticationCollection(){if(table)table->collect_readers();}
};
class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK)
            throw std::runtime_error(std::string("SQLite prepare: ") + sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    void text(int index, std::string_view value) {
        if (sqlite3_bind_text(stmt_,index,value.data(),static_cast<int>(value.size()),SQLITE_TRANSIENT)!=SQLITE_OK)
            throw std::runtime_error("SQLite text binding failed");
    }
    void integer(int index, std::int64_t value) { sqlite3_bind_int64(stmt_,index,value); }
    void nullable_text(int index,const std::optional<std::string>& value){if(value)text(index,*value);else if(sqlite3_bind_null(stmt_,index)!=SQLITE_OK)throw std::runtime_error("SQLite null binding failed");}
    bool row() {
        const auto result=sqlite3_step(stmt_);
        if (result==SQLITE_ROW) return true;
        if (result==SQLITE_DONE) return false;
        throw std::runtime_error(std::string("SQLite step: ") + sqlite3_errmsg(db_));
    }
    std::string text(int col) const {
        const auto* value=sqlite3_column_text(stmt_,col);
        return value ? std::string(reinterpret_cast<const char*>(value),sqlite3_column_bytes(stmt_,col)) : "";
    }
    int integer(int col) const { return sqlite3_column_int(stmt_,col); }
    int columns()const{return sqlite3_column_count(stmt_);}
    std::string name(int col)const{return sqlite3_column_name(stmt_,col);}
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};
void execute(sqlite3* db,const std::string& sql) {
    char* message=nullptr;
    if (sqlite3_exec(db,sql.c_str(),nullptr,nullptr,&message)!=SQLITE_OK) {
        std::string error=message ? message : "SQLite operation failed";
        sqlite3_free(message);
        throw std::runtime_error(error);
    }
}
bool nonempty_file(const std::string& name) {
    return name != ":memory:" && std::filesystem::exists(utf8_path(name)) &&
        std::filesystem::file_size(utf8_path(name)) > 0;
}
std::string account_columns(bool country){return "PrincipalID,ScopeID,FirstName,LastName,UserLevel,UserFlags,ServiceURLs,Email,Created,UserTitle,"+std::string(country?"UserCountry":"''");}
UserAccount read_account(Statement& query){
    Uuid principal,scope;try_parse_sdk_uuid(query.text(0),principal);try_parse_sdk_uuid(query.text(1),scope);
    UserAccount account{principal.str(),scope.str(),query.text(2),query.text(3),0,0};
    parse_account_integer(query.text(4),account.user_level);parse_account_integer(query.text(5),account.user_flags);
    account.email=query.text(7);account.created=account_integer(query.text(8));account.user_title=query.text(9);account.user_country=query.text(10);
    account.service_urls=decode_account_service_urls(query.text(6));return account;
}
UserAccount read_account(const UserAccountRecord& row){
 if(!row.data)throw std::invalid_argument("Null account Data");auto& fields=*row.data;UserAccount result;
 result.principal_id=row.principal.str();result.scope_id=row.scope.str();result.first_name=row.first;result.last_name=row.last;
 auto value=[&](const std::string& key){auto i=fields.find(key);return i==fields.end()?std::optional<std::string>{}:i->second;};
 result.email=value("Email").value_or("");result.user_title=value("UserTitle").value_or("");result.user_country=value("UserCountry").value_or("");
 result.created=account_integer(fields.at("Created").value());if(auto level=value("UserLevel"))parse_account_integer(*level,result.user_level);if(auto flags=value("UserFlags"))parse_account_integer(*flags,result.user_flags);
 result.service_urls=decode_account_service_urls(value("ServiceURLs").value_or(""));return result;
}

}
AccountStore::AccountStore(const std::string& accounts, const std::string& auth, bool initialize, bool source_accounts, std::string account_realm, std::string auth_realm, bool local_authentication) {
    if (accounts.empty() || (local_authentication&&auth.empty())) throw std::invalid_argument("Database paths must not be empty");
    if (initialize && (nonempty_file(accounts)||(local_authentication&&nonempty_file(auth))))
        throw std::runtime_error("Initialization refuses nonempty databases; use an empty destination");
    const int flags=SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | ((initialize||source_accounts) ? SQLITE_OPEN_CREATE : 0);
    if (sqlite3_open_v2(accounts.c_str(),&db_,flags,nullptr)!=SQLITE_OK) {
        const std::string error=db_ ? sqlite3_errmsg(db_) : "SQLite allocation failed";
        if (db_) sqlite3_close(db_);
        db_=nullptr;
        throw std::runtime_error(error);
    }
    try {
        sqlite3_busy_timeout(db_,5000);
        if(!source_accounts)sqlite3_limit(db_,SQLITE_LIMIT_LENGTH,1024*1024);
        if(source_accounts){Migration migration(sqlite_migration_database(db_),user_account_store_migrations(),std::string("UserAccount"));migration.update();auto borrowed=SqliteDatabase::borrow(db_);account_table_=std::make_unique<SqliteUserAccountTable>([borrowed]{return borrowed;},std::move(account_realm));}
        if(source_accounts&&local_authentication){
            // Each source provider migrates its own selected database before
            // serving requests, including when both stores share one file.
            if(accounts==auth){Migration migration(sqlite_migration_database(db_),auth_store_migrations(),std::string("AuthStore"));migration.update();}
            else{SqliteDatabase authentication;authentication.open_path(auth,true);Migration migration(sqlite_migration_database(authentication.handle()),auth_store_migrations(),std::string("AuthStore"));migration.update();}
        }
        if(!local_authentication)auth_schema_.clear();
        else if (accounts==auth) auth_schema_="main";
        else {
            if (!initialize && !std::filesystem::exists(utf8_path(auth)))
                throw std::runtime_error("Authentication database does not exist");
            Statement attach(db_,"ATTACH DATABASE ? AS authentication");
            attach.text(1,auth); attach.row(); auth_schema_="authentication";
        }
        if (initialize && !source_accounts) {
            execute(db_,"BEGIN IMMEDIATE");
            // Same current columns as SQLite/Resources/UserAccount.migrations.
            if(!source_accounts)execute(db_,"CREATE TABLE UserAccounts (PrincipalID CHAR(36) PRIMARY KEY, ScopeID CHAR(36) NOT NULL,"
                "FirstName VARCHAR(64) NOT NULL,LastName VARCHAR(64) NOT NULL,Email VARCHAR(64),ServiceURLs TEXT,Created INT,"
                "UserLevel INTEGER NOT NULL DEFAULT 0,UserFlags INTEGER NOT NULL DEFAULT 0,UserTitle VARCHAR(64) NOT NULL DEFAULT '',"
                "active BOOLEAN NOT NULL DEFAULT TRUE)");
            if(local_authentication){execute(db_,"CREATE TABLE "+auth_schema_+".auth (UUID CHAR(36) PRIMARY KEY NOT NULL,passwordHash CHAR(32) NOT NULL DEFAULT '',"
                "passwordSalt CHAR(32) NOT NULL DEFAULT '',webLoginKey VARCHAR(255) NOT NULL DEFAULT '',"
                "accountType VARCHAR(32) NOT NULL DEFAULT 'UserAccount')");
            execute(db_,"CREATE TABLE "+auth_schema_+".tokens (UUID CHAR(36) NOT NULL,token VARCHAR(255) NOT NULL,validity DATETIME NOT NULL)");}
            execute(db_,"COMMIT");
        }
        // Source providers defer column access to query materialization.
        if(!source_accounts){Statement accounts_check(db_,"SELECT PrincipalID,ScopeID,FirstName,LastName,UserLevel,UserFlags,ServiceURLs FROM UserAccounts LIMIT 0");}
        if(source_accounts&&local_authentication){auto borrowed=SqliteDatabase::borrow(db_);authentication_table_=std::make_unique<SqliteAuthenticationTable>([borrowed]{return borrowed;},std::move(auth_realm),SqliteAuthenticationTable::Clock{},SqliteAuthenticationTable::Diagnostic{},auth_schema_);}
        else if(local_authentication){Statement auth_check(db_,"SELECT UUID,passwordHash,passwordSalt,webLoginKey FROM "+auth_schema_+".auth LIMIT 0");Statement tokens_check(db_,"SELECT UUID,token,validity FROM "+auth_schema_+".tokens LIMIT 0");}
        Statement columns(db_,"PRAGMA table_info(UserAccounts)");while(columns.row())if(columns.text(1)=="UserCountry")has_country_=true;
    } catch (...) { authentication_table_.reset();account_table_.reset();sqlite3_close(db_); db_=nullptr; throw; }
}
AccountStore::~AccountStore() { authentication_table_.reset();account_table_.reset();if (db_) sqlite3_close(db_); }
std::optional<UserAccount> AccountStore::find(std::string_view scope,std::string_view first,std::string_view last) {
    return find_fields(scope,{{"FirstName",std::string(first)},{"LastName",std::string(last)}});
}
std::optional<UserAccount> AccountStore::find_by_email(std::string_view scope,std::string_view email){
    return find_fields(scope,{{"Email",std::string(email)}});
}
std::optional<UserAccount> AccountStore::find_by_id(std::string_view scope,std::string_view principal){
    return find_fields(scope,{{"PrincipalID",normalize_uuid(principal)}});
}
std::optional<UserAccount> AccountStore::find_fields(std::string_view scope,const std::vector<std::pair<std::string,std::string>>& fields) {
    std::lock_guard lock(mutex_);
    if(account_table_){SqliteTableStrings names,values;const auto normalized=normalize_uuid(scope);if(normalized!=ZeroUuid){names.push_back("ScopeID");values.push_back(normalized);}for(auto& [field,value]:fields){names.push_back(field);values.push_back(value);}auto rows=account_table_->get_fields(&names,&values);if(rows.empty()&&normalized!=ZeroUuid){values[0]=std::string(ZeroUuid);rows=account_table_->get_fields(&names,&values);}return rows.empty()?std::nullopt:std::optional{read_account(*rows.front())};}
    const bool scoped=scope!=ZeroUuid;
    std::string where;for(const auto& [field,value]:fields){if(!where.empty())where+=" AND ";where+=field+"=?";}
    Statement query(db_,"SELECT "+account_columns(has_country_)+" FROM UserAccounts WHERE "+where +
        std::string(scoped ? " AND (ScopeID=? OR ScopeID=?) ORDER BY CASE WHEN ScopeID=? THEN 0 ELSE 1 END" : "") + " LIMIT 1");
    int index=1;for(const auto& [field,value]:fields)query.text(index++,value);
    if (scoped) { query.text(index++,scope); query.text(index++,ZeroUuid); query.text(index,scope); }
    if (!query.row()) return std::nullopt;
    return read_account(query);
}
std::vector<UserAccount> AccountStore::search_accounts(std::string_view scope,std::string_view text){
    if(account_table_){std::lock_guard lock(mutex_);auto rows=account_table_->get_users(Uuid(normalize_uuid(scope)),trim_utf8(std::string(text)));std::vector<UserAccount> result;for(auto& row:rows)result.push_back(read_account(*row));return result;}
    const auto trimmed=trim_utf8(std::string(text));std::vector<std::string> words;std::size_t start=0;
    while(start<trimmed.size()){auto end=trimmed.find(' ',start);if(end==std::string::npos)end=trimmed.size();if(end!=start)words.push_back(trimmed.substr(start,end-start));start=end+1;}
    // Preserve the original array-shift loop: after a removal it skips the
    // shifted word, so adjacent short terms are not all discarded.
    for(std::size_t i=0;i<words.size();++i)if(utf16_replacing(words[i]).size()<3)words.erase(words.begin()+static_cast<std::ptrdiff_t>(i));
    if(words.empty()||words.size()>2)return {};
    const auto normalized_scope=normalize_uuid(scope);
    // SQLiteUserAccountData constructs this expression directly. Keep its
    // wildcard, two-term OR and malformed-quote behavior for source parity.
    const auto sql="SELECT "+account_columns(has_country_)+" FROM UserAccounts WHERE (ScopeID='"+normalized_scope+"' OR ScopeID='00000000-0000-0000-0000-000000000000') AND (FirstName LIKE '"+words[0]+"%' OR LastName LIKE '"+words[words.size()-1]+"%')";
    std::lock_guard lock(mutex_);Statement query(db_,sql);std::vector<UserAccount> result;while(query.row())result.push_back(read_account(query));return result;
}
bool AccountStore::store_service_urls(const UserAccount& account){
    if(account_table_)return store_account(account);
    const auto principal=normalize_uuid(account.principal_id);
    auto encoded=encode_account_service_urls(account.service_urls);
    std::lock_guard lock(mutex_);Statement update(db_,"UPDATE UserAccounts SET ServiceURLs=? WHERE PrincipalID=?");
    update.text(1,encoded);update.text(2,principal);update.row();return sqlite3_changes(db_)>0;
}
bool AccountStore::store_account(const UserAccount& account){
    // The service creates the record before serializing URL objects. Callbacks
    // may mutate the original account; the captured metadata must stay fixed.
    const auto principal=normalize_uuid(account.principal_id),scope=normalize_uuid(account.scope_id);
    const auto first=account.first_name,last=account.last_name,email=account.email,title=account.user_title,country=account.user_country;
    const auto created=account.created,level=account.user_level,flags=account.user_flags;
    auto urls=encode_account_service_urls(account.service_urls);
    if(account_table_){auto row=std::make_shared<UserAccountRecord>();row->principal=Uuid(principal);row->scope=Uuid(scope);row->first=first;row->last=last;row->data=std::make_shared<SqliteTableFields>();auto& data=*row->data;data["Email"]=email;data["Created"]=std::to_string(created);data["UserLevel"]=std::to_string(level);data["UserFlags"]=std::to_string(flags);if(title&&!title->empty())data["UserTitle"]=title;if(country&&!country->empty())data["UserCountry"]=country;data["ServiceURLs"]=std::move(urls);std::lock_guard lock(mutex_);return account_table_->store(row);}
    if(!first||!last)throw std::invalid_argument("Null account row name");
    const bool with_title=title&&!title->empty(),with_country=country&&!country->empty();
    std::lock_guard lock(mutex_);
    std::string sql="REPLACE INTO UserAccounts (PrincipalID,ScopeID,FirstName,LastName,Email,Created,UserLevel,UserFlags,ServiceURLs";
    if(with_title)sql+=",UserTitle";if(with_country)sql+=",UserCountry";sql+=") VALUES (?,?,?,?,?,?,?,?,?";
    if(with_title)sql+=",?";if(with_country)sql+=",?";sql+=")";
    Statement row(db_,sql);row.text(1,principal);row.text(2,scope);row.text(3,*first);row.text(4,*last);row.nullable_text(5,email);row.integer(6,created);row.integer(7,level);row.integer(8,flags);row.text(9,urls);
    int index=10;if(with_title)row.text(index++,*title);if(with_country)row.text(index,*country);row.row();return sqlite3_changes(db_)>0;
}
bool AccountStore::set_password(std::string_view principal,const std::optional<std::string>& password,std::function<Uuid()> random){
    if(auth_schema_.empty())return false;
    AuthenticationCollection collection{authentication_table_.get()};
    const auto id=normalize_uuid(principal),salt=md5((random?random():Uuid::random()).str());
    if(!password)throw std::invalid_argument("Null password");
    const auto hash=md5(md5(*password)+":"+salt);
    if(authentication_table_){std::lock_guard lock(mutex_);auto record=authentication_table_->get(Uuid(id));if(!record){record=std::make_shared<AuthenticationRecord>();record->principal=Uuid(id);record->data=std::make_shared<OrderedDictionary<std::string,std::any>>();(*record->data)["accountType"]=std::string("UserAccount");(*record->data)["webLoginKey"]=std::string(ZeroUuid);}(*record->data)["passwordHash"]=hash;(*record->data)["passwordSalt"]=salt;return authentication_table_->store(record);}
    std::lock_guard lock(mutex_);std::vector<std::pair<std::string,std::string>> values;bool exists=false;
    {
        Statement query(db_,"SELECT * FROM "+auth_schema_+".auth WHERE UUID=?");query.text(1,id);exists=query.row();
        if(exists)for(int i=0;i<query.columns();++i)if(query.name(i)!="UUID")values.push_back({query.name(i),query.text(i)});
    }
    if(!exists)values={{"accountType","UserAccount"},{"webLoginKey",std::string(ZeroUuid)}};
    for(const auto& item:{std::pair{"passwordHash",hash},std::pair{"passwordSalt",salt}}){auto i=std::find_if(values.begin(),values.end(),[&](auto& value){return value.first==item.first;});if(i==values.end())values.push_back(item);else i->second=item.second;}
    try{
        std::string sql;if(exists){sql="UPDATE "+auth_schema_+".auth SET ";for(const auto& [key,value]:values){if(sql.back()!=' ')sql+=",";sql+="`"+key+"`=?";}sql+=" WHERE UUID=?";}
        else{sql="INSERT INTO "+auth_schema_+".auth (";for(const auto& [key,value]:values){if(sql.back()!='(')sql+=",";sql+="`"+key+"`";}sql+=",UUID) VALUES (";for(std::size_t i=0;i<=values.size();++i){if(i)sql+=",";sql+="?";}sql+=")";}
        Statement row(db_,sql);int index=1;for(const auto& [key,value]:values)row.text(index++,value);row.text(index,id);row.row();return sqlite3_changes(db_)>0;
    }catch(const std::runtime_error&){return false;}
}
std::string AccountStore::create_user(std::string_view first,std::string_view last,std::string_view password,std::string_view scope,int level) {
    std::lock_guard lock(mutex_);
    AuthenticationCollection collection{authentication_table_.get()};
    if (first.empty()||last.empty()||password.empty()||first.size()>64||last.size()>64)
        throw std::invalid_argument("User names and password must be nonempty; names are limited to 64 UTF-8 bytes");
    const auto normalized_scope=normalize_uuid(scope);
    execute(db_,"BEGIN IMMEDIATE");
    try {
        if (find(normalized_scope,first,last)) throw std::invalid_argument("User already exists");
        auto principal=random_uuid(),salt=md5(random_uuid());
        if(account_table_){auto row=std::make_shared<UserAccountRecord>();row->principal=Uuid(principal);row->scope=Uuid(normalized_scope);row->first=std::string(first);row->last=std::string(last);row->data=std::make_shared<SqliteTableFields>();(*row->data)["Email"]="";(*row->data)["ServiceURLs"]="";(*row->data)["Created"]=std::to_string(std::time(nullptr));(*row->data)["UserLevel"]=std::to_string(level);(*row->data)["UserFlags"]="0";account_table_->store(row);}
        else{
            Statement account(db_,"INSERT INTO UserAccounts (PrincipalID,ScopeID,FirstName,LastName,Email,ServiceURLs,Created,UserLevel) VALUES (?,?,?,?,?,?,?,?)");
            account.text(1,principal);account.text(2,normalized_scope);account.text(3,first);account.text(4,last);
            account.text(5,"");account.text(6,"");account.integer(7,std::time(nullptr));account.integer(8,level);account.row();
        }
        if(authentication_table_){auto record=std::make_shared<AuthenticationRecord>();record->principal=Uuid(principal);record->data=std::make_shared<OrderedDictionary<std::string,std::any>>();auto& data=*record->data;data["passwordHash"]=md5(md5(password)+":"+salt);data["passwordSalt"]=salt;data["webLoginKey"]=std::string(ZeroUuid);data["accountType"]=std::string("UserAccount");if(!authentication_table_->store(record))throw std::runtime_error("Authentication record store failed");}
        else if(!auth_schema_.empty()){Statement auth(db_,"INSERT INTO "+auth_schema_+".auth (UUID,passwordHash,passwordSalt,webLoginKey) VALUES (?,?,?,?)");auth.text(1,principal); auth.text(2,md5(md5(password)+":"+salt)); auth.text(3,salt); auth.text(4,ZeroUuid); auth.row();}
        execute(db_,"COMMIT"); return principal;
    } catch (...) { execute(db_,"ROLLBACK"); throw; }
}
AuthenticationResult AccountStore::authenticate(std::string_view principal,std::string_view password,int lifetime,bool web_keys) {
    if(auth_schema_.empty())return {};
    std::lock_guard lock(mutex_);
    AuthenticationCollection collection{authentication_table_.get()};
    if(authentication_table_){
        const Uuid id{normalize_uuid(principal)};auto record=authentication_table_->get(id);if(!record)return {};
        auto field=[&](const std::string& name){return std::any_cast<std::string>(record->data->at(name));};
        bool accepted=false;
        if(web_keys&&password!=ZeroUuid&&constant_time_equal(field("webLoginKey"),password)){try{accepted=normalize_uuid(password)!=ZeroUuid;}catch(const std::invalid_argument&){}if(accepted){(*record->data)["webLoginKey"]=std::string(ZeroUuid);authentication_table_->store(record);}}
        if(!accepted)accepted=constant_time_equal(field("passwordHash"),md5(std::string(password)+":"+field("passwordSalt")));
        if(!accepted)return {};auto token=random_uuid();return authentication_table_->set_token(id,token,lifetime)?AuthenticationResult{token,std::string(ZeroUuid)}:AuthenticationResult{};
    }
    execute(db_,"BEGIN IMMEDIATE");
    try {
        bool accepted=false,used_web_key=false;
        {
            Statement query(db_,"SELECT passwordHash,passwordSalt,webLoginKey FROM "+auth_schema_+".auth WHERE UUID=?");
            query.text(1,principal);
            if (query.row()) {
                if (web_keys && password!=ZeroUuid && constant_time_equal(query.text(2),password)) {
                    // WebkeyAuthenticationService also requires the supplied key to parse as a UUID.
                    try { accepted=normalize_uuid(password)!=ZeroUuid; } catch (const std::invalid_argument&) { }
                    used_web_key=accepted;
                }
                if (!accepted) accepted=constant_time_equal(query.text(0),md5(std::string(password)+":"+query.text(1)));
            }
        }
        if (!accepted) { execute(db_,"ROLLBACK"); return {}; }
        if (used_web_key) {
            Statement reset(db_,"UPDATE "+auth_schema_+".auth SET webLoginKey=? WHERE UUID=?");
            reset.text(1,ZeroUuid); reset.text(2,principal); reset.row();
        }
        execute(db_,"DELETE FROM "+auth_schema_+".tokens WHERE validity < datetime('now','localtime')");
        auto token=random_uuid();
        Statement insert(db_,"INSERT INTO "+auth_schema_+".tokens (UUID,token,validity) VALUES (?,?,datetime('now','localtime',?))");
        insert.text(1,principal); insert.text(2,token); insert.text(3,std::to_string(lifetime)+" minutes"); insert.row();
        execute(db_,"COMMIT"); return {token,std::string(ZeroUuid)};
    } catch (...) { execute(db_,"ROLLBACK"); throw; }
}
bool AccountStore::verify(std::string_view principal,std::string_view token,int lifetime) {
    if(auth_schema_.empty())return false;
    std::lock_guard lock(mutex_);
    if(authentication_table_)return authentication_table_->check_token(Uuid(normalize_uuid(principal)),std::string(token),lifetime);
    Statement update(db_,"UPDATE "+auth_schema_+".tokens SET validity=datetime('now','localtime',?) WHERE UUID=? AND token=? AND validity>datetime('now','localtime')");
    update.text(1,std::to_string(lifetime)+" minutes"); update.text(2,principal); update.text(3,token); update.row();
    return sqlite3_changes(db_)>0;
}
bool AccountStore::release(std::string_view principal,std::string_view token) { return verify(principal,token,0); }
std::shared_ptr<AuthInfo> AccountStore::get_auth_info(Uuid principal){
    if(auth_schema_.empty())return {};
    std::lock_guard lock(mutex_);auto borrowed=SqliteDatabase::borrow(db_);SqliteAuthenticationTable fallback([borrowed]{return borrowed;},"auth",{},{},auth_schema_);
    auto* table=authentication_table_?authentication_table_.get():&fallback;AuthenticationCollection collection{table};return authentication_info(table->get(principal));
}
bool AccountStore::set_auth_info(const std::shared_ptr<AuthInfo>& info){
    if(auth_schema_.empty())return false;
    std::lock_guard lock(mutex_);auto record=authentication_record(info);auto borrowed=SqliteDatabase::borrow(db_);SqliteAuthenticationTable fallback([borrowed]{return borrowed;},"auth",{},{},auth_schema_);
    auto* table=authentication_table_?authentication_table_.get():&fallback;AuthenticationCollection collection{table};return table->store(record);
}
}
