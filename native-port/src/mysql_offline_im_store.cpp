// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/mysql_offline_im_store.hpp"
#include "opensim/services/offline_im_record.hpp"
#include "mysql_connection.hpp"
#include <mysqld_error.h>
#include <mutex>

namespace opensim::services {
namespace {
struct MysqlQueryError : std::runtime_error {
    explicit MysqlQueryError(unsigned code)
        : std::runtime_error("MySQL OfflineIM query failed"), code(code) {}
    unsigned code;
};

std::string literal(std::string_view value)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result = "X'";
    for (const unsigned char byte : value) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result + "'";
}
}

struct MysqlOfflineImStore::State {
    std::unique_ptr<MYSQL, decltype(&mysql_close)> connection{nullptr, mysql_close};
    std::string table;
    std::mutex mutex;

    auto run(const std::string& sql)
    {
        auto* db = connection.get();
        if (mysql_real_query(db, sql.data(), static_cast<unsigned long>(sql.size())) != 0)
            throw MysqlQueryError(mysql_errno(db));
        std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> rows(mysql_store_result(db), mysql_free_result);
        if (!rows && mysql_field_count(db) != 0)
            throw std::runtime_error("MySQL OfflineIM result failed");
        return rows;
    }

    template<class Operation>
    auto transaction(Operation operation)
    {
        std::lock_guard lock(mutex);
        for (int attempt = 0; ; ++attempt) {
            run("SET TRANSACTION ISOLATION LEVEL SERIALIZABLE");
            run("START TRANSACTION");
            try {
                auto result = operation();
                run("COMMIT");
                return result;
            } catch (const MysqlQueryError& error) {
                run("ROLLBACK");
                if (error.code != ER_LOCK_DEADLOCK || attempt == 3)
                    throw;
            } catch (...) {
                try { run("ROLLBACK"); } catch (...) {}
                throw;
            }
        }
    }
};

MysqlOfflineImStore::MysqlOfflineImStore(const std::string& connection, const std::string& realm)
    : state_(std::make_unique<State>())
{
    if (realm.empty() || realm.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos)
        throw std::invalid_argument("Invalid OfflineIM realm");
    auto& s = *state_;
    s.table = "`" + realm + "`";
    s.connection = connect_mysql(connection);
    s.run("CREATE TABLE IF NOT EXISTS " + s.table +
        " (ID mediumint NOT NULL AUTO_INCREMENT PRIMARY KEY, PrincipalID char(36) NOT NULL,"
        " FromID char(36) NOT NULL, Message text NOT NULL, TMStamp timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP"
        " ON UPDATE CURRENT_TIMESTAMP, KEY (PrincipalID), KEY (FromID)) ENGINE=InnoDB DEFAULT CHARSET=utf8");
    s.run("DELETE FROM " + s.table + " WHERE TMStamp < NOW() - INTERVAL 2 WEEK");
}

MysqlOfflineImStore::~MysqlOfflineImStore() = default;

bool MysqlOfflineImStore::store(framework::Uuid recipient, const network::ServiceXmlValue& message, int limit)
{
    auto& s = *state_;
    const auto id = literal(recipient.str());
    auto sender = framework::Uuid{}.str();
    for (const auto& [key, value] : message.fields)
        if (key == "FromAgentID")
            sender = value.text;
    const auto xml = literal(write_legacy_offline_im(message));
    return s.transaction([&] {
        const auto existing = s.run("SELECT ID FROM " + s.table + " WHERE PrincipalID=" + id + " FOR UPDATE");
        if (limit <= 0 || mysql_num_rows(existing.get()) >= static_cast<unsigned>(limit))
            return false;
        s.run("INSERT INTO " + s.table + " (PrincipalID, FromID, Message) VALUES (" +
              id + ", " + literal(sender) + ", " + xml + ")");
        return true;
    });
}

std::vector<network::ServiceXmlValue> MysqlOfflineImStore::drain(framework::Uuid recipient)
{
    auto& s = *state_;
    const auto predicate = " WHERE PrincipalID=" + literal(recipient.str());
    return s.transaction([&] {
        const auto rows = s.run("SELECT Message FROM " + s.table + predicate + " ORDER BY ID FOR UPDATE");
        std::vector<network::ServiceXmlValue> messages;
        while (auto row = mysql_fetch_row(rows.get()))
            messages.push_back(read_legacy_offline_im(std::string_view(row[0], mysql_fetch_lengths(rows.get())[0])));
        s.run("DELETE FROM " + s.table + predicate);
        return messages;
    });
}

void MysqlOfflineImStore::erase(framework::Uuid recipient)
{
    auto& s = *state_;
    std::lock_guard lock(s.mutex);
    s.run("DELETE FROM " + s.table + " WHERE PrincipalID=" + literal(recipient.str()));
}
}
