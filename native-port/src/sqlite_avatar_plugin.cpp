// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_avatar_plugin.hpp"
namespace opensim::services {
SqliteAttachmentScriptStore::SqliteAttachmentScriptStore(std::shared_ptr<SqliteDatabase> database)
    : database_(std::move(database)), table_([this] { return database_; }, "native_attachment_scripts") {
    std::lock_guard lock(database_->mutex());
    table_.execute("BEGIN IMMEDIATE", {});
    try {
        bool legacy_key = false;
        table_.query("SELECT name FROM pragma_table_info('native_attachment_scripts') "
                     "WHERE name='asset_id' AND pk=0", {}, {"name"},
                     [&](const auto&, const auto&) { legacy_key = true; });
        if (legacy_key)
            table_.execute("ALTER TABLE native_attachment_scripts RENAME TO native_attachment_scripts_legacy", {});
        table_.execute("CREATE TABLE IF NOT EXISTS native_attachment_scripts ("
                       "owner_id TEXT NOT NULL, item_id TEXT NOT NULL, asset_id TEXT NOT NULL, "
                       "script_id TEXT NOT NULL, checkpoint BLOB NOT NULL, "
                       "PRIMARY KEY (owner_id,item_id,asset_id,script_id))", {});
        if (legacy_key) {
            table_.execute("INSERT INTO native_attachment_scripts (owner_id,item_id,asset_id,script_id,checkpoint) "
                           "SELECT owner_id,item_id,asset_id,script_id,checkpoint "
                           "FROM native_attachment_scripts_legacy", {});
            table_.execute("DROP TABLE native_attachment_scripts_legacy", {});
        }
        table_.execute("COMMIT", {});
    } catch (...) {
        table_.execute("ROLLBACK", {});
        throw;
    }
}
AttachmentScriptStates SqliteAttachmentScriptStore::load(framework::Uuid owner, framework::Uuid item,
                                                          framework::Uuid asset) {
    AttachmentScriptStates states;
    table_.query("SELECT script_id,checkpoint FROM native_attachment_scripts "
                 "WHERE owner_id=:owner AND item_id=:item AND asset_id=:asset",
                 {{":owner", owner.str()}, {":item", item.str()}, {":asset", asset.str()}},
                 {"script_id", "checkpoint"}, [&](const auto& row, const auto&) {
                     framework::Uuid script;
                     if (framework::try_parse_sdk_uuid(row.string_value(0), script) && !script.is_zero()) {
                         const auto bytes = row.blob_value(1);
                         states.emplace(script, std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
                     }
                 });
    return states;
}
void SqliteAttachmentScriptStore::save(framework::Uuid owner, framework::Uuid item, framework::Uuid asset,
                                       const AttachmentScriptStates& states) {
    std::lock_guard lock(database_->mutex());
    table_.execute("BEGIN IMMEDIATE", {});
    try {
        table_.execute("DELETE FROM native_attachment_scripts "
                       "WHERE owner_id=:owner AND item_id=:item AND asset_id=:asset",
                       {{":owner", owner.str()}, {":item", item.str()}, {":asset", asset.str()}});
        for (const auto& [script, bytes] : states)
            table_.execute("INSERT INTO native_attachment_scripts "
                           "VALUES(:owner,:item,:asset,:script,CAST(:state AS BLOB))",
                           {{":owner", owner.str()}, {":item", item.str()}, {":asset", asset.str()},
                            {":script", script.str()}, {":state", std::string(bytes.begin(), bytes.end())}});
        table_.execute("COMMIT", {});
    } catch (...) {
        table_.execute("ROLLBACK", {});
        throw;
    }
}

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
