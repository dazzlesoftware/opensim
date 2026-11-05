// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/avatar_service.hpp"
#include "opensim/services/sqlite_generic_table.hpp"
namespace opensim::services {
class SqliteAvatarPlugin : public AvatarStore {
  public:
    SqliteAvatarPlugin(const std::string& connection, std::string realm = "Avatars", MigrationLog log = {},
                       std::shared_ptr<SqliteGenericState> state = {});
    std::vector<std::shared_ptr<AvatarBaseData>> get(framework::Uuid principal) override;
    std::vector<std::shared_ptr<AvatarBaseData>> get_field(std::optional<std::string> field,
                                                           std::optional<std::string> key);
    std::vector<std::shared_ptr<AvatarBaseData>> get_fields(const SqliteTableStrings* fields,
                                                            const SqliteTableStrings* keys);
    std::vector<std::shared_ptr<AvatarBaseData>> get_where(std::optional<std::string> where);
    bool store(std::shared_ptr<AvatarBaseData> row) override;
    bool erase_all(framework::Uuid principal) override;
    bool erase(framework::Uuid principal, const std::optional<std::string>& name) override;
    bool delete_fields(const SqliteTableStrings* fields, const SqliteTableStrings* keys);
    bool delete_field(std::optional<std::string> field, std::optional<std::string> key) {
        return table().delete_field(std::move(field), std::move(key));
    }
    static std::shared_ptr<SqliteGenericState> shared_state();

  private:
    SqliteGenericTable<AvatarBaseData> table();
    std::string realm_;
    std::shared_ptr<SqliteGenericState> state_;
    std::shared_ptr<SqliteQueryState> query_ = std::make_shared<SqliteQueryState>();
};
}
