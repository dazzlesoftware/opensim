// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_migration.hpp"
#include "opensim/network/osd_json.hpp"
#include <sqlite3.h>
#include <iostream>
using namespace opensim;
using namespace services;
using network::Value;
std::string hex(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string unhex(std::string_view s) {
    if (s == ".")
        return "";
    std::string out;
    for (size_t i = 0; i < s.size(); i += 2)
        out += char(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
std::optional<std::string> nullable(const Value& v) {
    return v.kind == Value::Kind::Undefined ? std::nullopt
                                            : std::optional<std::string>(network::osd_string(v));
}
struct Resources : MigrationResources {
    Value input;
    std::string trace;
    explicit Resources(Value v) : input(std::move(v)) {
    }
    void event(std::string text) {
        constexpr auto prefix = "debug:[MIGRATIONS]: Cmd was ";
        if (text.starts_with(prefix)) {
            auto at = text.find(" in SQL: ");
            if (at != text.npos)
                text = std::string(prefix) + "SQLERROR" + text.substr(at);
        }
        trace += text + ";";
        if (network::osd_string(input.get("fail")) == text)
            throw std::runtime_error("injected");
    }
    std::vector<std::string> names() override {
        event("names");
        std::vector<std::string> out;
        for (auto& r : input.get("resources").array)
            out.push_back(network::osd_string(r.get("name")));
        return out;
    }
    std::optional<std::string> bytes(const std::string& name) override {
        event("read:" + name);
        for (auto& r : input.get("resources").array)
            if (network::osd_string(r.get("name")) == name) {
                if (r.get("data").kind == Value::Kind::Undefined)
                    return {};
                return unhex(network::osd_string(r.get("data")));
            }
        return {};
    }
};
void exec(sqlite3* db, const std::string& sql) {
    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}
std::string scalar(sqlite3* db, const char* sql) {
    sqlite3_stmt* q = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &q, nullptr) != SQLITE_OK) {
        if (q)
            sqlite3_finalize(q);
        return "X";
    }
    std::string out;
    auto status = sqlite3_step(q);
    if (status == SQLITE_ROW && sqlite3_column_type(q, 0) != SQLITE_NULL) {
        auto text = sqlite3_column_text(q, 0);
        out.assign(reinterpret_cast<const char*>(text), sqlite3_column_bytes(q, 0));
    } else if (status != SQLITE_ROW && status != SQLITE_DONE)
        out = "X";
    sqlite3_finalize(q);
    return out;
}
std::string snapshot(sqlite3* db) {
    return hex(scalar(
               db,
               "SELECT coalesce(group_concat(coalesce(name,'<null>')||':'||quote(version),'|'),'') FROM (SELECT name,version FROM migrations ORDER BY name,version)")) +
           ":" +
           hex(scalar(
               db,
               "SELECT coalesce(group_concat(name||':'||type,'|'),'') FROM (SELECT name,type FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY name,type)")) +
           ":" +
           hex(scalar(
               db,
               "SELECT coalesce(group_concat(quote(value),'|'),'') FROM (SELECT value FROM payload ORDER BY rowid)"));
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        auto input = network::parse_osd_json(unhex(line));
        auto resources = std::make_shared<Resources>(input);
        sqlite3* db = nullptr;
        if (sqlite3_open(":memory:", &db) != SQLITE_OK)
            return 2;
        try {
            exec(db, network::osd_string(input.get("setup")));
        } catch (...) {
            sqlite3_close(db);
            std::cout << "FIXTURE_EX\n";
            continue;
        }
        auto database = network::osd_boolean(input.get("no_db")) ? std::shared_ptr<MigrationDatabase>{}
                                                                 : sqlite_migration_database(db);
        std::unique_ptr<Migration> migration;
        try {
            migration = std::make_unique<Migration>(
                database, resources, nullable(input.get("sub")), nullable(input.get("type")),
                MigrationLog{[&](auto s) { resources->event("info:" + s); },
                             [&](auto s) { resources->event("debug:" + s); },
                             [&](auto s) { resources->event("error:" + s); }});
        } catch (...) {
        }
        std::string output;
        for (auto& op : input.get("ops").array) {
            std::string result = "-", kind = network::osd_string(op.get("op"));
            try {
                if (!migration)
                    throw std::runtime_error("constructor");
                if (kind == "update")
                    migration->update();
                else if (kind == "table")
                    migration->init_migrations_table();
                else if (kind == "version")
                    result = std::to_string(migration->version());
                else if (kind == "set")
                    migration->version(network::osd_integer(op.get("value")));
                else if (kind == "find")
                    result = std::to_string(migration->find_version(database, nullable(op.get("type"))));
                else if (kind == "execute") {
                    std::vector<std::string> sql;
                    for (auto& v : op.get("batches").array)
                        sql.push_back(network::osd_string(v));
                    migration->execute_script(database, sql);
                } else if (kind == "raw")
                    exec(db, network::osd_string(op.get("sql")));
                else if (kind == "initialize")
                    migration->initialize(database, resources, nullable(op.get("type")),
                                          nullable(op.get("sub")));
            } catch (...) {
                result = "EX";
            }
            output += hex(result) + ":" + snapshot(db) + ";";
        }
        migration.reset();
        sqlite3_close(db);
        std::cout << output << '|' << hex(resources->trace) << '\n';
    }
}
