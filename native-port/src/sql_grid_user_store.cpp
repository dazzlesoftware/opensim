// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/grid_user_service.hpp"
#include "sql_service_connection.hpp"

#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace opensim::services {
namespace {
constexpr auto migration_name = "GridUserStore";

std::string create_grid_user(bool postgres) {
    std::string statement =
        "CREATE TABLE IF NOT EXISTS GridUser ("
        "\"UserID\" VARCHAR(255) NOT NULL PRIMARY KEY,"
        "\"HomeRegionID\" CHAR(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',"
        "\"HomePosition\" CHAR(64) NOT NULL DEFAULT '<0,0,0>',"
        "\"HomeLookAt\" CHAR(64) NOT NULL DEFAULT '<0,0,0>',"
        "\"LastRegionID\" CHAR(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',"
        "\"LastPosition\" CHAR(64) NOT NULL DEFAULT '<0,0,0>',"
        "\"LastLookAt\" CHAR(64) NOT NULL DEFAULT '<0,0,0>',"
        "\"Online\" CHAR(5) NOT NULL DEFAULT 'false',"
        "\"Login\" CHAR(16) NOT NULL DEFAULT '0',"
        "\"Logout\" CHAR(16) NOT NULL DEFAULT '0')";
    if (!postgres) {
        std::replace(statement.begin(), statement.end(), '"', '`');
        statement += " ENGINE=InnoDB DEFAULT CHARSET=latin1";
    }
    return statement;
}

class SqlGridUserStore final : public GridUserStore {
public:
    SqlGridUserStore(sql::Execute execute, std::string realm, bool postgres)
        : execute_(std::move(execute)), postgres_(postgres) {
        if (realm.empty())
            throw std::invalid_argument("Missing SQL grid-user realm");
        if (postgres_)
            for (auto& character : realm)
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
        table_ = quote(realm);
    }

    std::shared_ptr<GridUserData> get(const std::string& user) override {
        auto rows = query("=", user);
        return rows.empty() ? nullptr : std::move(rows.front());
    }

    std::vector<std::shared_ptr<GridUserData>> get_all(const std::string& prefix) override {
        return query(" LIKE ", prefix + "%");
    }

    bool store(const std::shared_ptr<GridUserData>& row) override {
        if (!row || !row->data)
            throw std::invalid_argument("Missing grid-user data");
        std::string columns = quote("UserID");
        std::string markers = "?";
        std::string updates;
        sql::Parameters values{row->user_id};
        for (const auto& [key, value] : *row->data) {
            if (key == "UserID")
                throw std::invalid_argument("Grid-user data contains a duplicate UserID");
            const auto column = quote(key);
            columns += "," + column;
            markers += ",?";
            values.push_back(value);
            if (!updates.empty())
                updates += ',';
            updates += column + "=EXCLUDED." + column;
        }
        std::string statement = postgres_ ? "INSERT INTO " : "REPLACE INTO ";
        statement += table_ + " (" + columns + ") VALUES (" + markers + ")";
        if (postgres_) {
            if (updates.empty())
                updates = "\"UserID\"=EXCLUDED.\"UserID\"";
            statement += " ON CONFLICT (\"UserID\") DO UPDATE SET " + updates;
        }
        std::lock_guard lock(mutex_);
        return execute_(statement, values).affected;
    }

private:
    std::string quote(std::string_view identifier) const {
        const char delimiter = postgres_ ? '"' : '`';
        std::string escaped(1, delimiter);
        for (const auto character : identifier) {
            if (character == '\0' || character == '?')
                throw std::invalid_argument("Invalid SQL grid-user identifier");
            if (character == delimiter)
                escaped.push_back(delimiter);
            escaped.push_back(character);
        }
        return escaped + delimiter;
    }

    std::vector<std::shared_ptr<GridUserData>> query(std::string_view condition, const std::string& value) {
        std::lock_guard lock(mutex_);
        const auto rows = execute_("SELECT * FROM " + table_ + " WHERE " + quote("UserID") +
                                   std::string(condition) + "?", {value});
        std::vector<std::shared_ptr<GridUserData>> result;
        for (const auto& values : rows.rows) {
            auto row = std::make_shared<GridUserData>();
            for (std::size_t index = 0; index < rows.names.size(); ++index) {
                if (rows.names[index] == "UserID")
                    row->user_id = values[index];
                else
                    (*row->data)[rows.names[index]] = values[index];
            }
            result.push_back(std::move(row));
        }
        return result;
    }

    sql::Execute execute_;
    bool postgres_;
    std::string table_;
    std::mutex mutex_;
};
}

#ifdef OPENSIM_HAVE_MYSQL
std::unique_ptr<GridUserStore> mysql_grid_user_store(const std::string& connection, const std::string& realm) {
    const auto execute = sql::mysql(connection);
    auto store = std::make_unique<SqlGridUserStore>(execute, realm, false);
    constexpr auto lock_name = "SHA2(CONCAT(DATABASE(), ':GridUserStore'), 256)";
    const auto lock = execute(std::string("SELECT GET_LOCK(") + lock_name + ", 30)", {});
    if (lock.rows.at(0).at(0) != "1")
        throw std::runtime_error("Cannot acquire MySQL grid-user migration lock");
    if (const auto version = sql::initialize_migrations(execute, migration_name); version < 2) {
        if (version < 1)
            execute(create_grid_user(false), {});
        // The source version 2 migration is empty.
        sql::record_migration(execute, migration_name, version, 2);
    }
    execute(std::string("SELECT RELEASE_LOCK(") + lock_name + ")", {});
    return store;
}
#endif

#ifdef OPENSIM_HAVE_POSTGRESQL
std::unique_ptr<GridUserStore> postgres_grid_user_store(const std::string& connection, const std::string& realm) {
    const auto execute = sql::postgres(connection);
    auto store = std::make_unique<SqlGridUserStore>(execute, realm, true);
    execute("BEGIN", {});
    execute("SELECT pg_advisory_xact_lock(hashtext(current_schema()), hashtext('GridUserStore'))", {});
    const auto version = sql::initialize_migrations(execute, migration_name);
    if (version < 1)
        execute(create_grid_user(true), {});
    if (version < 2) {
        // Convert the original v1 character columns in place, preserving other columns and indexes.
        execute("ALTER TABLE GridUser "
                "ALTER COLUMN \"HomeRegionID\" DROP DEFAULT, ALTER COLUMN \"LastRegionID\" DROP DEFAULT,"
                "ALTER COLUMN \"HomeRegionID\" TYPE uuid USING trim(\"HomeRegionID\"::text)::uuid,"
                "ALTER COLUMN \"LastRegionID\" TYPE uuid USING trim(\"LastRegionID\"::text)::uuid,"
                "ALTER COLUMN \"HomeRegionID\" SET DEFAULT '00000000-0000-0000-0000-000000000000',"
                "ALTER COLUMN \"LastRegionID\" SET DEFAULT '00000000-0000-0000-0000-000000000000'", {});
        sql::record_migration(execute, migration_name, version, 2);
    }
    execute("COMMIT", {});
    return store;
}
#endif
}
