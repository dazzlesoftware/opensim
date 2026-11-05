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
#include "opensim/services/grid_user_service.hpp"
#include "opensim/framework/filesystem.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <mutex>
#include <set>
#include <stdexcept>
namespace opensim::services {
namespace {
std::string quoted(const std::string& name) {
    std::string value = "\"";
    for (auto c : name) {
        if (c == '\0')
            throw std::invalid_argument("Invalid SQLite identifier");
        if (c == '\"')
            value += '\"';
        value += c;
    }
    return value + '\"';
}
struct Statement {
    sqlite3* db;
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* database, const std::string& sql) : db(database) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &value, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() {
        sqlite3_finalize(value);
    }
    void bind(int index, const std::optional<std::string>& text) {
        if ((text ? sqlite3_bind_text(value, index, text->data(), static_cast<int>(text->size()),
                                      SQLITE_TRANSIENT)
                  : sqlite3_bind_null(value, index)) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    bool row() {
        const auto result = sqlite3_step(value);
        if (result == SQLITE_ROW)
            return true;
        if (result == SQLITE_DONE)
            return false;
        throw std::runtime_error(sqlite3_errmsg(db));
    }
    std::string text(int index) {
        const auto* bytes = sqlite3_column_text(value, index);
        return bytes ? std::string(reinterpret_cast<const char*>(bytes), sqlite3_column_bytes(value, index))
                     : std::string{};
    }
};
}
struct SqliteGridUserStore::State {
    sqlite3* db = nullptr;
    std::recursive_mutex mutex;
    std::string table;
    std::set<std::string> columns;
    ~State() {
        if (db)
            sqlite3_close(db);
    }
    std::vector<std::shared_ptr<GridUserData>> query(const std::string& condition,
                                                     const std::string& argument) {
        Statement statement(db, "SELECT * FROM " + table + " WHERE " + condition);
        statement.bind(1, argument);
        std::vector<std::shared_ptr<GridUserData>> result;
        while (statement.row()) {
            auto row = std::make_shared<GridUserData>();
            for (int i = 0; i < sqlite3_column_count(statement.value); ++i) {
                const std::string column = sqlite3_column_name(statement.value, i);
                if (column == "UserID")
                    row->user_id = statement.text(i);
                else
                    (*row->data)[column] = statement.text(i);
            }
            result.push_back(row);
        }
        return result;
    }
};
SqliteGridUserStore::SqliteGridUserStore(const std::string& path, bool initialize, const std::string& realm)
    : state_(std::make_unique<State>()) {
    if (path.empty() || realm.empty())
        throw std::invalid_argument("Grid-user database and realm must not be empty");
    state_->table = quoted(realm);
    if (initialize && path != ":memory:" && std::filesystem::exists(framework::utf8_path(path)) &&
        std::filesystem::file_size(framework::utf8_path(path)) > 0)
        throw std::runtime_error("Grid-user initialization requires an empty destination");
    const auto flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | (initialize ? SQLITE_OPEN_CREATE : 0);
    if (sqlite3_open_v2(path.c_str(), &state_->db, flags, nullptr) != SQLITE_OK)
        throw std::runtime_error(state_->db ? sqlite3_errmsg(state_->db) : "SQLite allocation failed");
    sqlite3_busy_timeout(state_->db, 5000);
    if (initialize) {
        Statement create(
            state_->db,
            "CREATE TABLE " + state_->table +
                "(UserID VARCHAR(255) NOT NULL PRIMARY KEY,HomeRegionID CHAR(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',HomePosition CHAR(64) NOT NULL DEFAULT '<0,0,0>',HomeLookAt CHAR(64) NOT NULL DEFAULT '<0,0,0>',LastRegionID CHAR(36) NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',LastPosition CHAR(64) NOT NULL DEFAULT '<0,0,0>',LastLookAt CHAR(64) NOT NULL DEFAULT '<0,0,0>',Online CHAR(5) NOT NULL DEFAULT 'false',Login CHAR(16) NOT NULL DEFAULT '0',Logout CHAR(16) NOT NULL DEFAULT '0')");
        create.row();
    }
    Statement columns(state_->db, "PRAGMA table_info(" + state_->table + ")");
    while (columns.row())
        state_->columns.insert(columns.text(1));
    if (!state_->columns.contains("UserID"))
        throw std::runtime_error("Grid-user table is missing UserID");
}
SqliteGridUserStore::~SqliteGridUserStore() = default;
std::shared_ptr<GridUserData> SqliteGridUserStore::get(const std::string& user) {
    std::lock_guard lock(state_->mutex);
    auto rows = state_->query("UserID=?", user);
    return rows.empty() ? nullptr : rows.front();
}
std::vector<std::shared_ptr<GridUserData>> SqliteGridUserStore::get_all(const std::string& prefix) {
    std::lock_guard lock(state_->mutex);
    return state_->query("UserID LIKE ?", prefix + "%");
}
bool SqliteGridUserStore::store(const std::shared_ptr<GridUserData>& row) {
    if (!row || !row->data)
        throw std::invalid_argument("Missing grid-user data");
    std::lock_guard lock(state_->mutex);
    std::string names = "UserID", values = "?";
    for (const auto& [name, value] : *row->data) {
        if (name == "UserID" || !state_->columns.contains(name))
            throw std::invalid_argument("Unknown grid-user data column");
        names += ',' + quoted(name);
        values += ",?";
    }
    Statement statement(state_->db,
                        "REPLACE INTO " + state_->table + "(" + names + ") VALUES(" + values + ")");
    statement.bind(1, row->user_id);
    int index = 2;
    for (const auto& pair : *row->data)
        statement.bind(index++, pair.second);
    statement.row();
    return sqlite3_changes(state_->db) > 0;
}
}
