// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_avatar_plugin.hpp"
namespace opensim::services {
std::shared_ptr<SqliteGenericState> SqliteAvatarPlugin::shared_state() {
    static auto state = std::make_shared<SqliteGenericState>();
    return state;
}
SqliteAvatarPlugin::SqliteAvatarPlugin(const std::string &connection, std::string realm, MigrationLog log,
                                       std::shared_ptr<SqliteGenericState> state)
    : realm_(std::move(realm)), state_(state ? std::move(state) : shared_state()) {
    initialize_generic_sqlite(state_, connection, "Avatar", avatar_store_migrations(), std::move(log));
}
SqliteGenericTable<AvatarBaseData> SqliteAvatarPlugin::table() {
    SqliteTableSchema<AvatarBaseData> schema;
    schema.fields.push_back({"PrincipalID", [](auto &row) { return row.principal.str(); },
                             [](auto &row, const auto &source, int col) {
                                 framework::try_parse_sdk_uuid(source.value_text(col), row.principal);
                             }});
    schema.data = [](auto &row) -> std::shared_ptr<SqliteTableFields> & { return row.data; };
    return SqliteGenericTable<AvatarBaseData>([state = state_] { return state->connection.load(); }, realm_,
                                              std::move(schema), query_);
}
std::vector<std::shared_ptr<AvatarBaseData>> SqliteAvatarPlugin::get(framework::Uuid principal) {
    return table().get_field("PrincipalID", principal.str());
}
std::vector<std::shared_ptr<AvatarBaseData>> SqliteAvatarPlugin::get_field(std::optional<std::string> field,
                                                                           std::optional<std::string> key) {
    return table().get_field(std::move(field), std::move(key));
}
std::vector<std::shared_ptr<AvatarBaseData>> SqliteAvatarPlugin::get_fields(const SqliteTableStrings *fields,
                                                                            const SqliteTableStrings *keys) {
    return table().get_fields(fields, keys);
}
std::vector<std::shared_ptr<AvatarBaseData>> SqliteAvatarPlugin::get_where(std::optional<std::string> where) {
    return table().get_where(std::move(where));
}
bool SqliteAvatarPlugin::store(std::shared_ptr<AvatarBaseData> row) { return table().store(row); }
bool SqliteAvatarPlugin::erase_all(framework::Uuid principal) {
    return table().delete_field("PrincipalID", principal.str());
}
bool SqliteAvatarPlugin::erase(framework::Uuid principal, const std::optional<std::string> &name) {
    return table().execute("delete from " + realm_ + " where `PrincipalID` = :PrincipalID and `Name` = :Name",
                           {{":PrincipalID", principal.str()}, {":Name", name}}) > 0;
}
bool SqliteAvatarPlugin::delete_fields(const SqliteTableStrings *fields, const SqliteTableStrings *keys) {
    return table().delete_fields(fields, keys);
}
}
