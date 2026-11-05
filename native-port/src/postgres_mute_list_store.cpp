// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/mute_list_store.hpp"

#include "postgres_connection.hpp"

#include <mutex>
#include <stdexcept>

namespace opensim::services {
namespace {
struct PostgresMuteList {
    using Result = std::unique_ptr<PGresult, decltype(&PQclear)>;

    explicit PostgresMuteList(const std::string& settings) : connection(connect_postgres(settings)) {
        query("BEGIN");
        query("SELECT pg_advisory_xact_lock(hashtext(current_schema()), hashtext('MuteListStore'))");
        query("CREATE TABLE IF NOT EXISTS migrations (name varchar(100), version int)");
        query("INSERT INTO migrations (name,version) SELECT 'migrations',1"
              " WHERE NOT EXISTS (SELECT 1 FROM migrations WHERE name='migrations' AND version >= 1)");
        const auto version = query("SELECT version FROM migrations WHERE name='MuteListStore' AND version >= 1");
        if (PQntuples(version.get()) == 0) {
            query("CREATE TABLE IF NOT EXISTS MuteList ("
                  " \"AgentID\" varchar(36) NOT NULL,"
                  " \"MuteId\" varchar(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',"
                  " \"MuteName\" varchar(64) NOT NULL DEFAULT '',"
                  " \"MuteType\" int4 NOT NULL DEFAULT 1, \"MuteFlags\" int4 NOT NULL DEFAULT 0,"
                  " \"Stamp\" int4 NOT NULL, CONSTRAINT pk_agent_id PRIMARY KEY (\"AgentID\"),"
                  " CONSTRAINT unique_agent_2 UNIQUE (\"AgentID\", \"MuteId\", \"MuteName\"))");
            query("INSERT INTO migrations (name,version) VALUES ('MuteListStore',1)");
        }
        // The original migration used MuteId while the .NET model uses MuteID.
        const auto columns = query(
            "SELECT attname FROM pg_attribute WHERE attrelid=to_regclass('mutelist')"
            " AND NOT attisdropped AND attname IN ('MuteID','MuteId') ORDER BY attname COLLATE \"C\"");
        if (PQntuples(columns.get()) == 0)
            throw std::runtime_error("PostgreSQL mute-list schema has no mute ID column");
        mute_id_column = std::string_view(PQgetvalue(columns.get(), 0, 0)) == "MuteID"
            ? "\"MuteID\"" : "\"MuteId\"";
        query("COMMIT");
    }

    Result query(const std::string& sql, std::initializer_list<const char*> parameters = {}) {
        Result result(PQexecParams(connection.get(), sql.c_str(), static_cast<int>(parameters.size()),
                                   nullptr, parameters.begin(), nullptr, nullptr, 0), PQclear);
        const auto status = PQresultStatus(result.get());
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
            throw std::runtime_error("PostgreSQL mute-list query failed");
        return result;
    }

    std::string read(framework::Uuid agent) {
        std::lock_guard lock(mutex);
        const auto rows = query(
            "SELECT \"MuteType\"," + mute_id_column + ",\"MuteName\",\"MuteFlags\""
            " FROM MuteList WHERE \"AgentID\"=$1", {agent.str().c_str()});
        std::string data;
        for (int row = 0; row < PQntuples(rows.get()); ++row) {
            data.append(PQgetvalue(rows.get(), row, 0)).push_back(' ');
            data.append(PQgetvalue(rows.get(), row, 1)).push_back(' ');
            data.append(PQgetvalue(rows.get(), row, 2)).push_back('|');
            data.append(PQgetvalue(rows.get(), row, 3)).push_back('\n');
        }
        return data;
    }

    PostgresConnection connection;
    std::string mute_id_column;
    std::mutex mutex;
};
}

MuteListStore postgres_mute_list_store(const std::string& connection) {
    const auto database = std::make_shared<PostgresMuteList>(connection);
    return {
        [database](framework::Uuid agent) { return database->read(agent); },
        [database](framework::Uuid agent, framework::Uuid mute, const std::string& name,
                   std::int32_t type, std::int32_t flags, std::int64_t stamp) {
            if (name.find('\0') != std::string::npos)
                return false;
            std::lock_guard lock(database->mutex);
            const auto& mute_id = database->mute_id_column;
            database->query(
                "INSERT INTO MuteList (\"AgentID\"," + mute_id + ",\"MuteName\",\"MuteType\",\"MuteFlags\",\"Stamp\")"
                " VALUES ($1,$2,$3,$4,$5,$6) ON CONFLICT (\"AgentID\"," + mute_id + ",\"MuteName\")"
                " DO UPDATE SET \"MuteType\"=EXCLUDED.\"MuteType\",\"MuteFlags\"=EXCLUDED.\"MuteFlags\","
                " \"Stamp\"=EXCLUDED.\"Stamp\"",
                {agent.str().c_str(), mute.str().c_str(), name.c_str(), std::to_string(type).c_str(),
                 std::to_string(flags).c_str(), std::to_string(stamp).c_str()});
            return true;
        },
        [database](framework::Uuid agent, framework::Uuid mute, const std::string& name) {
            if (name.find('\0') != std::string::npos)
                return false;
            std::lock_guard lock(database->mutex);
            database->query("DELETE FROM MuteList WHERE \"AgentID\"=$1 AND " +
                            database->mute_id_column + "=$2 AND \"MuteName\"=$3",
                            {agent.str().c_str(), mute.str().c_str(), name.c_str()});
            return true;
        }
    };
}
}
