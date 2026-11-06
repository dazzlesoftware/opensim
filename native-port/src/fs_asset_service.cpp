// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "fs_asset_service.hpp"
#include "opensim/services/fs_asset_files.hpp"
#include "opensim/services/hg_asset_service.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/console_command_resolver.hpp"
#include "asset_console_format.hpp"

#if defined(OPENSIM_HAVE_MYSQL) || defined(OPENSIM_HAVE_POSTGRESQL)
#include "sql_service_connection.hpp"
#include "fs_asset_migration_resource.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include <bit>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>
#endif

namespace opensim::services {
#if defined(OPENSIM_HAVE_MYSQL) || defined(OPENSIM_HAVE_POSTGRESQL)
namespace {
using namespace std::chrono_literals;
constexpr auto migration = "FSAssetStore";

std::string provider_setting(const server::IniConfig& config, const char* key) {
    auto value = config.get("AssetService", key);
    return value.empty() ? config.get("DatabaseService", key) : value;
}

bool use_postgres(const server::IniConfig& config) {
    const auto provider = provider_setting(config, "StorageProvider");
#ifdef OPENSIM_HAVE_POSTGRESQL
    if (provider == "OpenSim.Data.PGSQL.dll" || provider == "OpenSim.Data.PGSQL.dll:PGSQLFSAssetData")
        return true;
#endif
#ifdef OPENSIM_HAVE_MYSQL
    if (provider == "OpenSim.Data.MySQL.dll" || provider == "OpenSim.Data.MySQL.dll:MySQLFSAssetData")
        return false;
#endif
    throw std::invalid_argument("Unsupported filesystem asset metadata provider: " + provider);
}

std::string postgres_timestamp() {
    const auto ticks = framework::shared_native_invariant_datetime_context()->now().ticks();
    const auto seconds = (ticks - 621355968000000000LL) / 10000000;
    return std::to_string(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(seconds)));
}

int number(std::string_view text) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw std::runtime_error("Invalid filesystem asset metadata number");
    return value;
}

std::string_view bytes_view(const framework::AssetBytes& data) {
    if (!data || data->empty())
        return {};
    return {reinterpret_cast<const char*>(data->data()), data->size()};
}

class FsAssetService final : public AssetService {
public:
    FsAssetService(const server::IniConfig& config, std::shared_ptr<NativeAssetStorageBackend> backend)
        : settings_(provider_setting(config, "ConnectionString")),
          postgres_(use_postgres(config)),
          table_(config.get("AssetService", "Realm", "fsassets")),
          access_days_(config.integer("AssetService", "DaysBetweenAccessTimeUpdates", 0)),
          show_stats_(config.boolean("AssetService", "ShowConsoleStats", true)),
          files_(framework::utf8_path(config.get("AssetService", "BaseDirectory")),
                 framework::utf8_path(config.get("AssetService", "SpoolDirectory", "/tmp")),
                 config.boolean("AssetService", "UseOsgridFormat", false)),
          backend_(std::move(backend)) {
        if (table_.empty() || table_.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
            throw std::invalid_argument("Invalid filesystem asset metadata table name");
        const char quote = postgres_ ? '"' : '`';
        table_ = quote + table_ + quote;
        migrate();

        const auto fallback = config.get("AssetService", "FallbackService");
        if (!fallback.empty()) {
            if (fallback != "OpenSim.Services.AssetService.dll:AssetService")
                throw std::invalid_argument("Unsupported filesystem asset fallback service: " + fallback);
            // The original module passes the same AssetService configuration to its fallback.
            fallback_ = AssetStorageFactory(backend_).create(&config);
        }
        if (!config.boolean("AssetService", "SecondaryInstance", false)) {
            const auto loader = config.get("AssetService", "DefaultAssetLoader");
            if (!loader.empty()) {
                const auto assets = backend_->load_loader(loader);
                if (!assets)
                    throw std::invalid_argument("Unsupported filesystem default asset loader: " + loader);
                assets->for_each(config.get("AssetService", "AssetLoaderArgs"),
                                 [this](Asset asset) { store(std::move(asset)); });
            }
            writer_ = std::jthread([this](std::stop_token stop) { write_spool(stop); });
            if (show_stats_)
                reporter_ = std::jthread([this](std::stop_token stop) { report_reads(stop); });
        }
    }

    Asset get_asset(const std::string& id) override {
        return get_with_digest(id, nullptr);
    }

    Asset get_with_digest(const std::string& id, std::string* digest) {
        const auto started = std::chrono::steady_clock::now();
        auto [metadata, hash] = lookup(id);
        if (digest)
            *digest = hash;
        const auto bytes = metadata ? files_.read(hash) : std::nullopt;
        if ((!bytes || bytes->empty()) && fallback_) {
            if (auto asset = fallback_->get_asset(id)) {
                asset->metadata()->content_type(framework::asset_content_type(asset->type()));
                if (digest)
                    *digest = FsAssetFiles::digest(bytes_view(asset->data));
                backend_->info("[FSASSETS]: Added asset " + id + " from fallback to local store");
                store(asset);
                if (metadata)
                    sanitize(*asset);
                return asset;
            }
        }
        if (!metadata) {
            if (show_stats_) {
                std::lock_guard lock(stats_mutex_);
                ++stats_.missing;
            }
            return {};
        }
        auto asset = std::make_shared<framework::AssetBase>();
        asset->metadata(std::move(metadata));
        asset->data = bytes ? std::make_shared<std::vector<std::uint8_t>>(bytes->begin(), bytes->end())
                            : std::make_shared<std::vector<std::uint8_t>>();
        if (show_stats_) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            std::lock_guard lock(stats_mutex_);
            ++stats_.reads;
            stats_.milliseconds += elapsed;
            if (asset->data->empty())
                ++stats_.missing_files;
        }
        sanitize(*asset);
        return asset;
    }

    Asset get_cached(const std::string& id) override { return get_asset(id); }
    Asset get_foreign(const std::string&, const std::optional<std::string>&, bool) override { return {}; }
    void get(const std::string&, const std::optional<std::string>&, bool, Complete) override {}
    bool get_async(const std::string& id, std::any sender, Retrieved callback) override {
        callback(id, std::move(sender), get_asset(id));
        return true;
    }
    std::shared_ptr<framework::AssetMetadata> get_metadata(const std::string& id) override {
        return lookup(id).first;
    }
    framework::AssetBytes get_data(const std::string& id) override {
        const auto [metadata, hash] = lookup(id);
        if (!metadata)
            return {};
        const auto data = files_.read(hash).value_or("");
        return std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.end());
    }
    std::vector<bool> assets_exist(const std::vector<std::string>& ids) override {
        if (ids.empty())
            return {};
        sql::Parameters parameters;
        std::string statement = "SELECT id FROM " + table_ + " WHERE id IN (";
        for (const auto& id : ids) {
            parameters.emplace_back(framework::construct_sdk_uuid(framework::utf16(id)).str());
            statement += "?,";
        }
        statement.back() = ')';
        const auto execute = connect_for_read();
        if (!execute)
            return std::vector<bool>(ids.size(), false);
        const auto rows = execute(statement, parameters);
        std::unordered_set<framework::Uuid, framework::UuidHash> found;
        for (const auto& row : rows.rows)
            found.emplace(row.at(0));
        std::vector<bool> result;
        for (const auto& id : parameters)
            result.push_back(found.contains(framework::Uuid(*id)));
        return result;
    }
    std::optional<std::string> store(Asset asset) override {
        const auto hash = FsAssetFiles::digest(bytes_view(asset->data));
        asset->name(framework::truncate_utf16(asset->name().value(), framework::AssetBase::MaxAssetName));
        asset->description(framework::truncate_utf16(asset->description().value(),
                                                     framework::AssetBase::MaxAssetDescription));
        const auto bytes = asset->type() == static_cast<int>(framework::AssetType::Object) && asset->data
                               ? sanitize_object_asset(asset->data) : asset->data;
        if (files_.stage(hash, bytes_view(bytes)))
            asset->data = bytes;
        if (asset->full_id().is_zero()) {
            framework::Uuid parsed;
            if (framework::try_parse_sdk_uuid(framework::utf16(asset->id()), parsed))
                asset->full_id(parsed);
            else
                asset->full_id(framework::Uuid::random());
        }

        const auto id = asset->id();
        try {
            if (!postgres_) {
                // MySQLFSAssetData checks metadata first, including its access-time update.
                // Once that succeeds, it acknowledges the store even if the insert fails.
                if (lookup(id).first)
                    return id;
                try {
                    connect()("INSERT INTO " + table_ +
                                  " (id,name,description,type,hash,asset_flags,create_time,access_time)"
                                  " VALUES (?,?,?,?,?,?,UNIX_TIMESTAMP(),UNIX_TIMESTAMP())",
                              {id, asset->name(), asset->description(), std::to_string(asset->type()),
                               hash, std::to_string(asset->flags())});
                } catch (const std::exception& error) {
                    backend_->error("[FSASSETS]: Failed to insert asset " + id, error);
                }
                return id;
            }
            const bool exists = static_cast<bool>(lookup(id).first);
            const auto execute = connect();
            const auto timestamp = postgres_timestamp();
            const auto key = asset->full_id().str();
            if (!exists) {
                execute("INSERT INTO " + table_ +
                            " (id,type,hash,asset_flags,create_time,access_time) VALUES (?,?,?,?,?,?)",
                        {key, std::to_string(asset->type()), hash, std::to_string(asset->flags()),
                         timestamp, timestamp});
                return id;
            }
            execute("UPDATE " + table_ + " SET access_time=? WHERE id=?", {timestamp, key});
            return asset->type() == static_cast<int>(framework::AssetType::OSMaterial)
                       ? id : framework::Uuid{}.str();
        } catch (const std::exception& error) {
            backend_->error("[FSASSETS]: Failed to store asset " + id, error);
            return asset->type() == static_cast<int>(framework::AssetType::OSMaterial)
                       ? id : framework::Uuid{}.str();
        }
    }
    bool update_content(const std::string&, framework::AssetBytes) override { return false; }
    bool erase(const std::string& id) override try {
        connect()("DELETE FROM " + table_ + " WHERE id=?",
                  {postgres_ ? framework::construct_sdk_uuid(framework::utf16(id)).str() : id});
        return true;
    } catch (const std::exception& error) {
        if (postgres_)
            throw;
        backend_->error("[FSASSETS]: Failed to delete asset " + id, error);
        return true;
    }

    using Output = std::function<void(const std::string&)>;

    void show_count(const std::vector<std::string>&, const Output& output) {
        const auto execute = connect_for_read();
        const auto count = execute ? execute("SELECT COUNT(*) FROM " + table_, {}).rows.at(0).at(0)
                                   : "0";
        output("Total asset count: " + count);
    }

    void show_digest(const std::vector<std::string>& args, const Output& output) {
        if (args.size() < 3) {
            output("Syntax: show digest <ID>");
            return;
        }
        std::string hash;
        const auto asset = get_with_digest(args[2], &hash);
        if (!asset || !asset->data || asset->data->empty()) {
            output("Asset not found");
            return;
        }
        output("Name: " + asset->name().value_or(""));
        output("Description: " + asset->description().value_or(""));
        output("Type: " + std::to_string(asset->type()));
        output("Content-type: " + asset->metadata()->content_type());
        output("Flags: " + server::asset_console_flags(asset->flags()));
        output("FS file: " + files_.relative_path(hash).string());
        server::asset_console_hex(*asset->data, output);
    }

    void delete_from_console(const std::vector<std::string>& args, const Output& output) {
        if (args.size() < 3) {
            output("Syntax: delete asset <ID>");
            return;
        }
        const auto asset = get_asset(args[2]);
        if (!asset || !asset->data || asset->data->empty()) {
            output("Asset not found");
            return;
        }
        erase(args[2]);
        output("Asset deleted");
    }

    void import_from_console(const std::vector<std::string>& args, const Output& output) try {
        const std::size_t first = args.front() == "force" ? 2 : 1;
        if (args.size() < first + 2) {
            output("Syntax: import <conn> <table> [<start> <count>]");
            return;
        }
        const auto table = import_table(args[first + 1]);
        const int start = args.size() > first + 2 ? number(args[first + 2]) : 0;
        const int count = args.size() > first + 3 ? number(args[first + 3]) : -1;
        auto statement = postgres_ ? "SELECT id,'','',\"assetType\",create_time,encode(data,'hex') FROM " + table
                                   : "SELECT id,name,description,assetType,create_time,data FROM " + table;
        if (count != -1) {
            // PGSQLFSAssetData reverses the values in its LIMIT/OFFSET clause.
            statement += postgres_ ? " LIMIT " + std::to_string(start) + " OFFSET " + std::to_string(count)
                                   : " LIMIT " + std::to_string(start) + ',' + std::to_string(count);
        }
        output("Querying database");
        output("Reading data");
        std::size_t imported = 0;
        const auto import_row = [&](const std::vector<std::string_view>& row) {
            if (imported % 100 == 0)
                output(std::to_string(imported) + " assets imported so far");
            auto asset = std::make_shared<framework::AssetBase>(framework::parse_sdk_uuid(row.at(0)),
                std::string(row.at(1)),
                std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(number(row.at(3)))), std::nullopt);
            asset->description(std::string(row.at(2)));
            asset->metadata()->content_type(framework::asset_content_type(asset->type()));
            asset->metadata()->creation_date = framework::AssetDateTime(
                621355968000000000LL + static_cast<std::int64_t>(number(row.at(4))) * 10000000, 1);
            const auto& data = row.at(5);
            if (postgres_) {
                if (data.size() % 2 != 0)
                    throw std::runtime_error("Invalid PostgreSQL asset bytes");
                asset->data = std::make_shared<std::vector<std::uint8_t>>();
                asset->data->reserve(data.size() / 2);
                for (std::size_t offset = 0; offset < data.size(); offset += 2) {
                    unsigned int byte = 0;
                    const auto end = data.data() + offset + 2;
                    const auto parsed = std::from_chars(data.data() + offset, end, byte, 16);
                    if (parsed.ec != std::errc{} || parsed.ptr != end)
                        throw std::runtime_error("Invalid PostgreSQL asset bytes");
                    asset->data->push_back(static_cast<std::uint8_t>(byte));
                }
            } else {
                asset->data = std::make_shared<std::vector<std::uint8_t>>(data.begin(), data.end());
            }
            // The original command passes the force flag, but its Store implementation ignores it.
            store(std::move(asset));
            ++imported;
        };
#ifdef OPENSIM_HAVE_POSTGRESQL
        if (postgres_)
            sql::stream_postgres(args[first], statement, import_row);
#endif
#ifdef OPENSIM_HAVE_MYSQL
        if (!postgres_)
            sql::stream_mysql(args[first], statement, import_row);
#endif
        output("Import done, " + std::to_string(imported) + " assets imported");
    } catch (const std::exception& error) {
        backend_->error("[FSASSETS]: Error importing assets: ", error);
    }

private:
    std::string import_table(std::string_view name) const {
        const auto quote = postgres_ ? '"' : '`';
        std::string result;
        while (!name.empty()) {
            const auto dot = name.find('.');
            const auto part = name.substr(0, dot);
            if (part.empty() || part.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string_view::npos)
                throw std::invalid_argument("Invalid filesystem import table name");
            if (!result.empty())
                result += '.';
            result += quote;
            result += part;
            result += quote;
            if (dot == std::string_view::npos)
                return result;
            name.remove_prefix(dot + 1);
        }
        throw std::invalid_argument("Invalid filesystem import table name");
    }

    sql::Execute connect() const { return connect(settings_); }

    sql::Execute connect(const std::string& settings) const {
#ifdef OPENSIM_HAVE_POSTGRESQL
        if (postgres_)
            return sql::postgres(settings);
#endif
#ifdef OPENSIM_HAVE_MYSQL
        return sql::mysql(settings);
#else
        throw std::logic_error("MySQL filesystem index is unavailable");
#endif
    }

    void migrate() {
        const auto execute = connect();
        if (postgres_) {
            execute("BEGIN", {});
            execute("SELECT pg_advisory_xact_lock(hashtext(current_schema()), hashtext('migrations'))", {});
        } else {
            const auto lock = execute("SELECT GET_LOCK(SHA2(CONCAT(DATABASE(), ':migrations'), 256),30)", {});
            if (lock.rows.at(0).at(0) != "1")
                throw std::runtime_error("Cannot acquire filesystem asset migration lock");
        }
        const auto version = sql::initialize_migrations(execute, migration);
        if (version < 1) {
            const std::string_view source = postgres_ ? PgsqlFsAssetMigration : FsAssetMigration;
            const auto start = source.find("CREATE TABLE");
            const auto end = source.find(';', start);
            auto statement = std::string(source.substr(start, end - start));
            // DDL commits implicitly. Keep the revision retryable if stamping fails.
            if (!postgres_)
                statement.insert(sizeof("CREATE TABLE") - 1, " IF NOT EXISTS");
            execute(statement, {});
            sql::record_migration(execute, migration, version, 1);
        }
        if (postgres_)
            execute("COMMIT", {});
    }

    sql::Execute connect_for_read() const try {
        return connect();
    } catch (const std::exception& error) {
        if (postgres_)
            throw;
        backend_->error("[FSASSETS]: Database open failed", error);
        return {};
    }

    std::pair<std::shared_ptr<framework::AssetMetadata>, std::string> lookup(const std::string& id) {
        const auto postgres_id = postgres_
            ? std::optional{framework::construct_sdk_uuid(framework::utf16(id))}
            : std::nullopt;
        const auto execute = connect_for_read();
        if (!execute)
            return {};
        const auto names = postgres_ ? "'',''," : "name,description,";
        const auto key = postgres_id ? postgres_id->str() : id;
        const auto result = execute(std::string("SELECT ") + names + "type,hash,create_time,asset_flags,access_time"
                                    " FROM " + table_ + " WHERE id=?", {key});
        if (result.rows.empty())
            return {};
        const auto& row = result.rows.front();
        auto metadata = std::make_shared<framework::AssetMetadata>();
        metadata->full_id(postgres_id ? *postgres_id : framework::construct_sdk_uuid(framework::utf16(id)));
        metadata->name = row.at(0);
        metadata->description = row.at(1);
        metadata->type = std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(number(row.at(2))));
        metadata->content_type(framework::asset_content_type(metadata->type));
        metadata->creation_date = framework::AssetDateTime(
            621355968000000000LL + static_cast<std::int64_t>(number(row.at(4))) * 10000000, 1);
        metadata->flags = number(row.at(5));
        const auto now = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        if (access_days_ <= 0 || now - number(row.at(6)) >= static_cast<std::int64_t>(access_days_) * 86400) {
            if (postgres_)
                execute("UPDATE " + table_ + " SET access_time=? WHERE id=?", {postgres_timestamp(), key});
            else
                execute("UPDATE " + table_ + " SET access_time=UNIX_TIMESTAMP() WHERE id=?", {key});
        }
        return {std::move(metadata), row.at(3)};
    }

    static void sanitize(framework::AssetBase& asset) {
        if (asset.type() == static_cast<int>(framework::AssetType::Object) && asset.data)
            asset.data = sanitize_object_asset(asset.data);
    }

    void write_spool(std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::unique_lock lock(mutex);
        while (!stop.stop_requested()) {
            try {
                files_.flush();
            } catch (const std::exception& error) {
                backend_->error("[FSASSETS]: Failed to archive the spool", error);
            }
            wake.wait_for(lock, stop, 1s, [] { return false; });
        }
    }

    void report_reads(std::stop_token stop) {
        std::mutex mutex;
        std::condition_variable_any wake;
        std::unique_lock lock(mutex);
        while (!stop.stop_requested()) {
            wake.wait_for(lock, stop, 60s, [] { return false; });
            if (stop.stop_requested())
                break;
            ReadStats snapshot;
            {
                std::lock_guard stats_lock(stats_mutex_);
                snapshot = std::exchange(stats_, {});
            }
            if (snapshot.reads == 0)
                continue;
            std::ostringstream message;
            message << "[FSASSETS]: Read stats: " << snapshot.reads << " files, "
                    << snapshot.milliseconds << " ticks, avg " << std::fixed << std::setprecision(2)
                    << static_cast<double>(snapshot.milliseconds) / static_cast<double>(snapshot.reads)
                    << ", missing " << snapshot.missing << ", FS " << snapshot.missing_files;
            backend_->info(message.str());
        }
    }

    struct ReadStats {
        std::int64_t reads = 0;
        std::int64_t milliseconds = 0;
        std::int64_t missing = 0;
        std::int64_t missing_files = 0;
    };

    std::string settings_;
    bool postgres_;
    std::string table_;
    int access_days_;
    bool show_stats_;
    FsAssetFiles files_;
    std::shared_ptr<NativeAssetStorageBackend> backend_;
    std::shared_ptr<AssetService> fallback_;
    std::mutex stats_mutex_;
    ReadStats stats_;
    std::jthread writer_;
    std::jthread reporter_;
};
}
#endif

std::shared_ptr<AssetService> make_fs_asset_service(
    const server::IniConfig& config, std::shared_ptr<NativeAssetStorageBackend> backend) {
#if defined(OPENSIM_HAVE_MYSQL) || defined(OPENSIM_HAVE_POSTGRESQL)
    return std::make_shared<FsAssetService>(config, std::move(backend));
#else
    (void)config;
    (void)backend;
    throw std::invalid_argument("Filesystem asset service requires a SQL-enabled build");
#endif
}

void register_fs_asset_console(const std::shared_ptr<AssetService>& service,
                               framework::ConsoleCommandResolver& commands,
                               const std::function<void(const std::string&)>& output) {
#if defined(OPENSIM_HAVE_MYSQL) || defined(OPENSIM_HAVE_POSTGRESQL)
    const auto filesystem = std::dynamic_pointer_cast<FsAssetService>(service);
    if (!filesystem)
        return;
    const auto bind = [&](auto handler) {
        using Callback = framework::ConsoleCommandResolver::Callback::element_type;
        return std::make_shared<Callback>([weak = std::weak_ptr(filesystem), output, handler](const auto&, auto& args) {
            if (const auto service = weak.lock())
                ((*service).*handler)(args, output);
        });
    };
    commands.add({"fs", "show assets", "show assets", "Show asset stats", "", false},
                 bind(&FsAssetService::show_count));
    commands.add({"fs", "show digest", "show digest <ID>", "Show asset digest", "", false},
                 bind(&FsAssetService::show_digest));
    commands.add({"fs", "delete asset", "delete asset <ID>", "Delete asset from database", "", false},
                 bind(&FsAssetService::delete_from_console));
    commands.add({"fs", "import", "import <conn> <table> [<start> <count>]", "Import legacy assets", "", false},
                 bind(&FsAssetService::import_from_console));
    commands.add({"fs", "force import", "force import <conn> <table> [<start> <count>]",
                  "Import legacy assets, overwriting current content", "", false},
                 bind(&FsAssetService::import_from_console));
#else
    (void)service;
    (void)commands;
    (void)output;
#endif
}
}
