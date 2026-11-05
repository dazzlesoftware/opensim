// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_offline_im_store.hpp"
#include "opensim/services/offline_im_record.hpp"
#include <sqlite3.h>

namespace opensim::services {
namespace {
void check(sqlite3* db, int status)
{
    if (status != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}

void execute(sqlite3* db, const std::string& sql)
{
    check(db, sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr));
}

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql, framework::Uuid recipient) : db_(db)
    {
        sqlite3_stmt* stmt = nullptr;
        const auto status = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
        value.reset(stmt);
        check(db, status);
        bind(1, recipient.str());
    }

    void bind(int index, const std::string& text)
    {
        check(db_, sqlite3_bind_text64(value.get(), index, text.data(), text.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
    }

    bool row()
    {
        const auto status = sqlite3_step(value.get());
        if (status == SQLITE_ROW)
            return true;
        if (status == SQLITE_DONE)
            return false;
        check(db_, status);
        return false;
    }

    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> value{nullptr, sqlite3_finalize};

private:
    sqlite3* db_;
};
}

SqliteOfflineImStore::SqliteOfflineImStore(const std::string& connection, const std::string& realm)
{
    if (realm.empty() || realm.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        throw std::invalid_argument("Invalid OfflineIM realm");
    table_ = '"' + realm + '"';
    database_.open(connection);
    auto* db = database_.handle();
    execute(db, "BEGIN IMMEDIATE");
    try {
        execute(db, "CREATE TABLE IF NOT EXISTS " + table_ + " "
                "(PrincipalID TEXT NOT NULL, Message TEXT NOT NULL, StoredAt INTEGER NOT NULL DEFAULT 0)");
        if (sqlite3_table_column_metadata(db, nullptr, realm.c_str(), "StoredAt",
                nullptr, nullptr, nullptr, nullptr, nullptr) != SQLITE_OK)
            execute(db, "ALTER TABLE " + table_ + " ADD COLUMN StoredAt INTEGER NOT NULL DEFAULT 0");
        execute(db, "CREATE INDEX IF NOT EXISTS \"" + realm + "_recipient\" ON " + table_ + "(PrincipalID)");
        execute(db, "DELETE FROM " + table_ + " WHERE StoredAt > 0 "
                "AND StoredAt < CAST(strftime('%s','now','-14 days') AS INTEGER)");
        execute(db, "COMMIT");
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

bool SqliteOfflineImStore::store(framework::Uuid recipient, const network::ServiceXmlValue& message, int limit)
{
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    Statement insert(db, "INSERT INTO " + table_ + " (PrincipalID, Message, StoredAt) "
                         "SELECT ?1, ?2, CAST(strftime('%s','now') AS INTEGER) "
                         "WHERE (SELECT count(*) FROM " + table_ + " WHERE PrincipalID=?1) < ?3",
                     recipient);
    insert.bind(2, write_legacy_offline_im(message));
    check(db, sqlite3_bind_int(insert.value.get(), 3, limit));
    insert.row();
    return sqlite3_changes(db) != 0;
}

std::vector<network::ServiceXmlValue> SqliteOfflineImStore::drain(framework::Uuid recipient)
{
    std::lock_guard lock(database_.mutex());
    auto* db = database_.handle();
    execute(db, "BEGIN IMMEDIATE");
    try {
        std::vector<network::ServiceXmlValue> messages;
        Statement select(db, "SELECT Message FROM " + table_ + " WHERE PrincipalID=?1 ORDER BY rowid", recipient);
        while (select.row()) {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(select.value.get(), 0));
            const auto length = sqlite3_column_bytes(select.value.get(), 0);
            if (!text || length == 0)
                throw std::runtime_error("Empty stored offline message");
            const std::string_view xml(text, static_cast<std::size_t>(length));
            auto message = network::parse_service_xml(xml);
            if (message.fields.empty())
                message = read_legacy_offline_im(xml);
            messages.push_back(std::move(message));
        }
        erase(recipient);
        execute(db, "COMMIT");
        return messages;
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

void SqliteOfflineImStore::erase(framework::Uuid recipient)
{
    std::lock_guard lock(database_.mutex());
    Statement remove(database_.handle(), "DELETE FROM " + table_ + " WHERE PrincipalID=?1", recipient);
    remove.row();
}
}
