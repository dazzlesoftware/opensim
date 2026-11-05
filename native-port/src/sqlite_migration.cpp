// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_migration.hpp"
#include "opensim/services/user_account.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "asset_store_migration_resource.hpp"
#include "friends_store_migration_resource.hpp"
#include "mute_list_migration_resource.hpp"
#include "avatar_store_migration_resource.hpp"
#include "grid_user_store_migration_resource.hpp"
#include "user_account_store_migration_resource.hpp"
#include "auth_store_migration_resource.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cmath>
namespace opensim::services {
namespace {
int forever_busy(void*, int) {
    sqlite3_sleep(1);
    return 1;
}
void timeout(sqlite3* db, int32_t seconds) {
    if (seconds == 0)
        sqlite3_busy_handler(db, forever_busy, nullptr);
    else
        sqlite3_busy_timeout(db, static_cast<int>(std::min(int64_t(INT32_MAX), int64_t(seconds) * 1000)));
}
class Reader : public MigrationReader {
    sqlite3* db_;
    sqlite3_stmt* statement_ = nullptr;
    int32_t timeout_;

  public:
    Reader(sqlite3* db, const std::string& sql, int32_t seconds) : db_(db), timeout_(seconds) {
        timeout(db_, timeout_);
        if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &statement_, nullptr) != SQLITE_OK) {
            auto message = std::string(sqlite3_errmsg(db_));
            if (statement_)
                sqlite3_finalize(statement_);
            throw std::runtime_error(message);
        }
    }
    ~Reader() override {
        if (statement_)
            sqlite3_finalize(statement_);
    }
    bool read() override {
        if (!statement_)
            throw std::runtime_error("Migration reader closed");
        timeout(db_, timeout_);
        auto result = sqlite3_step(statement_);
        if (result == SQLITE_ROW)
            return true;
        if (result == SQLITE_DONE)
            return false;
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    int32_t version_value() override {
        if (!statement_)
            throw std::runtime_error("Migration reader closed");
        int col = -1;
        for (int i = 0; i < sqlite3_column_count(statement_); ++i)
            if (framework::ordinal_equals_ignore_case(sqlite3_column_name(statement_, i), "version")) {
                col = i;
                break;
            }
        if (col < 0)
            throw std::invalid_argument("Missing migration version column");
        auto kind = sqlite3_column_type(statement_, col);
        if (kind == SQLITE_NULL)
            throw std::invalid_argument("Null migration version");
        const char* declared = sqlite3_column_decltype(statement_, col);
        std::string type = declared ? declared : "";
        for (char& c : type)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + 32);
        if (type == "int" || type == "int32")
            return sqlite3_column_int(statement_, col);
        if (type == "integer" || type == "bigint" || type == "int64") {
            auto v = sqlite3_column_int64(statement_, col);
            if (v < INT32_MIN || v > INT32_MAX)
                throw std::out_of_range("Migration version overflow");
            return static_cast<int32_t>(v);
        }
        if (kind == SQLITE_TEXT) {
            auto ptr = sqlite3_column_text(statement_, col);
            std::string value(reinterpret_cast<const char*>(ptr), sqlite3_column_bytes(statement_, col));
            int32_t result;
            if (!parse_account_integer(value, result))
                throw std::invalid_argument("Invalid migration version integer");
            return result;
        }
        if (kind == SQLITE_BLOB)
            throw std::invalid_argument("Binary migration version");
        long double value = kind == SQLITE_FLOAT
                                ? std::nearbyint(sqlite3_column_double(statement_, col))
                                : static_cast<long double>(sqlite3_column_int64(statement_, col));
        if (!std::isfinite(value) || value < INT32_MIN || value > INT32_MAX)
            throw std::out_of_range("Migration version overflow");
        return static_cast<int32_t>(value);
    }
    void close() override {
        if (statement_) {
            sqlite3_finalize(statement_);
            statement_ = nullptr;
        }
    }
    void dispose() override {
        close();
    }
};
class Command : public MigrationCommand {
    sqlite3* db_;
    std::string sql_;
    int32_t seconds_ = 30;
    bool disposed_ = false;
    void check() {
        if (disposed_)
            throw std::runtime_error("Migration command disposed");
    }

  public:
    explicit Command(sqlite3* db) : db_(db) {
    }
    void timeout(int32_t value) override {
        check();
        seconds_ = value;
    }
    void text(const std::string& sql) override {
        check();
        sql_ = sql;
    }
    void execute() override {
        check();
        services::timeout(db_, seconds_);
        char* error = nullptr;
        int result = sqlite3_exec(db_, sql_.c_str(), nullptr, nullptr, &error);
        if (result != SQLITE_OK) {
            std::string message = error ? error : sqlite3_errmsg(db_);
            if (error)
                sqlite3_free(error);
            throw std::runtime_error(message);
        }
        if (error)
            sqlite3_free(error);
    }
    std::shared_ptr<MigrationReader> reader() override {
        check();
        return std::make_shared<Reader>(db_, sql_, seconds_);
    }
    void dispose() override {
        if (!disposed_) {
            sqlite3_busy_timeout(db_, 30000);
            disposed_ = true;
        }
    }
};
class Database : public MigrationDatabase {
    sqlite3* db_;

  public:
    explicit Database(sqlite3* db) : db_(db) {
    }
    std::shared_ptr<MigrationCommand> command() override {
        if (!db_)
            throw std::invalid_argument("Null migration connection");
        return std::make_shared<Command>(db_);
    }
};
class AssetResources : public MigrationResources {
  public:
    std::vector<std::string> names() override {
        return {"OpenSim.Data.SQLite.Resources.AssetStore.migrations"};
    }
    std::optional<std::string> bytes(const std::string& name) override {
        if (name == "OpenSim.Data.SQLite.Resources.AssetStore.migrations")
            return std::string(AssetStoreMigration);
        return {};
    }
};
class FriendsResources : public MigrationResources {
  public:
    std::vector<std::string> names() override {
        return {"OpenSim.Data.SQLite.Resources.FriendsStore.migrations"};
    }
    std::optional<std::string> bytes(const std::string& name) override {
        if (name == "OpenSim.Data.SQLite.Resources.FriendsStore.migrations")
            return std::string(FriendsStoreMigration);
        return {};
    }
};
class MuteListResources final : public MigrationResources {
  public:
    std::vector<std::string> names() override {
        return {resource_name};
    }
    std::optional<std::string> bytes(const std::string& name) override {
        if (name == resource_name)
            return std::string(MuteListMigration);
        return {};
    }

  private:
    static constexpr const char* resource_name = "OpenSim.Data.SQLite.Resources.MuteListStore.migrations";
};
class AvatarResources : public MigrationResources {
  public:
    std::vector<std::string> names() override {
        return {"OpenSim.Data.SQLite.Resources.Avatar.migrations"};
    }
    std::optional<std::string> bytes(const std::string& name) override {
        if (name == "OpenSim.Data.SQLite.Resources.Avatar.migrations")
            return std::string(AvatarStoreMigration);
        return {};
    }
};
class UserAccountResources : public MigrationResources {
  public:
    std::vector<std::string> names() override {
        return {"OpenSim.Data.SQLite.Resources.UserAccount.migrations"};
    }
    std::optional<std::string> bytes(const std::string& name) override {
        if (name == "OpenSim.Data.SQLite.Resources.UserAccount.migrations")
            return std::string(UserAccountStoreMigration);
        return {};
    }
};
class AuthResources : public MigrationResources {
  public:
    std::vector<std::string> names() override {
        return {"OpenSim.Data.SQLite.Resources.AuthStore.migrations"};
    }
    std::optional<std::string> bytes(const std::string& name) override {
        if (name == "OpenSim.Data.SQLite.Resources.AuthStore.migrations")
            return std::string(AuthStoreMigration);
        return {};
    }
};
class GridUserResources : public MigrationResources {
  public:
    std::vector<std::string> names() override {
        return {"OpenSim.Data.SQLite.Resources.001_GridUserStore.sql"};
    }
    std::optional<std::string> bytes(const std::string& name) override {
        if (name == "OpenSim.Data.SQLite.Resources.001_GridUserStore.sql") {
            std::string value;
            std::string_view hex = GridUserStoreMigrationHex;
            auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
            for (size_t i = 0; i < hex.size(); i += 2)
                value += static_cast<char>((nibble(hex[i]) << 4) | nibble(hex[i + 1]));
            return value;
        }
        return {};
    }
};
}
std::shared_ptr<MigrationDatabase> sqlite_migration_database(sqlite3* db) {
    return std::make_shared<Database>(db);
}
std::shared_ptr<MigrationResources> asset_store_migrations() {
    return std::make_shared<AssetResources>();
}
std::shared_ptr<MigrationResources> friends_store_migrations() {
    return std::make_shared<FriendsResources>();
}
std::shared_ptr<MigrationResources> mute_list_migrations() {
    return std::make_shared<MuteListResources>();
}
std::shared_ptr<MigrationResources> avatar_store_migrations() {
    return std::make_shared<AvatarResources>();
}
std::shared_ptr<MigrationResources> grid_user_store_migrations() {
    return std::make_shared<GridUserResources>();
}
std::shared_ptr<MigrationResources> user_account_store_migrations() {
    return std::make_shared<UserAccountResources>();
}
std::shared_ptr<MigrationResources> auth_store_migrations() {
    return std::make_shared<AuthResources>();
}
}
