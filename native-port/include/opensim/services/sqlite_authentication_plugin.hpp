// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/sqlite_generic_table.hpp"
#include "opensim/framework/agent_circuit.hpp"
#include <atomic>
namespace opensim::services {
struct AuthenticationRecord {
 framework::Uuid principal;
 std::shared_ptr<framework::OrderedDictionary<std::string,std::any>> data;
};
class SqliteAuthenticationTable {
public:
 using Clock=std::function<std::int32_t()>;
 using Diagnostic=std::function<void(const std::string& channel,const std::string& message,const std::exception& error)>;
 SqliteAuthenticationTable(SqliteTableOperations::Connection connection,std::string realm="auth",Clock clock={},Diagnostic diagnostic={},std::string schema={});
 virtual ~SqliteAuthenticationTable()=default;
 std::shared_ptr<AuthenticationRecord> get(framework::Uuid principal);
 bool store(const std::shared_ptr<AuthenticationRecord>& record);
 bool set_data_item(framework::Uuid principal,const std::optional<std::string>& item,const std::optional<std::string>& value);
 bool set_token(framework::Uuid principal,const std::optional<std::string>& token,std::int32_t lifetime);
 bool check_token(framework::Uuid principal,const std::optional<std::string>& token,std::int32_t lifetime);
 std::int32_t last_expire()const{return last_expire_.load();}
 void collect_readers(){if(connection_){auto db=connection_();if(db)db->collect_readers();}}
private:
 void expire_if_needed();
 std::string table_name(const std::string& name)const;
 int execute(const std::string& sql,const std::vector<std::pair<std::string,std::any>>& parameters={});
 SqliteTableOperations::Connection connection_;std::string realm_,schema_;Clock clock_;Diagnostic diagnostic_;
 std::optional<std::vector<std::string>> columns_;std::mutex columns_mutex_;std::atomic<std::int32_t> last_expire_=0;
};
class SqliteAuthenticationPlugin:public SqliteAuthenticationTable {
public:
 SqliteAuthenticationPlugin(const std::string& connection,std::string realm="auth",MigrationLog log={},Clock clock={},Diagnostic diagnostic={},std::shared_ptr<SqliteGenericState> state={}):SqliteAuthenticationPlugin(connection,std::move(realm),std::move(log),std::move(clock),std::move(diagnostic),state?std::move(state):shared_state(),0){}
 static std::shared_ptr<SqliteGenericState> shared_state();
private:SqliteAuthenticationPlugin(const std::string&,std::string,MigrationLog,Clock,Diagnostic,std::shared_ptr<SqliteGenericState>,int);
};
}
