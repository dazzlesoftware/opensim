// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "mysql_asset_store.hpp"
#include "mysql_asset_store_migration_resource.hpp"
#include "mysql_connection.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <charconv>
#include <chrono>
#include <limits>
#include <unordered_set>

namespace opensim::services {
namespace {
using Result = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;
constexpr auto resource_name = "OpenSim.Data.MySQL.Resources.AssetStore.migrations";

std::runtime_error database_error(MYSQL* db) {
    return std::runtime_error(std::string("MySQL asset query failed: ") + mysql_error(db));
}

Result query(MYSQL* db, const std::string& sql) {
    if (sql.size() > std::numeric_limits<unsigned long>::max())
        throw std::length_error("MySQL asset query exceeds the client limit");
    if (mysql_real_query(db, sql.data(), static_cast<unsigned long>(sql.size())) != 0)
        throw database_error(db);
    Result rows(mysql_store_result(db), mysql_free_result);
    if (!rows && mysql_field_count(db) != 0)
        throw database_error(db);
    return rows;
}

std::string binary_literal(std::string_view data) {
    constexpr std::string_view digits = "0123456789abcdef";
    std::string result = "X'";
    for (const unsigned char byte : data) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    result += '\'';
    return result;
}

std::string text_literal(std::string_view text) {
    return "CONVERT(" + binary_literal(text) + " USING utf8mb4)";
}

void store_binary(MYSQL* db, const std::string& sql, const framework::AssetBytes& data) {
    std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)> statement(mysql_stmt_init(db), mysql_stmt_close);
    if (!statement)
        throw database_error(db);
    const auto check = [&](int status) {
        if (status != 0)
            throw std::runtime_error(std::string("MySQL asset write failed: ") + mysql_stmt_error(statement.get()));
    };
    check(mysql_stmt_prepare(statement.get(), sql.data(), static_cast<unsigned long>(sql.size())));
    if (data && data->size() > std::numeric_limits<unsigned long>::max())
        throw std::length_error("MySQL asset exceeds the binary parameter limit");
    unsigned long length = data ? static_cast<unsigned long>(data->size()) : 0;
    char empty = 0;
    MYSQL_BIND parameter{};
    parameter.buffer_type = data ? MYSQL_TYPE_LONG_BLOB : MYSQL_TYPE_NULL;
    parameter.buffer = data && !data->empty() ? static_cast<void*>(data->data()) : &empty;
    parameter.buffer_length = length;
    parameter.length = &length;
    check(mysql_stmt_bind_param(statement.get(), &parameter));
    check(mysql_stmt_execute(statement.get()));
}

int integer(std::string_view text) {
    int number = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("Invalid MySQL asset integer");
    return number;
}

std::int8_t asset_type(std::string_view text) {
    const auto type = integer(text);
    if (type < -128 || type > 127)
        throw std::runtime_error("MySQL asset type is outside the signed byte range");
    return static_cast<std::int8_t>(type);
}

const char* required_field(MYSQL_ROW row, int column) {
    if (!row[column])
        throw std::runtime_error("Missing MySQL asset field");
    return row[column];
}

class AssetMigrations final : public MigrationResources {
public:
    std::vector<std::string> names() override { return {resource_name}; }
    std::optional<std::string> bytes(const std::string& name) override {
        return name == resource_name ? std::optional<std::string>(MysqlAssetStoreMigration) : std::nullopt;
    }
};

void migrate(const std::string& settings) {
    const auto db = connect_mysql(settings, CLIENT_MULTI_STATEMENTS);
    constexpr auto lock_name = "SHA2(CONCAT(DATABASE(), ':migrations'), 256)";
    const auto lock = query(db.get(), std::string("SELECT GET_LOCK(") + lock_name + ", 30)");
    const auto acquired = mysql_fetch_row(lock.get());
    if (!acquired || !acquired[0] || std::string_view(acquired[0]) != "1")
        throw std::runtime_error("Cannot acquire MySQL asset migration lock");

    query(db.get(), "CREATE TABLE IF NOT EXISTS migrations (name varchar(100), version int)");
    query(db.get(), "INSERT INTO migrations (name,version) SELECT 'migrations',1"
                    " WHERE NOT EXISTS (SELECT 1 FROM migrations WHERE name='migrations' AND version >= 1)");
    const auto stamp = query(db.get(), "SELECT COALESCE(MAX(version),0) FROM migrations WHERE name='AssetStore'");
    auto version = integer(mysql_fetch_row(stamp.get())[0]);
    Migration scripts(nullptr, std::make_shared<AssetMigrations>(), std::string("AssetStore"));
    for (const auto& [next, batches] : scripts.migrations_after(version)) {
        for (const auto& sql : batches) {
            query(db.get(), sql);
            while (mysql_more_results(db.get())) {
                if (mysql_next_result(db.get()) != 0)
                    throw database_error(db.get());
                Result rows(mysql_store_result(db.get()), mysql_free_result);
                if (!rows && mysql_field_count(db.get()) != 0)
                    throw database_error(db.get());
            }
        }
        // MariaDB/MySQL implicitly commit the DDL. The original CREATE IF NOT
        // EXISTS can be retried if startup fails before the version is written.
        if (version < 1)
            query(db.get(), "INSERT INTO migrations (name,version) VALUES ('AssetStore'," +
                            std::to_string(next) + ")");
        else
            query(db.get(), "UPDATE migrations SET version=" + std::to_string(next) + " WHERE name='AssetStore'");
        version = next;
    }
    query(db.get(), std::string("SELECT RELEASE_LOCK(") + lock_name + ")");
}

class MysqlAssetStore final : public InitializableAssetStore {
public:
    explicit MysqlAssetStore(NativeAssetStorageBackend::Log log) : log_(std::move(log)) {}

    void initialise(const std::string& settings) override {
        migrate(settings);
        settings_ = settings;
    }

    void dispose() override {}

    AssetService::Asset get(framework::Uuid id) override {
        const auto db = connect_mysql(settings_);
        AssetService::Asset asset;
        try {
            const auto rows = query(db.get(),
                "SELECT name,description,assetType,local,temporary,asset_flags,CreatorID,data "
                "FROM assets WHERE id=" + text_literal(id.str()));
            if (const auto row = mysql_fetch_row(rows.get())) {
                const auto sizes = mysql_fetch_lengths(rows.get());
                asset = std::make_shared<framework::AssetBase>(
                    id, required_field(row, 0), asset_type(required_field(row, 2)), row[6] ? row[6] : "");
                const auto* data = reinterpret_cast<const std::uint8_t*>(required_field(row, 7));
                asset->data = std::make_shared<std::vector<std::uint8_t>>(data, data + sizes[7]);
                asset->description(required_field(row, 1));
                const std::string_view local = required_field(row, 3);
                asset->local(local == "1" || framework::ordinal_equals_ignore_case(local, "true"));
                asset->temporary(integer(required_field(row, 4)) != 0);
                asset->flags(integer(required_field(row, 5)));
            }
        } catch (const std::exception& failure) {
            error(failure);
        }
        return asset;
    }

    bool store(AssetService::Asset asset) override {
        const auto id = asset->id();
        const auto name = truncate(asset->name().value(), "Name", id, framework::AssetBase::MaxAssetName);
        const auto description = truncate(asset->description().value(), "Description", id,
                                          framework::AssetBase::MaxAssetDescription);
        const auto db = connect_mysql(settings_);
        try {
            const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            const auto time = std::to_string(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(now)));
            const auto creator = asset->creator_id();
            store_binary(db.get(),
                "REPLACE INTO assets (id,name,description,assetType,local,temporary,create_time,access_time,"
                "asset_flags,CreatorID,data) VALUES (" +
                text_literal(id) + ',' + text_literal(name) + ',' + text_literal(description) + ',' +
                std::to_string(asset->type()) + ',' + std::to_string(asset->local()) + ',' +
                std::to_string(asset->temporary()) + ',' + time + ',' + time + ',' +
                std::to_string(asset->flags()) + ',' + (creator ? text_literal(*creator) : "NULL") + ",?)",
                asset->data);
            return true;
        } catch (const std::exception& failure) {
            error(failure);
            return false;
        }
    }

    std::vector<bool> exists(const std::vector<framework::Uuid>& ids) override {
        if (ids.empty())
            return {};
        std::string sql = "SELECT id FROM assets WHERE id IN (";
        for (const auto& id : ids)
            sql += text_literal(id.str()) + ',';
        sql.back() = ')';
        const auto db = connect_mysql(settings_);
        const auto rows = query(db.get(), sql);
        std::unordered_set<framework::Uuid, framework::UuidHash> found;
        while (const auto row = mysql_fetch_row(rows.get()))
            found.emplace(required_field(row, 0));
        std::vector<bool> result;
        result.reserve(ids.size());
        for (const auto& id : ids)
            result.push_back(found.contains(id));
        return result;
    }

    std::vector<std::shared_ptr<framework::AssetMetadata>> metadata_set(std::int32_t start,
                                                                       std::int32_t count) override {
        if (count < 0)
            throw std::out_of_range("Negative asset metadata capacity");
        try {
            const auto db = connect_mysql(settings_);
            const auto rows = query(db.get(),
                "SELECT name,description,assetType,temporary,id,asset_flags,CreatorID FROM assets LIMIT " +
                std::to_string(start) + ',' + std::to_string(count));
            std::vector<std::shared_ptr<framework::AssetMetadata>> result;
            while (const auto row = mysql_fetch_row(rows.get())) {
                auto metadata = std::make_shared<framework::AssetMetadata>();
                metadata->full_id(framework::Uuid(required_field(row, 4)));
                metadata->name = required_field(row, 0);
                metadata->description = required_field(row, 1);
                metadata->type = asset_type(required_field(row, 2));
                metadata->temporary = integer(required_field(row, 3)) != 0;
                metadata->flags = integer(required_field(row, 5));
                metadata->creator_id = row[6] ? row[6] : "";
                metadata->sha1 = std::make_shared<std::vector<std::uint8_t>>();
                result.push_back(std::move(metadata));
            }
            return result;
        } catch (const std::exception& failure) {
            error(failure);
            return {};
        }
    }

    bool erase(const std::string& id) override {
        try {
            const auto db = connect_mysql(settings_);
            query(db.get(), "DELETE FROM assets WHERE id=" + text_literal(id));
            return true;
        } catch (const std::exception& failure) {
            error(failure);
            return false;
        }
    }

private:
    void error(const std::exception& failure) const {
        if (log_)
            log_("error", failure.what());
    }

    std::string truncate(const std::string& value, const char* field, const std::string& id, std::size_t limit) {
        const auto units = framework::utf16_replacing(value);
        if (units.size() <= limit)
            return value;
        if (log_)
            log_("warn", std::string("[ASSET DB]: ") + field + " '" + value + "' for asset " + id +
                         " truncated from " + std::to_string(units.size()) + " to " +
                         std::to_string(limit) + " characters on add");
        return framework::utf8(std::u16string_view(units).substr(0, limit));
    }

    NativeAssetStorageBackend::Log log_;
    std::string settings_;
};
}

std::shared_ptr<InitializableAssetStore> make_mysql_asset_store(NativeAssetStorageBackend::Log log) {
    return std::make_shared<MysqlAssetStore>(std::move(log));
}
}
