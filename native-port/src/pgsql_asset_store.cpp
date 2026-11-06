// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "pgsql_asset_store.hpp"
#include "pgsql_asset_store_migration_resource.hpp"
#include "postgres_connection.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <charconv>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace opensim::services {
namespace {
using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;
constexpr auto resource_name = "OpenSim.Data.PGSQL.Resources.AssetStore.migrations";

Result checked(PGconn* connection, PGresult* response) {
    Result result(response, PQclear);
    if (!result || (PQresultStatus(result.get()) != PGRES_COMMAND_OK &&
                    PQresultStatus(result.get()) != PGRES_TUPLES_OK))
        throw std::runtime_error(std::string("PostgreSQL asset query failed: ") + PQerrorMessage(connection));
    return result;
}

Result query(PGconn* connection, const char* sql, std::initializer_list<const char*> parameters) {
    return checked(connection, PQexecParams(connection, sql, static_cast<int>(parameters.size()), nullptr,
                                            parameters.begin(), nullptr, nullptr, 0));
}

int integer(PGresult* rows, int row, int column) {
    const std::string_view value = PQgetvalue(rows, row, column);
    int number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("Invalid PostgreSQL asset integer");
    return number;
}

std::int8_t asset_type(PGresult* rows, int row) {
    const auto type = integer(rows, row, 3);
    if (type < -128 || type > 127)
        throw std::runtime_error("PostgreSQL asset type is outside the signed byte range");
    return static_cast<std::int8_t>(type);
}

class AssetMigrations final : public MigrationResources {
public:
    std::vector<std::string> names() override { return {resource_name}; }
    std::optional<std::string> bytes(const std::string& name) override {
        return name == resource_name ? std::optional<std::string>(PgsqlAssetStoreMigration) : std::nullopt;
    }
};

void migrate(PGconn* connection) {
    const auto execute = [&](const char* sql) { return checked(connection, PQexec(connection, sql)); };
    execute("BEGIN");
    execute("SELECT pg_advisory_xact_lock(hashtext(current_schema()), hashtext('migrations'))");
    execute("CREATE TABLE IF NOT EXISTS migrations (name varchar(100), version int)");
    execute("INSERT INTO migrations (name,version) SELECT 'migrations',1"
            " WHERE NOT EXISTS (SELECT 1 FROM migrations WHERE name='migrations' AND version >= 1)");
    const auto stamp = execute("SELECT COALESCE(MAX(version),0) FROM migrations WHERE name='AssetStore'");
    auto version = integer(stamp.get(), 0, 0);
    Migration scripts(nullptr, std::make_shared<AssetMigrations>(), std::string("AssetStore"));
    for (const auto& [next, batches] : scripts.migrations_after(version)) {
        for (const auto& batch : batches) {
            // The checked-in script wraps individual revisions. Keep those SQL
            // statements and stamp them under the same outer transaction.
            std::istringstream input(batch);
            std::string sql;
            for (std::string line; std::getline(input, line);) {
                auto command = framework::trim_utf8(line);
                for (auto& character : command)
                    if (character >= 'A' && character <= 'Z')
                        character += 'a' - 'A';
                if (command != "begin transaction;" && command != "commit;")
                    sql += line + '\n';
            }
            if (!framework::trim_utf8(sql).empty())
                execute(sql.c_str());
        }
        const auto value = std::to_string(next);
        if (version < 1)
            query(connection, "INSERT INTO migrations (name,version) VALUES ('AssetStore',$1)",
                  {value.c_str()});
        else
            query(connection, "UPDATE migrations SET version=$1 WHERE name='AssetStore'", {value.c_str()});
        version = next;
    }
    execute("COMMIT");
}

class PgsqlAssetStore final : public InitializableAssetStore {
public:
    explicit PgsqlAssetStore(NativeAssetStorageBackend::Log log) : log_(std::move(log)) {}

    void initialise(const std::string& settings) override {
        const auto connection = connect_postgres(settings);
        migrate(connection.get());
        settings_ = settings;
    }

    void dispose() override {}

    AssetService::Asset get(framework::Uuid id) override {
        const auto connection = connect_postgres(settings_);
        const auto rows = query(connection.get(),
            "SELECT id,name,description,\"assetType\",local,temporary,creatorid,asset_flags,data "
            "FROM assets WHERE id=$1", {id.str().c_str()});
        if (PQntuples(rows.get()) == 0)
            return {};
        auto asset = std::make_shared<framework::AssetBase>(
            framework::Uuid(PQgetvalue(rows.get(), 0, 0)), PQgetvalue(rows.get(), 0, 1),
            asset_type(rows.get(), 0), PQgetvalue(rows.get(), 0, 6));
        asset->description(PQgetvalue(rows.get(), 0, 2));
        asset->local(std::string_view(PQgetvalue(rows.get(), 0, 4)) == "t");
        asset->temporary(std::string_view(PQgetvalue(rows.get(), 0, 5)) == "t");
        asset->flags(integer(rows.get(), 0, 7));
        std::size_t size = 0;
        std::unique_ptr<unsigned char, decltype(&PQfreemem)> bytes(
            PQunescapeBytea(reinterpret_cast<const unsigned char*>(PQgetvalue(rows.get(), 0, 8)), &size),
            PQfreemem);
        if (!bytes)
            throw std::runtime_error("Could not decode PostgreSQL asset bytes");
        asset->data = std::make_shared<std::vector<std::uint8_t>>(bytes.get(), bytes.get() + size);
        return asset;
    }

    bool store(AssetService::Asset asset) override {
        const auto id = asset->full_id().str();
        const auto name = truncate(asset->name().value(), "Name", id, framework::AssetBase::MaxAssetName);
        const auto description = truncate(asset->description().value(), "Description", id,
                                          framework::AssetBase::MaxAssetDescription);
        const auto type = std::to_string(asset->type());
        const auto flags = std::to_string(asset->flags());
        const auto creator = asset->creator_id();
        const auto ticks = framework::shared_native_invariant_datetime_context()->now().ticks();
        const auto seconds = (ticks - 621355968000000000LL) / 10000000;
        const auto timestamp = std::to_string(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(seconds)));
        const auto& data = asset->data;
        if (data && data->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            throw std::length_error("PostgreSQL asset exceeds the binary parameter limit");
        const char* binary = !data ? nullptr
                             : data->empty() ? "" : reinterpret_cast<const char*>(data->data());
        const char* values[] = {id.c_str(), name.c_str(), description.c_str(), type.c_str(),
            asset->local() ? "true" : "false", asset->temporary() ? "true" : "false",
            timestamp.c_str(), creator ? creator->c_str() : nullptr, flags.c_str(), binary};
        const int lengths[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, data ? static_cast<int>(data->size()) : 0};
        const int formats[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        const auto connection = connect_postgres(settings_);
        try {
            if (name.find('\0') != std::string::npos || description.find('\0') != std::string::npos ||
                (creator && creator->find('\0') != std::string::npos))
                throw std::invalid_argument("PostgreSQL asset text contains an embedded NUL");
            checked(connection.get(), PQexecParams(connection.get(),
                "INSERT INTO assets (id,name,description,\"assetType\",local,temporary,create_time,access_time,"
                "creatorid,asset_flags,data) VALUES ($1,$2,$3,$4,$5,$6,$7,$7,$8,$9,$10) "
                "ON CONFLICT (id) DO UPDATE SET name=EXCLUDED.name,description=EXCLUDED.description,"
                "\"assetType\"=EXCLUDED.\"assetType\",local=EXCLUDED.local,temporary=EXCLUDED.temporary,"
                "creatorid=EXCLUDED.creatorid,data=EXCLUDED.data",
                10, nullptr, values, lengths, formats, 0));
        } catch (const std::exception& failure) {
            // The original PostgreSQL provider logs execution errors and returns true.
            if (log_)
                log_("error", failure.what());
        }
        return true;
    }

    std::vector<bool> exists(const std::vector<framework::Uuid>& ids) override {
        if (ids.empty())
            return {};
        std::string parameters = "{";
        for (const auto& id : ids) {
            if (parameters.size() != 1)
                parameters += ',';
            parameters += id.str();
        }
        parameters += '}';
        const auto connection = connect_postgres(settings_);
        const auto rows = query(connection.get(), "SELECT id FROM assets WHERE id=ANY($1::uuid[])",
                                {parameters.c_str()});
        std::unordered_set<framework::Uuid, framework::UuidHash> found;
        for (int row = 0; row < PQntuples(rows.get()); ++row)
            found.emplace(PQgetvalue(rows.get(), row, 0));
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
        const auto offset = std::to_string(start);
        // The original provider passes start + count - 1 as LIMIT.
        const auto limit = std::to_string(std::bit_cast<std::int32_t>(
            static_cast<std::uint32_t>(start) + static_cast<std::uint32_t>(count) - 1u));
        const auto connection = connect_postgres(settings_);
        const auto rows = query(connection.get(),
            "SELECT id,name,description,\"assetType\",temporary,creatorid FROM assets ORDER BY id LIMIT $1 OFFSET $2",
            {limit.c_str(), offset.c_str()});
        std::vector<std::shared_ptr<framework::AssetMetadata>> result;
        for (int row = 0; row < PQntuples(rows.get()); ++row) {
            auto metadata = std::make_shared<framework::AssetMetadata>();
            metadata->full_id(framework::Uuid(PQgetvalue(rows.get(), row, 0)));
            metadata->name = PQgetvalue(rows.get(), row, 1);
            metadata->description = PQgetvalue(rows.get(), row, 2);
            metadata->type = asset_type(rows.get(), row);
            metadata->temporary = std::string_view(PQgetvalue(rows.get(), row, 4)) == "t";
            metadata->creator_id = PQgetvalue(rows.get(), row, 5);
            result.push_back(std::move(metadata));
        }
        return result;
    }

    bool erase(const std::string&) override { return false; }

private:
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

std::shared_ptr<InitializableAssetStore> make_pgsql_asset_store(NativeAssetStorageBackend::Log log) {
    return std::make_shared<PgsqlAssetStore>(std::move(log));
}
}
