// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/asset_store.hpp"
#include "opensim/services/sqlite_migration.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/services/user_account.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_set>
namespace opensim::services {
namespace {
template <class T> T &required(const std::shared_ptr<T> &value) {
    if (!value)
        throw std::invalid_argument("Null asset data reference");
    return *value;
}
class Statement {
    sqlite3 *db_;
    sqlite3_stmt *value_ = nullptr;
    void check(int result) {
        if (result != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
    }

  public:
    Statement(sqlite3 *db, const std::string &sql) : db_(db) {
        if (!db)
            throw std::runtime_error("Asset database disposed");
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &value_, nullptr) != SQLITE_OK) {
            auto error = std::string(sqlite3_errmsg(db));
            if (value_)
                sqlite3_finalize(value_);
            throw std::runtime_error(error);
        }
    }
    ~Statement() {
        sqlite3_finalize(value_);
    }
    void bind(int index, const std::optional<std::string> &text) {
        check(text ? sqlite3_bind_text(value_, index, text->data(), static_cast<int>(text->size()),
                                       SQLITE_TRANSIENT)
                   : sqlite3_bind_null(value_, index));
    }
    void integer(int index, std::int64_t value) {
        check(sqlite3_bind_int64(value_, index, value));
    }
    void blob(int index, const framework::AssetBytes &value) {
        if (!value)
            check(sqlite3_bind_null(value_, index));
        else if (value->empty())
            check(sqlite3_bind_zeroblob(value_, index, 0));
        else
            check(sqlite3_bind_blob(value_, index, value->data(), static_cast<int>(value->size()),
                                    SQLITE_TRANSIENT));
    }
    int column(const std::string &name) const {
        for (int i = 0; i < sqlite3_column_count(value_); ++i)
            if (framework::ordinal_equals_ignore_case(sqlite3_column_name(value_, i), name))
                return i;
        throw std::out_of_range("Missing asset column: " + name);
    }
    bool row() {
        int result = sqlite3_step(value_);
        if (result == SQLITE_ROW)
            return true;
        if (result == SQLITE_DONE)
            return false;
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    std::string text(int col, bool null_empty = false) const {
        if (sqlite3_column_type(value_, col) == SQLITE_NULL) {
            if (null_empty)
                return "";
            throw std::invalid_argument("Null asset database string");
        }
        if (sqlite3_column_type(value_, col) != SQLITE_TEXT)
            throw std::invalid_argument("Asset database value is not a string");
        auto text = sqlite3_column_text(value_, col);
        return {reinterpret_cast<const char *>(text), static_cast<size_t>(sqlite3_column_bytes(value_, col))};
    }
    std::int32_t number(int col) const {
        int type = sqlite3_column_type(value_, col);
        const char *declared = sqlite3_column_decltype(value_, col);
        bool declared_integer = declared && std::string_view(declared) == "INTEGER";
        if (type == SQLITE_NULL)
            throw std::invalid_argument("Null asset database number");
        if (declared_integer) {
            auto value = sqlite3_column_int64(value_, col);
            if (value < INT32_MIN || value > INT32_MAX)
                throw std::out_of_range("Asset database integer overflow");
            return static_cast<int32_t>(value);
        }
        if (type == SQLITE_BLOB)
            throw std::invalid_argument("Invalid asset database number");
        if (type == SQLITE_TEXT) {
            std::int32_t value;
            if (!parse_account_integer(text(col), value))
                throw std::invalid_argument("Invalid asset database integer");
            return value;
        }
        long double value = type == SQLITE_FLOAT
                                ? std::nearbyint(sqlite3_column_double(value_, col))
                                : static_cast<long double>(sqlite3_column_int64(value_, col));
        if (!std::isfinite(value) || value < INT32_MIN || value > INT32_MAX)
            throw std::out_of_range("Asset database integer overflow");
        return static_cast<int32_t>(value);
    }

    std::int8_t byte(int col) const {
        auto value = number(col);
        if (value < INT8_MIN || value > INT8_MAX)
            throw std::out_of_range("Asset database type overflow");
        return static_cast<int8_t>(value);
    }
    bool boolean(int col) const {
        if (sqlite3_column_type(value_, col) == SQLITE_TEXT) {
            auto value = framework::trim_utf8(text(col));
            for (char &c : value)
                if (c >= 'A' && c <= 'Z')
                    c = static_cast<char>(c + 32);
            if (value == "true")
                return true;
            if (value == "false")
                return false;
            throw std::invalid_argument("Invalid asset database boolean");
        }
        if (sqlite3_column_type(value_, col) == SQLITE_NULL ||
            sqlite3_column_type(value_, col) == SQLITE_BLOB)
            throw std::invalid_argument("Invalid asset database boolean");
        return sqlite3_column_double(value_, col) != 0;
    }
    framework::AssetBytes bytes(int col) const {
        if (sqlite3_column_type(value_, col) != SQLITE_BLOB)
            throw std::invalid_argument("Asset database value is not binary");
        auto result = std::make_shared<std::vector<uint8_t>>();
        auto size = sqlite3_column_bytes(value_, col);
        if (size) {
            auto bytes = static_cast<const uint8_t *>(sqlite3_column_blob(value_, col));
            result->assign(bytes, bytes + size);
        }
        return result;
    }
};
framework::Uuid uuid(const std::string &value) {
    return framework::parse_sdk_uuid(value);
}
}
struct SqliteAssetStore::State {
    sqlite3 *db = nullptr;
    std::recursive_mutex mutex;
    ~State() {
        if (db)
            sqlite3_close(db);
    }
};
SqliteAssetStore::SqliteAssetStore(Warn warn) : state_(std::make_unique<State>()), warn_(std::move(warn)) {}
void SqliteAssetStore::open(const std::string &path, bool create) {
    std::lock_guard lock(state_->mutex);
    if (path.empty())
        throw std::invalid_argument("Empty asset database path");
    if (state_->db) {
        if (sqlite3_close(state_->db) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(state_->db));
        state_->db = nullptr;
    }
    if (sqlite3_open_v2(path.c_str(), &state_->db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | (create ? SQLITE_OPEN_CREATE : 0),
                        nullptr) != SQLITE_OK)
        throw std::runtime_error(state_->db ? sqlite3_errmsg(state_->db) : "SQLite allocation failed");
    sqlite3_busy_timeout(state_->db, 30000);
}
void SqliteAssetStore::migrate(std::shared_ptr<MigrationResources> resources, const MigrationLog &log) {
    std::lock_guard lock(state_->mutex);
    Migration migration(sqlite_migration_database(state_->db), std::move(resources),
                        std::string("AssetStore"), log);
    migration.update();
}
SqliteAssetStore::SqliteAssetStore(const std::string &path, bool initialize, Warn warn)
    : SqliteAssetStore(std::move(warn)) {
    open(path, initialize);
    if (initialize) {
        Statement command(
            state_->db,
            "CREATE TABLE IF NOT EXISTS assets(UUID NOT NULL PRIMARY KEY,Name,Description,Type,Local,Temporary,asset_flags INTEGER NOT NULL DEFAULT 0,CreatorID varchar(128) default '',Data)");
        command.row();
    }
    Statement check(
        state_->db,
        "SELECT UUID,Name,Description,Type,Local,Temporary,asset_flags,CreatorID,Data FROM assets LIMIT 0");
    check.row();
}
SqliteAssetStore::~SqliteAssetStore() = default;
void SqliteAssetStore::dispose() {
    std::lock_guard lock(state_->mutex);
    if (state_->db) {
        if (sqlite3_close(state_->db) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(state_->db));
        state_->db = nullptr;
    }
}
AssetService::Asset SqliteAssetStore::get(framework::Uuid id) {
    std::lock_guard lock(state_->mutex);
    Statement query(state_->db, "select * from assets where UUID=?");
    query.bind(1, id.str());
    if (!query.row())
        return {};
    auto full_id = uuid(query.text(query.column("UUID")));
    auto name = query.text(query.column("Name"));
    auto type = query.byte(query.column("Type"));
    auto creator = query.text(query.column("CreatorID"));
    auto asset = std::make_shared<framework::AssetBase>(full_id, name, type, creator);
    asset->description(query.text(query.column("Description")));
    asset->local(query.boolean(query.column("Local")));
    asset->temporary(query.boolean(query.column("Temporary")));
    asset->flags(query.number(query.column("asset_flags")));
    asset->data = query.bytes(query.column("Data"));
    return asset;
}

std::vector<bool> SqliteAssetStore::exists(const std::vector<framework::Uuid> &ids) {
    if (ids.empty())
        return {};
    std::string sql = "SELECT UUID FROM assets WHERE UUID IN (";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i)
            sql += ",";
        sql += "'" + ids[i].str() + "'";
    }
    sql += ")";
    std::unordered_set<framework::Uuid, framework::UuidHash> found;
    {
        std::lock_guard lock(state_->mutex);
        Statement query(state_->db, sql);
        while (query.row())
            found.insert(uuid(query.text(0)));
    }
    std::vector<bool> result;
    result.reserve(ids.size());
    for (auto id : ids)
        result.push_back(found.contains(id));
    return result;
}
bool SqliteAssetStore::store(AssetService::Asset asset) {
    auto asset_name = required(asset).name();
    auto name_units = framework::utf16_replacing(required(asset).name().value());
    if (name_units.size() > framework::AssetBase::MaxAssetName) {
        asset_name =
            framework::utf8(std::u16string_view(name_units).substr(0, framework::AssetBase::MaxAssetName));
        if (warn_)
            warn_("[ASSET DB]: Name '" + asset->name().value() + "' for asset " + asset->id() +
                  " truncated from " +
                  std::to_string(framework::utf16_replacing(asset->name().value()).size()) + " to " +
                  std::to_string(framework::AssetBase::MaxAssetName) + " characters on add");
    }
    auto description = asset->description();
    auto desc_units = framework::utf16_replacing(asset->description().value());
    if (desc_units.size() > framework::AssetBase::MaxAssetDescription) {
        description = framework::utf8(
            std::u16string_view(desc_units).substr(0, framework::AssetBase::MaxAssetDescription));
        if (warn_)
            warn_("[ASSET DB]: Description '" + asset->description().value() + "' for asset " + asset->id() +
                  " truncated from " +
                  std::to_string(framework::utf16_replacing(asset->description().value()).size()) + " to " +
                  std::to_string(framework::AssetBase::MaxAssetDescription) + " characters on add");
    }
    auto found = exists({asset->full_id()});
    bool update = !found.empty() && found[0];
    std::lock_guard lock(state_->mutex);
    Statement command(
        state_->db,
        update
            ? "UPDATE assets SET Name=?2,Description=?3,Type=?4,Local=?5,Temporary=?6,asset_flags=?7,CreatorID=?8,Data=?9 WHERE UUID=?1"
            : "INSERT INTO assets(UUID,Name,Description,Type,Local,Temporary,asset_flags,CreatorID,Data) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9)");
    command.bind(1, asset->full_id().str());
    command.bind(2, asset_name);
    command.bind(3, description);
    command.integer(4, asset->type());
    command.integer(5, asset->local());
    command.integer(6, asset->temporary());
    command.integer(7, asset->flags());
    command.bind(8, required(asset->metadata()).creator_id);
    command.blob(9, asset->data);
    command.row();
    return true;
}
std::vector<std::shared_ptr<framework::AssetMetadata>> SqliteAssetStore::metadata_set(std::int32_t start,
                                                                                      std::int32_t count) {
    if (count < 0)
        throw std::out_of_range("Negative asset metadata capacity");
    std::vector<std::shared_ptr<framework::AssetMetadata>> result;
    result.reserve(static_cast<size_t>(count));
    std::lock_guard lock(state_->mutex);
    Statement query(
        state_->db,
        "SELECT Name,Description,Type,Temporary,asset_flags,UUID,CreatorID FROM assets LIMIT ?1,?2");
    query.integer(1, start);
    query.integer(2, count);
    while (query.row()) {
        auto value = std::make_shared<framework::AssetMetadata>();
        value->full_id(uuid(query.text(5)));
        value->name = query.text(0);
        value->description = query.text(1);
        value->type = query.byte(2);
        value->temporary = query.boolean(3);
        value->flags = query.number(4);
        value->creator_id = query.text(6, true);
        value->sha1 = std::make_shared<std::vector<uint8_t>>();
        result.push_back(value);
    }
    return result;
}
bool SqliteAssetStore::erase_asset(framework::Uuid id) {
    std::lock_guard lock(state_->mutex);
    Statement command(state_->db, "DELETE FROM assets WHERE UUID=?");
    command.bind(1, id.str());
    command.row();
    return true;
}
bool SqliteAssetStore::erase(const std::string &id) {
    framework::Uuid parsed;
    if (!framework::try_parse_sdk_uuid(id, parsed))
        return false;
    return erase_asset(parsed);
}
}
