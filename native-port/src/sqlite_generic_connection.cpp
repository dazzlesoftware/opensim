// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_generic_connection.hpp"
#include "opensim/services/sqlite_connection.hpp"
#include "opensim/services/user_account.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <sqlite3.h>
namespace opensim::services {
SqliteDatabase::~SqliteDatabase(){collect_readers();if(connection_&&owns_)sqlite3_close_v2(connection_);}
void SqliteDatabase::retain_reader(sqlite3_stmt* reader,std::string sql,std::vector<std::string> columns){std::lock_guard lock(mutex_);readers_.push_back({reader,std::move(sql),std::move(columns)});}
std::optional<std::vector<std::string>> SqliteDatabase::reader_columns(const std::string& sql){std::lock_guard lock(mutex_);auto i=pooled_columns_.find(sql);return i==pooled_columns_.end()?std::nullopt:std::optional{i->second};}
void SqliteDatabase::collect_readers(){std::lock_guard lock(mutex_);for(auto& reader:readers_){if(!reader.sql.empty())pooled_columns_[reader.sql]=reader.columns;sqlite3_finalize(reader.value);}readers_.clear();}
std::shared_ptr<SqliteDatabase> SqliteDatabase::borrow(sqlite3* connection){if(!connection)throw std::invalid_argument("Null borrowed SQLite connection");auto result=std::make_shared<SqliteDatabase>();result->connection_=connection;result->owns_=false;return result;}
void SqliteDatabase::open_path(const std::string& path,bool create){std::lock_guard lock(mutex_);if(!owns_)throw std::logic_error("Cannot reopen borrowed SQLite connection");if(connection_){sqlite3_close_v2(connection_);connection_=nullptr;}if(sqlite3_open_v2(path.c_str(),&connection_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX|(create?SQLITE_OPEN_CREATE:0),nullptr)!=SQLITE_OK)throw std::runtime_error(connection_?sqlite3_errmsg(connection_):"SQLite allocation failed");sqlite3_busy_timeout(connection_,30000);}
void SqliteDatabase::open(const std::string& connection){
 auto values=parse_sqlite_connection(connection);
 for(auto& [key,value]:values){if(framework::ordinal_equals_ignore_case(key,"URI")||framework::ordinal_equals_ignore_case(key,"Data Source")||framework::ordinal_equals_ignore_case(key,"DataSource")||framework::ordinal_equals_ignore_case(key,"Data_Source"))continue;
 if(framework::ordinal_equals_ignore_case(key,"Version")){int32_t version;if(!parse_account_integer(value,version)||version!=3)throw std::invalid_argument("Unsupported SQLite version");}
 else if(framework::ordinal_equals_ignore_case(key,"Pooling")){if(sqlite_boolean(value))throw std::invalid_argument("SQLite connection pooling is not implemented");}
 else throw std::invalid_argument("SQLite connection option is not implemented: "+key);}
 open_path(sqlite_connection_path(values),true);
}
std::shared_ptr<SqliteDatabase> initialize_generic_sqlite(const std::shared_ptr<SqliteGenericState>& state,const std::string& connection,const std::string& store,const std::shared_ptr<MigrationResources>& resources,MigrationLog log){
 if(!state)throw std::invalid_argument("Null SQLite generic state");
 if(!state->initialized.load()){auto database=std::make_shared<SqliteDatabase>();state->connection=database;database->open(connection);if(!store.empty()){Migration migration(sqlite_migration_database(database->handle()),resources,store,std::move(log));migration.update();}state->initialized=true;}
 return state->connection.load();
}
std::string sqlite_data_source_connection(const std::string& path){std::string value="Data Source=\"";for(char c:path){if(c=='\\'||c=='"')value+='\\';value+=c;}return value+'"';}
}
