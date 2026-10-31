// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_user_account_plugin.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::services {
SqliteGenericTable<UserAccountRecord> SqliteUserAccountTable::table(){SqliteTableSchema<UserAccountRecord> schema;schema.fields={
 {"PrincipalID",[](auto& r){return r.principal.str();},[](auto& r,const auto& row,int col){framework::try_parse_sdk_uuid(row.value_text(col),r.principal);}},
 {"ScopeID",[](auto& r){return r.scope.str();},[](auto& r,const auto& row,int col){framework::try_parse_sdk_uuid(row.value_text(col),r.scope);}},
 {"FirstName",[](auto& r){return r.first;},[](auto& r,const auto& row,int col){r.first=row.string_value(col);}},
 {"LastName",[](auto& r){return r.last;},[](auto& r,const auto& row,int col){r.last=row.string_value(col);}}};schema.data=[](auto& r)->std::shared_ptr<SqliteTableFields>&{return r.data;};return SqliteGenericTable<UserAccountRecord>(connection_,realm_,std::move(schema),query_);}
SqliteUserAccountTable::Rows SqliteUserAccountTable::get_users(framework::Uuid scope,const std::optional<std::string>& query){if(!query)throw std::invalid_argument("Null user-account search");std::vector<std::string> words;size_t start=0;while(start<=query->size()){auto end=query->find(' ',start);auto word=query->substr(start,end==query->npos?end:end-start);if(!word.empty())words.push_back(std::move(word));if(end==query->npos)break;start=end+1;}
 for(size_t i=0;i<words.size();++i)if(framework::utf16_replacing(words[i]).size()<3)words.erase(words.begin()+static_cast<ptrdiff_t>(i));if(words.empty()||words.size()>2)return {};
 auto where="(ScopeID='"+scope.str()+"' or ScopeID='00000000-0000-0000-0000-000000000000') and (FirstName like '"+words[0]+"%' or LastName like '"+words[words.size()-1]+"%')";return table().get_where(std::move(where));}
std::shared_ptr<SqliteGenericState> SqliteUserAccountPlugin::shared_state(){static auto state=std::make_shared<SqliteGenericState>();return state;}
SqliteUserAccountPlugin::SqliteUserAccountPlugin(const std::string& connection,std::string realm,MigrationLog log,std::shared_ptr<SqliteGenericState> state,int):SqliteUserAccountTable([state]{return state->connection.load();},std::move(realm)){initialize_generic_sqlite(state,connection,"UserAccount",user_account_store_migrations(),std::move(log));}
}
