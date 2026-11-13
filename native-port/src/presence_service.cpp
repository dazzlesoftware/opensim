/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/services/presence_service.hpp"
#include "opensim/services/sqlite_generic_connection.hpp"
#include <sqlite3.h>

namespace opensim::services {
namespace {
class PresenceStatement {
  public:
    PresenceStatement(sqlite3* db, const std::string& sql, std::initializer_list<std::string> values)
        : db_(db) {
        sqlite3_stmt* statement = nullptr;
        const auto status = sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr);
        statement_.reset(statement);
        check(status);
        int index = 1;
        for (const auto& value : values)
            check(sqlite3_bind_text(statement, index++, value.c_str(), -1, SQLITE_TRANSIENT));
    }
    bool next() {
        const auto status = sqlite3_step(statement_.get());
        if (status == SQLITE_ROW)
            return true;
        if (status != SQLITE_DONE)
            check(status);
        return false;
    }
    std::string column(int index) const {
        const auto* value = sqlite3_column_text(statement_.get(), index);
        return value ? reinterpret_cast<const char*>(value) : "";
    }

  private:
    void check(int status) const {
        if (status != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
    }
    sqlite3* db_;
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement_{nullptr, sqlite3_finalize};
};

bool presence_field(const std::string& field) {
    return field == "SessionID" || field == "UserID" || field == "RegionID" || field == "SecureSessionID";
}
}

SqliteTravelStore::SqliteTravelStore(const std::string& path, std::string_view realm)
    : database_(std::make_shared<SqliteDatabase>()) {
    if (realm.empty() || realm.find('\0') != realm.npos)
        throw std::invalid_argument("Invalid Hypergrid travel realm");
    table_ = '"';
    for (const auto character : realm) {
        if (character == '"')
            table_ += '"';
        table_ += character;
    }
    table_ += '"';
    database_->open_path(path, true);
    auto* const db = database_->handle();
    sqlite3_busy_timeout(db, 5000);
    const auto execute = [db](const std::string& statement) {
        PresenceStatement(db, statement, {}).next();
    };
    const auto create = [&](const std::string& table) {
        execute("CREATE TABLE IF NOT EXISTS " + table + " ("
                "SessionID VARCHAR(36) NOT NULL PRIMARY KEY, UserID VARCHAR(36) NOT NULL UNIQUE, "
                "GridExternalName VARCHAR(255) NOT NULL DEFAULT '', ServiceToken VARCHAR(255) NOT NULL DEFAULT '', "
                "ClientIPAddress VARCHAR(16) NOT NULL DEFAULT '', MyIPAddress VARCHAR(16) NOT NULL DEFAULT '', "
                "TMStamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP)");
    };

    execute("BEGIN IMMEDIATE");
    try {
        const std::string default_table = "\"hg_traveling_data\"";
        create(default_table);
        if (table_ != default_table)
            create(table_);
        execute("CREATE TABLE IF NOT EXISTS migrations(name varchar(100), version int)");
        execute("INSERT INTO migrations(name, version) SELECT 'migrations', 1 "
                "WHERE NOT EXISTS (SELECT 1 FROM migrations WHERE name = 'migrations')");
        execute("INSERT INTO migrations(name, version) SELECT 'HGTravelStore', 2 "
                "WHERE NOT EXISTS (SELECT 1 FROM migrations WHERE name = 'HGTravelStore')");
        execute("UPDATE migrations SET version = 2 WHERE name = 'HGTravelStore' AND version < 2");
        execute("DELETE FROM " + table_ + " WHERE TMStamp < datetime('now', '-2 day')");
        execute("COMMIT");
    } catch (...) {
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

std::optional<TravelRecord> SqliteTravelStore::get(const framework::Uuid& session) {
    std::lock_guard lock(database_->mutex());
    PresenceStatement row(database_->handle(),
                          "SELECT UserID, GridExternalName, ServiceToken, ClientIPAddress, MyIPAddress "
                          "FROM " + table_ + " WHERE SessionID = ?",
                          {session.str()});
    if (!row.next())
        return std::nullopt;
    return TravelRecord{
        session, framework::Uuid(row.column(0)), row.column(1), row.column(2), row.column(3), row.column(4)};
}

std::optional<TravelRecord> SqliteTravelStore::get_by_user(const framework::Uuid& user) {
    std::lock_guard lock(database_->mutex());
    PresenceStatement row(database_->handle(),
                          "SELECT SessionID, GridExternalName, ServiceToken, ClientIPAddress, MyIPAddress "
                          "FROM " + table_ + " WHERE UserID = ?",
                          {user.str()});
    if (!row.next())
        return std::nullopt;
    return TravelRecord{
        framework::Uuid(row.column(0)), user, row.column(1), row.column(2), row.column(3), row.column(4)};
}

std::optional<TravelRecord> SqliteTravelStore::replaced_by(const framework::Uuid& session,
                                                          const framework::Uuid& user) {
    if (auto current = get(session))
        return current;
    return get_by_user(user);
}

std::optional<TravelRecord> SqliteTravelStore::locate(const framework::Uuid& user, std::string_view home) {
    auto record = get_by_user(user);
    return record && !record->grid.empty() && record->grid != home ? record : std::nullopt;
}

void SqliteTravelStore::store(const TravelRecord& record) {
    std::lock_guard lock(database_->mutex());
    PresenceStatement(database_->handle(),
                      "INSERT OR REPLACE INTO " + table_ +
                      " (SessionID, UserID, GridExternalName, ServiceToken, ClientIPAddress, MyIPAddress) "
                      "VALUES (?, ?, ?, ?, ?, ?)",
                      {record.session_id.str(), record.user_id.str(), record.grid, record.service_token,
                       record.client_ip, record.external_ip})
        .next();
}

void SqliteTravelStore::erase(const framework::Uuid& session) {
    std::lock_guard lock(database_->mutex());
    PresenceStatement(database_->handle(), "DELETE FROM " + table_ + " WHERE SessionID = ?",
                      {session.str()})
        .next();
}

SqlitePresenceStore::SqlitePresenceStore(const std::string& path)
    : database_(std::make_shared<SqliteDatabase>()) {
    database_->open_path(path, true);
    sqlite3_busy_timeout(database_->handle(), 5000);
    PresenceStatement(
        database_->handle(),
        "CREATE TABLE IF NOT EXISTS Presence (SessionID TEXT PRIMARY KEY, UserID TEXT NOT NULL, "
        "RegionID TEXT NOT NULL, SecureSessionID TEXT NOT NULL)",
        {})
        .next();
    PresenceStatement(database_->handle(), "CREATE INDEX IF NOT EXISTS Presence_UserID ON Presence (UserID)",
                      {})
        .next();
    PresenceStatement(database_->handle(),
                      "CREATE INDEX IF NOT EXISTS Presence_RegionID ON Presence (RegionID)", {})
        .next();
}

bool SqlitePresenceStore::store(const std::shared_ptr<PresenceData>& data) {
    if (!data)
        return false;
    std::lock_guard lock(database_->mutex());
    const auto secure = data->data.find("SecureSessionID");
    PresenceStatement(
        database_->handle(),
        "INSERT OR REPLACE INTO Presence (SessionID, UserID, RegionID, SecureSessionID) VALUES (?, ?, ?, ?)",
        {data->session_id.str(), data->user_id, data->region_id.str(),
         secure == data->data.end() ? framework::Uuid{}.str() : secure->second})
        .next();
    return true;
}

std::shared_ptr<PresenceData> SqlitePresenceStore::get(const framework::Uuid& session) {
    auto rows = get("SessionID", session.str());
    return rows.empty() ? nullptr : rows.front();
}

std::vector<std::shared_ptr<PresenceData>> SqlitePresenceStore::get(const std::string& field,
                                                                    const std::string& value) {
    if (!presence_field(field))
        return {};
    std::lock_guard lock(database_->mutex());
    PresenceStatement query(
        database_->handle(),
        "SELECT SessionID, UserID, RegionID, SecureSessionID FROM Presence WHERE " + field + " = ?", {value});
    std::vector<std::shared_ptr<PresenceData>> rows;
    while (query.next()) {
        auto row = std::make_shared<PresenceData>();
        row->session_id = framework::Uuid(query.column(0));
        row->user_id = query.column(1);
        row->region_id = framework::Uuid(query.column(2));
        row->data["SecureSessionID"] = query.column(3);
        rows.push_back(std::move(row));
    }
    return rows;
}

bool SqlitePresenceStore::erase(const std::string& field, const std::string& value) {
    if (!presence_field(field))
        return false;
    std::lock_guard lock(database_->mutex());
    PresenceStatement(database_->handle(), "DELETE FROM Presence WHERE " + field + " = ?", {value}).next();
    return sqlite3_changes(database_->handle()) != 0;
}

void SqlitePresenceStore::logout_region(const framework::Uuid& region) {
    erase("RegionID", region.str());
}

bool SqlitePresenceStore::erase_session_in_region(const framework::Uuid& session,
                                                  const framework::Uuid& region) {
    std::lock_guard lock(database_->mutex());
    PresenceStatement(database_->handle(), "DELETE FROM Presence WHERE SessionID = ? AND RegionID = ?",
                      {session.str(), region.str()})
        .next();
    return sqlite3_changes(database_->handle()) != 0;
}

bool SqlitePresenceStore::report(const framework::Uuid& session, const framework::Uuid& region) {
    std::lock_guard lock(database_->mutex());
    PresenceStatement(database_->handle(), "UPDATE Presence SET RegionID = ? WHERE SessionID = ?",
                      {region.str(), session.str()})
        .next();
    return sqlite3_changes(database_->handle()) != 0;
}

bool SqlitePresenceStore::verify(const framework::Uuid& agent, const framework::Uuid& secure_session) {
    std::lock_guard lock(database_->mutex());
    return PresenceStatement(database_->handle(),
                             "SELECT 1 FROM Presence WHERE UserID = ? AND SecureSessionID = ?",
                             {agent.str(), secure_session.str()})
        .next();
}
}
#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace opensim::services {
using framework::Uuid;
namespace {
std::optional<Uuid> parse(const std::string& value) {
    try {
        return Uuid(value);
    } catch (...) {
        return std::nullopt;
    }
}
double milliseconds() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
std::int32_t integer(double value) {
    return value >= -2147483648.0 && value < 2147483648.0 ? static_cast<std::int32_t>(value)
                                                          : std::numeric_limits<std::int32_t>::min();
}
}
struct NullPresenceStore::State {
    struct Slot {
        Uuid key;
        std::shared_ptr<PresenceData> value;
    };
    std::recursive_mutex mutex;
    std::vector<Slot> slots;
    std::vector<std::size_t> free;
    std::unordered_map<Uuid, std::size_t, framework::UuidHash> index;
    bool match(const Slot& slot, const std::string& field, const std::string& value) const {
        if (!slot.value)
            return false;
        if (field == "UserID")
            return slot.value->user_id == value;
        if (field == "SessionID") {
            const auto id = parse(value);
            return id && slot.key == *id;
        }
        if (field == "RegionID") {
            const auto id = parse(value);
            return id && slot.value->region_id == *id;
        }
        const auto found = slot.value->data.find(field);
        return found != slot.value->data.end() && found->second == value;
    }
};
NullPresenceStore::NullPresenceStore() {
    static auto singleton = std::make_shared<State>();
    state_ = singleton;
}
bool NullPresenceStore::store(const std::shared_ptr<PresenceData>& data) {
    if (!data)
        throw std::invalid_argument("Missing presence data");
    std::lock_guard lock(state_->mutex);
    auto found = state_->index.find(data->session_id);
    if (found != state_->index.end())
        state_->slots[found->second].value = data;
    else {
        std::size_t index;
        if (state_->free.empty()) {
            index = state_->slots.size();
            state_->slots.push_back({data->session_id, data});
        } else {
            index = state_->free.back();
            state_->free.pop_back();
            state_->slots[index] = {data->session_id, data};
        }
        state_->index[data->session_id] = index;
    }
    return true;
}
std::shared_ptr<PresenceData> NullPresenceStore::get(const Uuid& session) {
    std::lock_guard lock(state_->mutex);
    const auto found = state_->index.find(session);
    return found == state_->index.end() ? nullptr : state_->slots[found->second].value;
}
std::vector<std::shared_ptr<PresenceData>> NullPresenceStore::get(const std::string& field,
                                                                  const std::string& value) {
    std::lock_guard lock(state_->mutex);
    std::vector<std::shared_ptr<PresenceData>> result;
    for (const auto& slot : state_->slots)
        if (state_->match(slot, field, value))
            result.push_back(slot.value);
    return result;
}
bool NullPresenceStore::erase(const std::string& field, const std::string& value) {
    std::lock_guard lock(state_->mutex);
    bool erased = false;
    for (std::size_t i = 0; i < state_->slots.size(); ++i)
        if (state_->match(state_->slots[i], field, value)) {
            state_->index.erase(state_->slots[i].key);
            state_->slots[i].value.reset();
            state_->free.push_back(i);
            erased = true;
        }
    return erased;
}
void NullPresenceStore::logout_region(const Uuid& region) {
    erase("RegionID", region.str());
}
bool NullPresenceStore::report(const Uuid& session, const Uuid& region) {
    std::lock_guard lock(state_->mutex);
    auto value = get(session);
    if (!value)
        return false;
    value->region_id = region;
    return true;
}
struct PresenceCache::State {
    struct Entry {
        std::shared_ptr<PresenceData> value;
        std::int32_t expires;
    };
    std::recursive_mutex mutex;
    std::condition_variable_any changed;
    bool stopping = false;
    Clock clock;
    double start;
    std::unordered_map<Uuid, Entry, framework::UuidHash> sessions;
    std::unordered_map<std::string, Entry> users;
    std::optional<double> session_purge, user_purge;
    std::thread worker;
    explicit State(Clock value) : clock(value ? value : Clock{milliseconds}), start(clock()) {
        if (!value)
            worker = std::thread([this] {
                std::unique_lock lock(mutex);
                while (!stopping) {
                    if (!session_purge && !user_purge) {
                        changed.wait(lock, [&] { return stopping || session_purge || user_purge; });
                        continue;
                    }
                    const auto deadline = std::min(session_purge.value_or(std::numeric_limits<double>::max()),
                                                   user_purge.value_or(std::numeric_limits<double>::max()));
                    if (clock() < deadline) {
                        changed.wait_for(lock, std::chrono::duration<double, std::milli>(deadline - clock()));
                        continue;
                    }
                    purge_due();
                }
            });
    }
    ~State() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            changed.notify_all();
        }
        if (worker.joinable())
            worker.join();
    }
    std::int32_t now() const {
        return integer(clock() - start);
    }
    void purge_due(bool force = false) {
        const auto timestamp = clock();
        const auto relative = now();
        const auto clean = [&](auto& values, auto& next) {
            if (force || (next && *next <= timestamp)) {
                std::erase_if(values, [&](const auto& pair) {
                    return pair.second.expires > 0 && pair.second.expires < relative;
                });
                next = values.empty() ? std::nullopt : std::optional{timestamp + 60000};
            }
        };
        clean(sessions, session_purge);
        clean(users, user_purge);
    }
    std::shared_ptr<PresenceData> session(const Uuid& id) {
        const auto found = sessions.find(id);
        return found == sessions.end() ? nullptr : found->second.value;
    }
    std::shared_ptr<PresenceData> user(const std::string& id) {
        const auto found = users.find(id);
        return found == users.end() ? nullptr : found->second.value;
    }
    void add(const std::shared_ptr<PresenceData>& data, std::optional<Uuid> session_key = {},
             std::optional<std::string> user_key = {}) {
        const auto expires = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(now()) + 300000u);
        if (sessions.empty())
            session_purge = clock() + 60000;
        if (users.empty())
            user_purge = clock() + 60000;
        sessions[session_key.value_or(data->session_id)] = {data, expires};
        users[user_key.value_or(data->user_id)] = {data, expires};
        changed.notify_all();
    }
    void remove_session(const Uuid& id) {
        sessions.erase(id);
        if (sessions.empty())
            session_purge.reset();
    }
    void remove_user(const std::string& id) {
        users.erase(id);
        if (users.empty())
            user_purge.reset();
    }
};
PresenceCache::PresenceCache(Clock clock) : state_(std::make_unique<State>(std::move(clock))) {
}
PresenceCache::~PresenceCache() = default;
void PresenceCache::purge() {
    std::lock_guard lock(state_->mutex);
    state_->purge_due(true);
}
PresenceService::PresenceService(PresenceStore& store, bool allow_duplicates,
                                 std::shared_ptr<PresenceCache> cache)
    : store_(store), allow_duplicates_(allow_duplicates), cache_(std::move(cache)) {
    if (!cache_) {
        static auto shared = std::make_shared<PresenceCache>();
        cache_ = shared;
    }
}
bool PresenceService::login(const std::string& user, const Uuid& session, const Uuid& secure_session) {
    auto& cache = *cache_->state_;
    std::lock_guard lock(cache.mutex);
    auto previous = cache.user(user);
    const bool cached = static_cast<bool>(previous);
    if (!previous) {
        const auto rows = store_.get("UserID", user);
        if (!rows.empty())
            previous = rows.front();
    }
    if (!allow_duplicates_ && previous) {
        store_.erase("UserID", user);
        if (cached) {
            cache.remove_session(previous->session_id);
            cache.remove_user(user);
        }
    }
    auto data = std::make_shared<PresenceData>();
    data->user_id = user;
    data->session_id = session;
    data->data["SecureSessionID"] = secure_session.str();
    store_.store(data);
    cache.add(data, session, user);
    return true;
}
bool PresenceService::logout_from_region(const Uuid& session, const Uuid& region) {
    if (!store_.persistent()) {
        logout(session);
        return true;
    }
    auto& cache = *cache_->state_;
    std::lock_guard lock(cache.mutex);
    const auto previous = cache.session(session);
    const bool removed = store_.erase_session_in_region(session, region);
    if (previous) {
        cache.remove_session(session);
        cache.remove_user(previous->user_id);
    }
    return removed;
}
bool PresenceService::logout(const Uuid& session) {
    auto& cache = *cache_->state_;
    std::lock_guard lock(cache.mutex);
    auto previous = cache.session(session);
    const bool cached = static_cast<bool>(previous);
    if (!previous)
        previous = store_.get(session);
    const auto result = store_.erase("SessionID", session.str());
    if (cached) {
        cache.remove_session(session);
        if (previous)
            cache.remove_user(previous->user_id);
    }
    return result;
}
bool PresenceService::logout_region(const Uuid& region) {
    auto& cache = *cache_->state_;
    std::lock_guard lock(cache.mutex);
    const auto rows = store_.get("RegionID", region.str());
    if (rows.empty())
        return true;
    for (const auto& value : rows) {
        cache.remove_session(value->session_id);
        cache.remove_user(value->user_id);
    }
    store_.logout_region(region);
    return true;
}
bool PresenceService::report(const Uuid& session, const Uuid& region) {
    try {
        auto& cache = *cache_->state_;
        std::lock_guard lock(cache.mutex);
        auto data = cache.session(session);
        const bool cached = static_cast<bool>(data);
        if (!data)
            data = store_.get(session);
        const bool success = data && store_.report(session, region);
        if (success) {
            data->region_id = region;
            cache.add(data, session);
        } else if (cached) {
            cache.remove_session(session);
            cache.remove_user(data->user_id);
        }
        return success;
    } catch (...) {
        return false;
    }
}
std::optional<PresenceInfo> PresenceService::agent(const Uuid& session) {
    auto& cache = *cache_->state_;
    std::lock_guard lock(cache.mutex);
    auto data = cache.session(session);
    if (!data)
        data = store_.get(session);
    if (!data)
        return std::nullopt;
    cache.add(data, session);
    return PresenceInfo{data->user_id, data->region_id};
}
std::vector<PresenceInfo> PresenceService::agents(const std::vector<std::string>& users) {
    auto& cache = *cache_->state_;
    std::lock_guard lock(cache.mutex);
    std::vector<PresenceInfo> result;
    for (const auto& user : users) {
        auto data = cache.user(user);
        if (!data) {
            const auto rows = store_.get("UserID", user);
            if (rows.empty())
                continue;
            data = rows.front();
        }
        cache.add(data);
        result.push_back({data->user_id, data->region_id});
    }
    return result;
}
}
