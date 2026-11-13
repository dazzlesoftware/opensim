// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/presence_service.hpp"
#include "sql_service_connection.hpp"

#include <mutex>
#include <stdexcept>

namespace opensim::services {
namespace {
using framework::Uuid;

class SqlTravelStore final : public TravelStore {
public:
    SqlTravelStore(sql::Execute execute, std::string realm, bool postgres)
        : execute_(std::move(execute)), postgres_(postgres) {
        if (realm.empty() || realm.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != realm.npos)
            throw std::invalid_argument("Invalid SQL travel realm");
        if (postgres_)
            for (auto& character : realm)
                if (character >= 'A' && character <= 'Z')
                    character += 'a' - 'A';
        table_ = quote(realm);
    }

    void initialize() {
        const auto create = [&](const std::string& table) {
            const auto timestamp = postgres_ ? "\"TMStamp\" timestamp NOT NULL DEFAULT now()"
                                            : "`TMStamp` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP";
            execute_("CREATE TABLE IF NOT EXISTS " + table + " (" +
                     quote("SessionID") + " varchar(36) NOT NULL PRIMARY KEY," +
                     quote("UserID") + " varchar(36) NOT NULL," +
                     quote("GridExternalName") + " varchar(255) NOT NULL DEFAULT ''," +
                     quote("ServiceToken") + " varchar(255) NOT NULL DEFAULT ''," +
                     quote("ClientIPAddress") + " varchar(16) NOT NULL DEFAULT ''," +
                     quote("MyIPAddress") + " varchar(16) NOT NULL DEFAULT ''," + timestamp +
                     (postgres_ ? ")" : ",KEY (`UserID`)) ENGINE=InnoDB DEFAULT CHARSET=latin1"), {});
        };
        if (postgres_) {
            execute_("BEGIN", {});
            execute_("SELECT pg_advisory_xact_lock(hashtext(current_schema()), hashtext('HGTravelStore'))", {});
        } else {
            const auto lock = execute_("SELECT GET_LOCK(SHA2(CONCAT(DATABASE(), ':HGTravelStore'),256),30)", {});
            if (lock.rows.at(0).at(0) != "1")
                throw std::runtime_error("Cannot acquire MySQL travel migration lock");
        }
        const auto version = sql::initialize_migrations(execute_, "HGTravelStore");
        const auto default_table = quote("hg_traveling_data");
        const int target = postgres_ ? 1 : 2;
        if (version < target) {
            create(default_table);
            if (!postgres_ && version == 1)
                execute_("ALTER TABLE " + default_table +
                         " MODIFY `TMStamp` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP", {});
            sql::record_migration(execute_, "HGTravelStore", version, target);
        }
        if (table_ != default_table)
            create(table_);
        execute_("DELETE FROM " + table_ + " WHERE " + quote("TMStamp") +
                 (postgres_ ? " < CURRENT_DATE - INTERVAL '2 day'" : " < NOW() - INTERVAL 2 DAY"), {});
        if (postgres_)
            execute_("COMMIT", {});
        else
            execute_("SELECT RELEASE_LOCK(SHA2(CONCAT(DATABASE(), ':HGTravelStore'),256))", {});
    }

    std::optional<TravelRecord> get(const Uuid& session) override {
        auto rows = find("SessionID", session);
        if (rows.empty())
            return std::nullopt;
        return read(rows.front());
    }

    std::optional<TravelRecord> replaced_by(const Uuid& session, const Uuid&) override {
        return get(session);
    }

    std::optional<TravelRecord> locate(const Uuid& user, std::string_view home) override {
        for (const auto& row : find("UserID", user))
            if (!row[2].empty() && row[2] != home)
                return read(row);
        return std::nullopt;
    }

    void store(const TravelRecord& record) override {
        auto statement = std::string(postgres_ ? "INSERT INTO " : "REPLACE INTO ") +
                         table_ + " (" + columns() + ") VALUES (?,?,?,?,?)";
        if (postgres_)
            statement += " ON CONFLICT (\"SessionID\") DO UPDATE SET "
                         "\"UserID\"=EXCLUDED.\"UserID\",\"GridExternalName\"=EXCLUDED.\"GridExternalName\","
                         "\"ServiceToken\"=EXCLUDED.\"ServiceToken\",\"ClientIPAddress\"=EXCLUDED.\"ClientIPAddress\"";
        std::lock_guard lock(mutex_);
        execute_(statement, {record.session_id.str(), record.user_id.str(), record.grid,
                             record.service_token, record.client_ip});
    }

    void erase(const Uuid& session) override {
        std::lock_guard lock(mutex_);
        execute_("DELETE FROM " + table_ + " WHERE " + quote("SessionID") + "=?", {session.str()});
    }

private:
    std::string quote(std::string_view identifier) const {
        const char delimiter = postgres_ ? '"' : '`';
        return delimiter + std::string(identifier) + delimiter;
    }

    std::string columns() const {
        return quote("SessionID") + "," + quote("UserID") + "," + quote("GridExternalName") + "," +
               quote("ServiceToken") + "," + quote("ClientIPAddress");
    }

    std::vector<std::vector<std::string>> find(std::string_view column, const Uuid& value) {
        std::lock_guard lock(mutex_);
        return execute_("SELECT " + columns() + "," + quote("MyIPAddress") + " FROM " +
                        table_ + " WHERE " + quote(column) + "=?",
                        {value.str()}).rows;
    }

    static TravelRecord read(const std::vector<std::string>& row) {
        return {Uuid(row[0]), Uuid(row[1]), row[2], row[3], row[4], row[5]};
    }

    sql::Execute execute_;
    bool postgres_;
    std::string table_;
    std::mutex mutex_;
};
}

#ifdef OPENSIM_HAVE_MYSQL
std::shared_ptr<TravelStore> mysql_travel_store(const std::string& connection, const std::string& realm) {
    auto store = std::make_shared<SqlTravelStore>(sql::mysql(connection), realm, false);
    store->initialize();
    return store;
}
#endif

#ifdef OPENSIM_HAVE_POSTGRESQL
std::shared_ptr<TravelStore> postgres_travel_store(const std::string& connection, const std::string& realm) {
    auto store = std::make_shared<SqlTravelStore>(sql::postgres(connection), realm, true);
    store->initialize();
    return store;
}
#endif
}
