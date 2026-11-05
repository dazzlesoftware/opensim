// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/pgsql_offline_im_store.hpp"
#include "opensim/services/offline_im_record.hpp"
#include "postgres_connection.hpp"
#include <mutex>

namespace opensim::services {
struct PgsqlOfflineImStore::State {
    std::unique_ptr<PGconn, decltype(&PQfinish)> connection{nullptr, PQfinish};
    std::string table;
    std::mutex mutex;

    auto run(const std::string& sql, std::initializer_list<const char*> parameters = {})
    {
        std::unique_ptr<PGresult, decltype(&PQclear)> result(
            PQexecParams(connection.get(), sql.c_str(), static_cast<int>(parameters.size()),
                         nullptr, parameters.begin(), nullptr, nullptr, 0), PQclear);
        const auto status = PQresultStatus(result.get());
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
            throw std::runtime_error("PostgreSQL OfflineIM query failed");
        return result;
    }
};

PgsqlOfflineImStore::PgsqlOfflineImStore(const std::string& connection, const std::string& realm)
    : state_(std::make_unique<State>())
{
    if (realm.empty() || realm.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        throw std::invalid_argument("Invalid OfflineIM realm");
    auto& s = *state_;
    s.table = '"' + realm + '"';
    s.connection = connect_postgres(connection);
    s.run("BEGIN");
    try {
        s.run("SELECT pg_advisory_xact_lock(hashtext(current_schema()), hashtext($1))", {realm.c_str()});
        s.run("CREATE TABLE IF NOT EXISTS " + s.table +
              " (\"ID\" serial, \"PrincipalID\" uuid NOT NULL, \"FromID\" uuid NOT NULL,"
              " \"Message\" text NOT NULL, \"TMStamp\" timestamp(6) NOT NULL DEFAULT clock_timestamp(),"
              " PRIMARY KEY (\"ID\", \"PrincipalID\", \"FromID\"))");
        // IM_Store versions 1 and 2 have no sender column. The sender in
        // their serialized messages remains available when draining the queue.
        if (PQntuples(s.run("SELECT 1 FROM pg_attribute WHERE attrelid=to_regclass($1)"
                           " AND attname='FromID' AND NOT attisdropped", {s.table.c_str()}).get()) == 0)
            s.run("ALTER TABLE " + s.table + " ADD COLUMN \"FromID\" uuid NOT NULL"
                  " DEFAULT '00000000-0000-0000-0000-000000000000'");
        s.run("DELETE FROM " + s.table + " WHERE \"TMStamp\" < CURRENT_DATE - INTERVAL '2 weeks'");
        s.run("COMMIT");
    } catch (...) {
        try { s.run("ROLLBACK"); } catch (...) {}
        throw;
    }
}

PgsqlOfflineImStore::~PgsqlOfflineImStore() = default;

bool PgsqlOfflineImStore::store(framework::Uuid recipient, const network::ServiceXmlValue& message, int limit)
{
    auto& s = *state_;
    const auto id = recipient.str();
    const auto maximum = std::to_string(limit);
    const auto xml = write_legacy_offline_im(message);
    auto sender = framework::Uuid{}.str();
    for (const auto& [key, value] : message.fields)
        if (key == "FromAgentID")
            sender = value.text;
    std::lock_guard lock(s.mutex);
    s.run("BEGIN");
    try {
        s.run("LOCK TABLE " + s.table + " IN SHARE ROW EXCLUSIVE MODE");
        auto result = s.run("INSERT INTO " + s.table + " (\"PrincipalID\", \"FromID\", \"Message\") "
            "SELECT $1, $2::uuid, $3 WHERE (SELECT count(*) FROM " + s.table +
            " WHERE \"PrincipalID\"=$1) < $4::integer RETURNING \"ID\"",
            {id.c_str(), sender.c_str(), xml.c_str(), maximum.c_str()});
        s.run("COMMIT");
        return PQntuples(result.get()) != 0;
    } catch (...) {
        try { s.run("ROLLBACK"); } catch (...) {}
        throw;
    }
}

std::vector<network::ServiceXmlValue> PgsqlOfflineImStore::drain(framework::Uuid recipient)
{
    auto& s = *state_;
    const auto id = recipient.str();
    std::lock_guard lock(s.mutex);
    s.run("BEGIN");
    try {
        auto rows = s.run("WITH drained AS (DELETE FROM " + s.table +
            " WHERE \"PrincipalID\"=$1 RETURNING \"ID\", \"Message\") "
            "SELECT \"Message\" FROM drained ORDER BY \"ID\"", {id.c_str()});
        std::vector<network::ServiceXmlValue> messages;
        for (int i = 0; i < PQntuples(rows.get()); ++i)
            messages.push_back(read_legacy_offline_im(PQgetvalue(rows.get(), i, 0)));
        s.run("COMMIT");
        return messages;
    } catch (...) {
        try { s.run("ROLLBACK"); } catch (...) {}
        throw;
    }
}

void PgsqlOfflineImStore::erase(framework::Uuid recipient)
{
    auto& s = *state_;
    const auto id = recipient.str();
    std::lock_guard lock(s.mutex);
    s.run("DELETE FROM " + s.table + " WHERE \"PrincipalID\"=$1", {id.c_str()});
}
}
