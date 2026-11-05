// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/sqlite_migration.hpp"
#include <atomic>
#include <mutex>
struct sqlite3_stmt;
namespace opensim::services {
class SqliteDatabase {
public:
    SqliteDatabase() = default;
    ~SqliteDatabase();
    SqliteDatabase(const SqliteDatabase&) = delete;
    void open_path(const std::string& path, bool create);
    void open(const std::string& connection);
    static std::shared_ptr<SqliteDatabase> borrow(sqlite3* connection);
    void retain_reader(sqlite3_stmt* reader, std::string sql = {}, std::vector<std::string> columns = {});
    std::optional<std::vector<std::string>> reader_columns(const std::string& sql);
    void collect_readers();
    sqlite3* handle() const {
        return connection_;
    }
    std::recursive_mutex& mutex() {
        return mutex_;
    }

private:
    struct Reader {
        sqlite3_stmt* value;
        std::string sql;
        std::vector<std::string> columns;
    };
    sqlite3* connection_ = nullptr;
    bool owns_ = true;
    std::vector<Reader> readers_;
    std::map<std::string, std::vector<std::string>> pooled_columns_;
    std::recursive_mutex mutex_;
};
// One instance corresponds to the original generic class's static fields for T.
// Keep a shared owner when reading the connection, including failed startup.
struct SqliteGenericState {
    std::atomic<std::shared_ptr<SqliteDatabase>> connection;
    std::atomic<bool> initialized = false;
};
struct SqliteQueryState {
    std::optional<std::vector<std::string>> columns;
    std::mutex mutex;
};
std::shared_ptr<SqliteDatabase>
initialize_generic_sqlite(const std::shared_ptr<SqliteGenericState>& state, const std::string& connection,
                          const std::string& store, const std::shared_ptr<MigrationResources>& resources,
                          MigrationLog log = {});
std::string sqlite_data_source_connection(const std::string& path);
}
