// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/mute_list_store.hpp"

#include "mysql_connection.hpp"

#include <mutex>
#include <stdexcept>

namespace opensim::services {
namespace {
std::string text_literal(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string literal = "CONVERT(X'";
    for (const unsigned char byte : value) {
        literal.push_back(hex[byte >> 4]);
        literal.push_back(hex[byte & 15]);
    }
    return literal + "' USING utf8mb4)";
}

struct MysqlMuteList {
    using Result = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;

    explicit MysqlMuteList(const std::string& settings) : connection(connect_mysql(settings)) {
        constexpr auto lock_name = "SHA2(CONCAT(DATABASE(), ':MuteListStore'), 256)";
        const auto lock = query(std::string("SELECT GET_LOCK(") + lock_name + ", 30)");
        const auto row = mysql_fetch_row(lock.get());
        if (!row || !row[0] || std::string_view(row[0]) != "1")
            throw std::runtime_error("Cannot acquire MySQL mute-list migration lock");

        // MySQL commits DDL independently. Idempotent DDL allows recovery after interruption.
        query("CREATE TABLE IF NOT EXISTS migrations (name varchar(100), version int)");
        query("INSERT INTO migrations (name,version) SELECT 'migrations',1"
              " WHERE NOT EXISTS (SELECT 1 FROM migrations WHERE name='migrations' AND version >= 1)");
        const auto version = query("SELECT version FROM migrations WHERE name='MuteListStore' AND version >= 1");
        if (mysql_num_rows(version.get()) == 0) {
            query("CREATE TABLE IF NOT EXISTS MuteList ("
                  "AgentID char(36) NOT NULL,"
                  "MuteID char(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',"
                  "MuteName varchar(64) NOT NULL DEFAULT '',"
                  "MuteType int NOT NULL DEFAULT 1, MuteFlags int NOT NULL DEFAULT 0, Stamp int NOT NULL,"
                  "UNIQUE KEY AgentID_2 (AgentID,MuteID,MuteName), KEY AgentID (AgentID))"
                  " ENGINE=InnoDB DEFAULT CHARSET=latin1");
            query("INSERT INTO migrations (name,version) VALUES ('MuteListStore',1)");
        }
        query(std::string("SELECT RELEASE_LOCK(") + lock_name + ")");
    }

    Result query(const std::string& sql) {
        auto* db = connection.get();
        if (mysql_real_query(db, sql.data(), static_cast<unsigned long>(sql.size())) != 0)
            throw std::runtime_error("MySQL mute-list query failed (client error " +
                                     std::to_string(mysql_errno(db)) + ")");
        Result rows(mysql_store_result(db), mysql_free_result);
        if (!rows && mysql_field_count(db) != 0)
            throw std::runtime_error("MySQL mute-list result failed");
        return rows;
    }

    bool execute(const std::string& sql) {
        std::lock_guard lock(mutex);
        query(sql);
        return mysql_affected_rows(connection.get()) > 0;
    }

    std::string read(framework::Uuid agent) {
        std::lock_guard lock(mutex);
        const auto rows = query("SELECT MuteType,MuteID,MuteName,MuteFlags FROM MuteList WHERE AgentID=" +
                                text_literal(agent.str()));
        std::string data;
        while (const auto row = mysql_fetch_row(rows.get())) {
            const auto lengths = mysql_fetch_lengths(rows.get());
            data.append(row[0], lengths[0]).push_back(' ');
            data.append(row[1], lengths[1]).push_back(' ');
            data.append(row[2], lengths[2]).push_back('|');
            data.append(row[3], lengths[3]).push_back('\n');
        }
        return data;
    }

    MysqlConnection connection;
    std::mutex mutex;
};
}

MuteListStore mysql_mute_list_store(const std::string& connection) {
    const auto database = std::make_shared<MysqlMuteList>(connection);
    return {
        [database](framework::Uuid agent) { return database->read(agent); },
        [database](framework::Uuid agent, framework::Uuid mute, const std::string& name,
                   std::int32_t type, std::int32_t flags, std::int64_t stamp) {
            return database->execute(
                "REPLACE INTO MuteList (AgentID,MuteID,MuteName,MuteType,MuteFlags,Stamp) VALUES (" +
                text_literal(agent.str()) + "," + text_literal(mute.str()) + "," + text_literal(name) +
                "," + std::to_string(type) + "," + std::to_string(flags) + "," + std::to_string(stamp) + ")");
        },
        [database](framework::Uuid agent, framework::Uuid mute, const std::string& name) {
            return database->execute("DELETE FROM MuteList WHERE AgentID=" + text_literal(agent.str()) +
                                     " AND MuteID=" + text_literal(mute.str()) +
                                     " AND MuteName=" + text_literal(name));
        }
    };
}
}
