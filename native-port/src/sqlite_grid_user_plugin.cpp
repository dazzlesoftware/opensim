// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_grid_user_plugin.hpp"
namespace opensim::services {
std::shared_ptr<SqliteGenericState> SqliteGridUserPlugin::shared_state() {
    static auto state = std::make_shared<SqliteGenericState>();
    return state;
}
SqliteGridUserPlugin::SqliteGridUserPlugin(const std::string &connection, std::string realm, MigrationLog log,
                                           std::shared_ptr<SqliteGenericState> state)
    : realm_(std::move(realm)), state_(state ? std::move(state) : shared_state()) {
    initialize_generic_sqlite(state_, connection, "GridUserStore", grid_user_store_migrations(),
                              std::move(log));
}
SqliteGenericTable<GridUserData> SqliteGridUserPlugin::table() {
    SqliteTableSchema<GridUserData> schema;
    schema.fields.push_back(
        {"UserID", [](auto &row) { return row.user_id; },
         [](auto &row, const auto &source, int col) { row.user_id = source.string_value(col); }});
    schema.data = [](auto &row) -> std::shared_ptr<SqliteTableFields> & { return row.data; };
    return SqliteGenericTable<GridUserData>([state = state_] { return state->connection.load(); }, realm_,
                                            std::move(schema), query_);
}
std::shared_ptr<GridUserData> SqliteGridUserPlugin::get_record(std::optional<std::string> user) {
    auto rows = table().get_field("UserID", std::move(user));
    return rows.empty() ? nullptr : rows.front();
}
std::vector<std::shared_ptr<GridUserData>>
SqliteGridUserPlugin::get_all_records(std::optional<std::string> prefix) {
    return table().get_where("UserID LIKE '" + prefix.value_or("") + "%'");
}
std::vector<std::shared_ptr<GridUserData>> SqliteGridUserPlugin::get_field(std::optional<std::string> field,
                                                                           std::optional<std::string> key) {
    return table().get_field(std::move(field), std::move(key));
}
std::vector<std::shared_ptr<GridUserData>> SqliteGridUserPlugin::get_fields(const SqliteTableStrings *fields,
                                                                            const SqliteTableStrings *keys) {
    return table().get_fields(fields, keys);
}
std::vector<std::shared_ptr<GridUserData>> SqliteGridUserPlugin::get_where(std::optional<std::string> where) {
    return table().get_where(std::move(where));
}
bool SqliteGridUserPlugin::store(const std::shared_ptr<GridUserData> &row) { return table().store(row); }
}
