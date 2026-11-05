// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/grid_user_service.hpp"
#include "opensim/services/sqlite_generic_table.hpp"
namespace opensim::services {
class SqliteGridUserPlugin : public GridUserStore {
  public:
    SqliteGridUserPlugin(const std::string& connection, std::string realm = "GridUser", MigrationLog log = {},
                         std::shared_ptr<SqliteGenericState> state = {});
    std::shared_ptr<GridUserData> get(const std::string& user) override {
        return get_record(user);
    }
    std::vector<std::shared_ptr<GridUserData>> get_all(const std::string& prefix) override {
        return get_all_records(prefix);
    }
    std::shared_ptr<GridUserData> get_record(std::optional<std::string> user);
    std::vector<std::shared_ptr<GridUserData>> get_all_records(std::optional<std::string> prefix);
    std::vector<std::shared_ptr<GridUserData>> get_field(std::optional<std::string> field,
                                                         std::optional<std::string> key);
    std::vector<std::shared_ptr<GridUserData>> get_fields(const SqliteTableStrings* fields,
                                                          const SqliteTableStrings* keys);
    std::vector<std::shared_ptr<GridUserData>> get_where(std::optional<std::string> where);
    bool store(const std::shared_ptr<GridUserData>& row) override;
    bool delete_field(std::optional<std::string> field, std::optional<std::string> key) {
        return table().delete_field(std::move(field), std::move(key));
    }
    bool delete_fields(const SqliteTableStrings* fields, const SqliteTableStrings* keys) {
        return table().delete_fields(fields, keys);
    }
    static std::shared_ptr<SqliteGenericState> shared_state();

  private:
    SqliteGenericTable<GridUserData> table();
    std::string realm_;
    std::shared_ptr<SqliteGenericState> state_;
    std::shared_ptr<SqliteQueryState> query_ = std::make_shared<SqliteQueryState>();
};
}
