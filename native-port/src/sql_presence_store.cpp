// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/presence_service.hpp"

#include "sql_service_connection.hpp"

#include <mutex>
#include <stdexcept>

namespace opensim::services {
namespace {
using framework::Uuid;
using sql::Execute;
using sql::Parameters;
using sql::Result;

class SqlPresenceStore final : public PresenceStore {
public:
    SqlPresenceStore(Execute execute, const std::string& realm, bool postgres)
        : execute_(std::move(execute)), postgres_(postgres) {
        if (realm.empty() ||
            realm.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                std::string::npos)
            throw std::invalid_argument("Invalid SQL presence realm");
        auto table_name = realm;
        if (postgres_) {
            // The original PostgreSQL provider uses unquoted table names.
            for (auto& character : table_name)
                if (character >= 'A' && character <= 'Z')
                    character = static_cast<char>(character - 'A' + 'a');
        }
        table_ = quote(table_name);
    }

    bool persistent() const override { return true; }

    bool store(const std::shared_ptr<PresenceData>& data) override {
        if (!data)
            return false;
        const auto secure = data->data.find("SecureSessionID");
        std::string statement = postgres_ ? "INSERT INTO " : "REPLACE INTO ";
        statement += table_ + " (" + quote("SessionID") + "," + quote("UserID") + "," +
                     quote("RegionID") + "," + quote("SecureSessionID") + ") VALUES (?,?,?,?)";
        if (postgres_)
            statement += " ON CONFLICT (\"SessionID\") DO UPDATE SET "
                         "\"UserID\"=EXCLUDED.\"UserID\",\"RegionID\"=EXCLUDED.\"RegionID\","
                         "\"SecureSessionID\"=EXCLUDED.\"SecureSessionID\"";
        return run(statement, {data->session_id.str(), data->user_id, data->region_id.str(),
                               secure == data->data.end() ? Uuid{}.str() : secure->second}).affected;
    }

    std::shared_ptr<PresenceData> get(const Uuid& session) override {
        auto rows = get("SessionID", session.str());
        return rows.empty() ? nullptr : std::move(rows.front());
    }

    std::vector<std::shared_ptr<PresenceData>> get(const std::string& field,
                                                   const std::string& value) override {
        if (!valid_field(field))
            return {};
        const auto result = run("SELECT " + quote("SessionID") + "," + quote("UserID") + "," +
                                quote("RegionID") + "," + quote("SecureSessionID") + " FROM " +
                                table_ + " WHERE " + quote(field) + "=?", {value});
        std::vector<std::shared_ptr<PresenceData>> rows;
        for (const auto& columns : result.rows) {
            auto row = std::make_shared<PresenceData>();
            row->session_id = Uuid(columns[0]);
            row->user_id = columns[1];
            row->region_id = Uuid(columns[2]);
            row->data["SecureSessionID"] = columns[3];
            rows.push_back(std::move(row));
        }
        return rows;
    }

    bool erase(const std::string& field, const std::string& value) override {
        return valid_field(field) &&
               run("DELETE FROM " + table_ + " WHERE " + quote(field) + "=?", {value}).affected;
    }

    bool erase_session_in_region(const Uuid& session, const Uuid& region) override {
        return run("DELETE FROM " + table_ + " WHERE " + quote("SessionID") + "=? AND " +
                   quote("RegionID") + "=?", {session.str(), region.str()}).affected;
    }

    void logout_region(const Uuid& region) override {
        erase("RegionID", region.str());
    }

    bool report(const Uuid& session, const Uuid& region) override {
        if (region.is_zero())
            return false;
        return run("UPDATE " + table_ + " SET " + quote("RegionID") + "=?," +
                   quote("LastSeen") + "=NOW() WHERE " + quote("SessionID") + "=?",
                   {region.str(), session.str()}).affected;
    }

    bool verify(const Uuid& agent, const Uuid& secure_session) override {
        const auto rows = get("SecureSessionID", secure_session.str());
        return !rows.empty() && rows.front()->user_id == agent.str();
    }

private:
    static bool valid_field(std::string_view field) {
        return field == "SessionID" || field == "UserID" || field == "RegionID" ||
               field == "SecureSessionID";
    }

    std::string quote(std::string_view identifier) const {
        const char delimiter = postgres_ ? '"' : '`';
        return delimiter + std::string(identifier) + delimiter;
    }

    Result run(const std::string& sql, Parameters values) {
        std::lock_guard lock(mutex_);
        return execute_(sql, values);
    }

    Execute execute_;
    bool postgres_;
    std::string table_;
    std::mutex mutex_;
};


}

#ifdef OPENSIM_HAVE_MYSQL
std::unique_ptr<PresenceStore> mysql_presence_store(const std::string& connection, const std::string& realm) {
    const auto execute = sql::mysql(connection);
    auto store = std::make_unique<SqlPresenceStore>(execute, realm, false);
    constexpr auto lock_name = "SHA2(CONCAT(DATABASE(), ':Presence'), 256)";
    const auto lock = execute(std::string("SELECT GET_LOCK(") + lock_name + ", 30)", {});
    if (lock.rows.at(0).at(0) != "1")
        throw std::runtime_error("Cannot acquire MySQL presence migration lock");
    if (const auto version = sql::initialize_migrations(execute, "Presence"); version < 4) {
        execute("CREATE TABLE IF NOT EXISTS Presence ("
                "UserID varchar(255) NOT NULL,"
                "RegionID char(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',"
                "SessionID char(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',"
                "SecureSessionID char(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',"
                "LastSeen timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
                "UNIQUE KEY SessionID (SessionID), KEY UserID (UserID), KEY RegionID (RegionID))"
                " ENGINE=InnoDB DEFAULT CHARSET=latin1", {});
        sql::record_migration(execute, "Presence", version, 4);
    }
    execute(std::string("SELECT RELEASE_LOCK(") + lock_name + ")", {});
    return store;
}
#endif

#ifdef OPENSIM_HAVE_POSTGRESQL
std::unique_ptr<PresenceStore> postgres_presence_store(const std::string& connection, const std::string& realm) {
    const auto execute = sql::postgres(connection);
    auto store = std::make_unique<SqlPresenceStore>(execute, realm, true);
    execute("BEGIN", {});
    execute("SELECT pg_advisory_xact_lock(hashtext(current_schema()), hashtext('Presence'))", {});
    const auto version = sql::initialize_migrations(execute, "Presence");
    if (version < 1)
        execute("CREATE TABLE IF NOT EXISTS Presence ("
                "\"UserID\" varchar(255) NOT NULL, \"RegionID\" uuid NOT NULL,"
                "\"SessionID\" uuid NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',"
                "\"SecureSessionID\" uuid NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000')", {});
    if (version < 2) {
        execute("CREATE UNIQUE INDEX IF NOT EXISTS SessionID ON Presence (\"SessionID\")", {});
        execute("CREATE INDEX IF NOT EXISTS UserID ON Presence (\"UserID\")", {});
    }
    if (version < 3)
        execute("CREATE INDEX IF NOT EXISTS RegionID ON Presence (\"RegionID\")", {});
    if (version < 4) {
        // The source version 4 resets LastSeen because some version 2 databases lacked it.
        execute("ALTER TABLE Presence DROP COLUMN IF EXISTS \"LastSeen\", ADD COLUMN \"LastSeen\" timestamp", {});
        sql::record_migration(execute, "Presence", version, 4);
    }
    execute("COMMIT", {});
    return store;
}
#endif
}
