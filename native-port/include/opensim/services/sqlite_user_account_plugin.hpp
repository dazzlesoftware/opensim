// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/sqlite_generic_table.hpp"
#include "opensim/framework/uuid.hpp"
namespace opensim::services {
struct UserAccountRecord {
 framework::Uuid principal,scope;
 std::optional<std::string> first,last;
 std::shared_ptr<SqliteTableFields> data;
};
class SqliteUserAccountTable {
public:
 using Rows=std::vector<std::shared_ptr<UserAccountRecord>>;
 virtual ~SqliteUserAccountTable()=default;
 SqliteUserAccountTable(SqliteTableOperations::Connection connection,std::string realm="UserAccounts"):connection_(std::move(connection)),realm_(std::move(realm)){}
 Rows get_field(std::optional<std::string> field,std::optional<std::string> key){return table().get_field(std::move(field),std::move(key));}
 Rows get_fields(const SqliteTableStrings* fields,const SqliteTableStrings* keys){return table().get_fields(fields,keys);}
 Rows get_where(std::optional<std::string> where){return table().get_where(std::move(where));}
 Rows get_users(framework::Uuid scope,const std::optional<std::string>& query);
 std::optional<Rows> get_users_where(framework::Uuid,const std::optional<std::string>&){return std::nullopt;}
 bool store(const std::shared_ptr<UserAccountRecord>& row){return table().store(row);}
 bool delete_field(std::optional<std::string> field,std::optional<std::string> key){return table().delete_field(std::move(field),std::move(key));}
 bool delete_fields(const SqliteTableStrings* fields,const SqliteTableStrings* keys){return table().delete_fields(fields,keys);}
private:
 SqliteGenericTable<UserAccountRecord> table();
 SqliteTableOperations::Connection connection_;std::string realm_;std::shared_ptr<SqliteQueryState> query_=std::make_shared<SqliteQueryState>();
};
class SqliteUserAccountPlugin:public SqliteUserAccountTable {
public:
 SqliteUserAccountPlugin(const std::string& connection,std::string realm="UserAccounts",MigrationLog log={},std::shared_ptr<SqliteGenericState> state={}):SqliteUserAccountPlugin(connection,std::move(realm),std::move(log),state?std::move(state):shared_state(),0){}
 static std::shared_ptr<SqliteGenericState> shared_state();
private:SqliteUserAccountPlugin(const std::string&,std::string,MigrationLog,std::shared_ptr<SqliteGenericState>,int);
};
}
