// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/friends_service.hpp"
#include "opensim/services/sqlite_generic_connection.hpp"
namespace opensim::services {
class SqliteFriendsPlugin:public FriendsStore {
public:
 SqliteFriendsPlugin(const std::string& connection,std::string realm="Friends",MigrationLog log={},std::shared_ptr<SqliteGenericState> state={});
 using FriendsStore::get;using FriendsStore::erase;
 std::vector<std::shared_ptr<FriendsData>> get(const std::optional<std::string>& principal)override;
 std::vector<std::shared_ptr<FriendsData>> get_field(std::optional<std::string> field,std::optional<std::string> key);
 std::vector<std::shared_ptr<FriendsData>> get_fields(const SqliteTableStrings* fields,const SqliteTableStrings* keys);
 std::vector<std::shared_ptr<FriendsData>> get_where(std::optional<std::string> where);
 bool delete_fields(const SqliteTableStrings* fields,const SqliteTableStrings* keys);
 bool store(const std::shared_ptr<FriendsData>& row)override;
 bool erase(const std::optional<std::string>& principal,const std::optional<std::string>& friend_id)override;
 static std::shared_ptr<SqliteGenericState> shared_state();
private:std::string realm_;std::shared_ptr<SqliteGenericState> state_;std::shared_ptr<SqliteFriendsQueryState> query_=std::make_shared<SqliteFriendsQueryState>();
};
}
