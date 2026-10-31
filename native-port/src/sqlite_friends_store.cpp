// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/friends_service.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/services/sqlite_generic_connection.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <mutex>
#include <algorithm>
namespace opensim::services {
namespace {
class Statement {
    sqlite3* db_;sqlite3_stmt* value_=nullptr;
public:
    Statement(sqlite3* db,const std::string& sql):db_(db){if(sqlite3_prepare_v2(db,sql.c_str(),-1,&value_,nullptr)!=SQLITE_OK){auto error=std::string(sqlite3_errmsg(db));if(value_)sqlite3_finalize(value_);throw std::runtime_error(error);}}
    ~Statement(){sqlite3_finalize(value_);}
    bool row(){auto result=sqlite3_step(value_);if(result==SQLITE_ROW)return true;if(result==SQLITE_DONE)return false;throw std::runtime_error(sqlite3_errmsg(db_));}

};
SqliteGenericTable<FriendsData> table(const std::shared_ptr<SqliteDatabase>& database,const std::string& realm,const std::shared_ptr<SqliteFriendsQueryState>& query){
 SqliteTableSchema<FriendsData> schema;
 schema.fields.push_back({"PrincipalID",[](auto& row){return row.principal;},[](auto& row,const auto& source,int col){row.principal=source.string_value(col);}});
 schema.fields.push_back({"Friend",[](auto& row){return row.friend_id;},[](auto& row,const auto& source,int col){row.friend_id=source.string_value(col);}});
 schema.data=[](auto& row)->std::shared_ptr<SqliteTableFields>&{return row.data;};
 return SqliteGenericTable<FriendsData>([database]{return database;},realm,std::move(schema),query);
}

}
struct SqliteFriendsStore::State{std::string realm;std::shared_ptr<SqliteDatabase> owner;std::shared_ptr<SqliteFriendsQueryState> query=std::make_shared<SqliteFriendsQueryState>();};
SqliteFriendsStore::SqliteFriendsStore(const std::string& path,bool initialize,const std::string& realm):state_(std::make_unique<State>()){
    if(path.empty()||realm.empty())throw std::invalid_argument("Empty friends database path or realm");state_->realm=realm;
    if(initialize&&path!=":memory:"&&std::filesystem::exists(framework::utf8_path(path))&&std::filesystem::file_size(framework::utf8_path(path))>0)throw std::runtime_error("Friends initialization requires an empty destination");
    state_->owner=std::make_shared<SqliteDatabase>();state_->owner->open_path(path,initialize);auto db=state_->owner->handle();

    if(initialize){Statement create(db,"CREATE TABLE Friends (PrincipalID CHAR(36) NOT NULL, Friend VARCHAR(255) NOT NULL, Flags VARCHAR(16) NOT NULL DEFAULT 0, Offered VARCHAR(32) NOT NULL DEFAULT 0, PRIMARY KEY(PrincipalID,Friend))");create.row();}
    Statement check(db,"SELECT PrincipalID,Friend,Flags,Offered FROM "+realm+" LIMIT 0");check.row();
}
SqliteFriendsStore::SqliteFriendsStore(std::shared_ptr<SqliteDatabase> connection,const std::string& realm,std::shared_ptr<SqliteFriendsQueryState> query_state):state_(std::make_unique<State>()){state_->owner=std::move(connection);state_->realm=realm;if(query_state)state_->query=std::move(query_state);}
SqliteFriendsStore::~SqliteFriendsStore()=default;
std::vector<std::shared_ptr<FriendsData>> SqliteFriendsStore::get(const std::optional<std::string>& principal){
 if(!principal)throw std::invalid_argument("Null friend principal");
 return table(state_->owner,state_->realm,state_->query).query("select a.*,case when b.Flags is null then -1 else b.Flags end as TheirFlags from "+state_->realm+" as a left join "+state_->realm+" as b on a.PrincipalID = b.Friend and a.Friend = b.PrincipalID where a.PrincipalID = :PrincipalID",{{":PrincipalID",principal}});
}
std::vector<std::shared_ptr<FriendsData>> SqliteFriendsStore::get_field(std::optional<std::string> field,std::optional<std::string> key){return table(state_->owner,state_->realm,state_->query).get_field(std::move(field),std::move(key));}
std::vector<std::shared_ptr<FriendsData>> SqliteFriendsStore::get_fields(const SqliteTableStrings* fields,const SqliteTableStrings* keys){return table(state_->owner,state_->realm,state_->query).get_fields(fields,keys);}
std::vector<std::shared_ptr<FriendsData>> SqliteFriendsStore::get_where(std::optional<std::string> where){return table(state_->owner,state_->realm,state_->query).get_where(std::move(where));}
bool SqliteFriendsStore::delete_fields(const SqliteTableStrings* fields,const SqliteTableStrings* keys){return table(state_->owner,state_->realm,state_->query).delete_fields(fields,keys);}
bool SqliteFriendsStore::store(const std::shared_ptr<FriendsData>& row){return table(state_->owner,state_->realm,state_->query).store(row);}
bool SqliteFriendsStore::erase(const std::optional<std::string>& principal,const std::optional<std::string>& friend_id){if(!principal)throw std::invalid_argument("Null friend principal");table(state_->owner,state_->realm,state_->query).execute("delete from "+state_->realm+" where PrincipalID = :PrincipalID and Friend = :Friend",{{":PrincipalID",principal},{":Friend",friend_id}});return true;}
}
