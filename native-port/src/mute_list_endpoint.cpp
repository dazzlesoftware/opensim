// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/mute_list_endpoint.hpp"

#include "opensim/framework/crc32.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/integer_text.hpp"
#include "opensim/framework/sdk_uuid.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/services/sqlite_connection.hpp"
#include "opensim/services/sqlite_generic_table.hpp"
#include "opensim/services/sqlite_migration.hpp"

#include <charconv>
#include <chrono>

namespace opensim::server {
namespace {
bool parse_signed_integer(std::string_view input, std::int32_t& value) {
    const auto trimmed = framework::trim_utf8(input);
    input = trimmed;
    if (input.starts_with('+')) {
        input.remove_prefix(1);
        if (input.starts_with('-'))
            return false;
    }
    const auto* end = input.data() + input.size();
    const auto parsed = std::from_chars(input.data(), end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}
}

MuteListEndpoint::MuteListEndpoint(const IniConfig& config) : auth_(config, "MuteListService") {
    if (config.get("MuteListService", "LocalServiceModule") !=
        "OpenSim.Services.MuteListService.dll:MuteListService")
        throw std::invalid_argument("MuteListService requires the OpenSimulator local service module");
    const auto provider = config.get("MuteListStore", "StorageProvider",
                                     config.get("DatabaseService", "StorageProvider"));
    auto connection = config.get("MuteListStore", "ConnectionString",
        config.get("DatabaseService", "MuteConnectionString",
                   config.get("DatabaseService", "ConnectionString")));
#ifdef OPENSIM_HAVE_MYSQL
    if (provider == "OpenSim.Data.MySQL.dll") {
        store_ = services::mysql_mute_list_store(connection);
        return;
    }
#endif
#ifdef OPENSIM_HAVE_POSTGRESQL
    if (provider == "OpenSim.Data.PGSQL.dll") {
        store_ = services::postgres_mute_list_store(connection);
        return;
    }
#endif
    if (provider != "OpenSim.Data.SQLite.dll")
        throw std::invalid_argument("Unsupported native mute-list storage provider: " + provider);
    if (connection.find('=') != std::string::npos)
        connection = services::sqlite_connection_path(services::parse_sqlite_connection(connection));
    if (connection.empty())
        connection = "native.db";
    if (connection != ":memory:") {
        auto path = framework::utf8_path(connection);
        if (path.is_relative())
            path = config.directory / path;
        const auto utf8 = path.u8string();
        connection.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
    auto database = std::make_shared<services::SqliteDatabase>();
    database->open_path(connection, true);
    const auto table = std::make_shared<services::SqliteTableOperations>(
        [database] { return database; }, "MuteList");
    services::Migration migration(services::sqlite_migration_database(database->handle()),
                                  services::mute_list_migrations(), "MuteListStore");
    if (migration.version() <= 0) {
        std::vector<std::string> columns;
        bool agent_primary_key = false;
        table->query("PRAGMA table_info(MuteList)", {}, {"name", "pk"},
                     [&](const auto& row, const auto&) {
            auto name = row.string_value(row.column("name"));
            if (name == "AgentID")
                agent_primary_key = row.int32_value(row.column("pk")) == 1;
            columns.push_back(std::move(name));
        });
        if (!columns.empty()) {
            // Adopt databases created by the native port before it recorded migrations.
            if (!agent_primary_key || columns != std::vector<std::string>{
                    "AgentID", "MuteID", "MuteName", "MuteType", "MuteFlags", "Stamp"})
                throw std::runtime_error("Unexpected unversioned SQLite MuteList schema");
            migration.init_migrations_table();
            migration.version(1);
        }
    }
    migration.update();
    store_.read = [table](framework::Uuid agent) {
        std::string data;
        table->query(
            "SELECT MuteID,MuteName,MuteType,MuteFlags FROM MuteList WHERE AgentID=:agent",
            {{":agent", agent.str()}}, {"MuteID", "MuteName", "MuteType", "MuteFlags"},
            [&](const auto& row, const auto&) {
                data += std::to_string(row.int32_value(2)) + " " + row.string_value(0) + " " +
                        row.string_value(1) + "|" + std::to_string(row.int32_value(3)) + "\n";
            });
        return data;
    };
    store_.write = [table](framework::Uuid agent, framework::Uuid mute, const std::string& name,
                           std::int32_t type, std::int32_t flags, std::int64_t stamp) {
        return table->execute(
            "REPLACE INTO MuteList (AgentID,MuteID,MuteName,MuteType,MuteFlags,Stamp)"
            " VALUES (:agent,:mute,:name,:type,:flags,:stamp)",
            {{":agent", agent.str()}, {":mute", mute.str()}, {":name", name},
             {":type", std::to_string(type)}, {":flags", std::to_string(flags)},
             {":stamp", std::to_string(stamp)}}) > 0;
    };
    store_.erase = [table](framework::Uuid agent, framework::Uuid mute, const std::string& name) {
        return table->execute(
            "DELETE FROM MuteList WHERE AgentID=:agent AND MuteID=:mute AND MuteName=:name",
            {{":agent", agent.str()}, {":mute", mute.str()}, {":name", name}}) > 0;
    };
}

std::vector<std::uint8_t> MuteListEndpoint::get(framework::Uuid agent, std::uint32_t crc) {
    const auto data = store_.read(agent);
    std::vector<std::uint8_t> bytes(data.begin(), data.end());
    if (!bytes.empty() && framework::Crc32::compute(bytes) == crc) {
        if (crc == 0)
            return {};
        return {1};
    }
    return bytes;
}

bool MuteListEndpoint::store(framework::Uuid agent, framework::Uuid mute, const std::string& name,
                            std::int32_t type, std::int32_t flags, std::optional<std::int32_t> stamp) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    return store_.write(agent, mute, name, type, flags, stamp ? *stamp : now.count());
}

bool MuteListEndpoint::remove(framework::Uuid agent, framework::Uuid mute, const std::string& name) {
    return store_.erase(agent, mute, name);
}

network::HttpResponse MuteListEndpoint::handle(const network::HttpRequest& request) {
    network::HttpResponse response;
    response.content_type = "application/xml";
    if (request.method != "POST") {
        response.status = 405;
        response.headers["Allow"] = "POST";
        return response;
    }
    if (!auth_.authenticate(request, response))
        return response;
    std::string result;
    try {
        result = process(request.body);
    } catch (const std::exception&) {
        result = "Failure";
    }
    response.body = network::service_xml(network::ServiceXmlValue::object({{"result", std::move(result)}}));
    return response;
}

std::string MuteListEndpoint::process(std::string_view body) {
    const auto form = network::parse_service_form(framework::trim_utf8(network::read_service_body(body)));
    const auto field = [&](const char* key) -> const std::string* {
        const auto entry = form.find(key);
        if (entry == form.end())
            return nullptr;
        return &std::get<std::string>(entry->second);
    };

    const auto* method = field("METHOD");
    const auto* agent_id = field("agentid");
    framework::Uuid agent;
    if (!method || !agent_id || !framework::try_parse_sdk_uuid(*agent_id, agent))
        return "Failure";

    if (*method == "get") {
        const auto* crc_text = field("mutecrc");
        std::uint32_t crc{};
        if (!crc_text || !framework::try_parse_unsigned_integer(*crc_text, crc))
            return "Failure";
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(get(agent, crc));
        return network::binary_base64(network::osd_from_binary(std::move(bytes)));
    }

    const auto* mute_id = field("muteid");
    framework::Uuid mute;
    if (!mute_id || !framework::try_parse_sdk_uuid(*mute_id, mute))
        return "Failure";
    const auto* name_field = field("mutename");
    const std::string name = name_field ? *name_field : "";
    if (*method == "delete")
        return remove(agent, mute, name) ? "Success" : "Failure";
    if (*method != "update")
        return "Failure";

    std::int32_t type{}, flags{};
    std::optional<std::int32_t> stamp;
    if (const auto* value = field("mutetype"); value && !parse_signed_integer(*value, type))
        return "Failure";
    if (const auto* value = field("muteflags"); value && !parse_signed_integer(*value, flags))
        return "Failure";
    if (const auto* value = field("mutestamp"); value && !parse_signed_integer(*value, stamp.emplace()))
        return "Failure";
    return store(agent, mute, name, type, flags, stamp) ? "Success" : "Failure";
}
}
