// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_friends_plugin.hpp"
namespace opensim::services {
std::shared_ptr<SqliteGenericState> SqliteFriendsPlugin::shared_state() {
    static auto state = std::make_shared<SqliteGenericState>();
    return state;
}
SqliteFriendsPlugin::SqliteFriendsPlugin(const std::string& connection, std::string realm, MigrationLog log,
                                         std::shared_ptr<SqliteGenericState> state)
    : realm_(std::move(realm)), state_(state ? std::move(state) : shared_state()) {
    initialize_generic_sqlite(state_, connection, "FriendsStore", friends_store_migrations(), std::move(log));
}
std::vector<std::shared_ptr<FriendsData>>
SqliteFriendsPlugin::get(const std::optional<std::string>& principal) {
    SqliteFriendsStore store(state_->connection.load(), realm_, query_);
    return store.get(principal);
}
bool SqliteFriendsPlugin::store(const std::shared_ptr<FriendsData>& row) {
    SqliteFriendsStore store(state_->connection.load(), realm_, query_);
    return store.store(row);
}
bool SqliteFriendsPlugin::erase(const std::optional<std::string>& principal,
                                const std::optional<std::string>& friend_id) {
    SqliteFriendsStore store(state_->connection.load(), realm_, query_);
    return store.erase(principal, friend_id);
}
std::vector<std::shared_ptr<FriendsData>> SqliteFriendsPlugin::get_field(std::optional<std::string> field,
                                                                         std::optional<std::string> key) {
    SqliteFriendsStore store(state_->connection.load(), realm_, query_);
    return store.get_field(std::move(field), std::move(key));
}
std::vector<std::shared_ptr<FriendsData>> SqliteFriendsPlugin::get_fields(const SqliteTableStrings* fields,
                                                                          const SqliteTableStrings* keys) {
    SqliteFriendsStore store(state_->connection.load(), realm_, query_);
    return store.get_fields(fields, keys);
}
std::vector<std::shared_ptr<FriendsData>> SqliteFriendsPlugin::get_where(std::optional<std::string> where) {
    SqliteFriendsStore store(state_->connection.load(), realm_, query_);
    return store.get_where(std::move(where));
}
bool SqliteFriendsPlugin::delete_fields(const SqliteTableStrings* fields, const SqliteTableStrings* keys) {
    SqliteFriendsStore store(state_->connection.load(), realm_, query_);
    return store.delete_fields(fields, keys);
}

}
