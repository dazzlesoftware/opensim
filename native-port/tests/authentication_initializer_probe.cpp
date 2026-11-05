// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_authentication_plugin.hpp"
#include "opensim/network/osd_json.hpp"
#include <sqlite3.h>
#include <filesystem>
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
    std::string out;
    for (size_t i = 0; i < s.size(); i += 2)
        out += char(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
std::optional<std::string> text(const Value& v) {
    if (v.kind == Value::Kind::Undefined)
        return {};
    auto s = network::osd_string(v);
    return s == "." ? "" : s;
}
std::string scalar(sqlite3* db, const char* sql) {
    sqlite3_stmt* s = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) {
        if (s)
            sqlite3_finalize(s);
        return "X";
    }
    std::string out;
    if (sqlite3_step(s) == SQLITE_ROW && sqlite3_column_type(s, 0) != SQLITE_NULL) {
        auto p = sqlite3_column_text(s, 0);
        out.assign(reinterpret_cast<const char*>(p), sqlite3_column_bytes(s, 0));
    }
    sqlite3_finalize(s);
    return out;
}
std::string snapshot(sqlite3* db) {
    return hex(scalar(
               db,
               "SELECT coalesce(group_concat(name||':'||quote(version),'|'),'') FROM (SELECT name,version FROM migrations ORDER BY name,version)")) +
           ":" +
           hex(scalar(
               db,
               "SELECT coalesce(group_concat(UUID||':'||quote(passwordHash)||':'||quote(passwordSalt)||':'||quote(webLoginKey),'|'),'') FROM (SELECT * FROM auth ORDER BY UUID)")) +
           ":" +
           hex(scalar(
               db,
               "SELECT coalesce(group_concat(UUID||':'||quote(token)||':'||CASE WHEN validity>datetime('now','localtime') THEN 'future' ELSE 'past' END,'|'),'') FROM (SELECT * FROM tokens ORDER BY rowid)"));
}
std::string row(const std::shared_ptr<AuthenticationRecord>& r) {
    if (!r)
        return "N";
    std::string result = r->principal.str() + ":";
    for (auto& [key, value] : *r->data)
        result += hex(key) + "=" + hex(std::any_cast<std::string>(value)) + ",";
    return result;
}
std::any value(const Value& x) {
    auto kind = network::osd_string(x.get("kind"));
    auto& v = x.get("value");
    if (kind == "null")
        return {};
    if (kind == "int")
        return network::osd_integer(v);
    if (kind == "bool")
        return network::osd_boolean(v);
    if (kind == "double")
        return network::osd_real(v);
    if (kind == "blob") {
        auto bytes = unhex(network::osd_string(v));
        return std::vector<uint8_t>(bytes.begin(), bytes.end());
    }
    return text(v).value_or("");
}
std::string shape(const std::shared_ptr<AuthenticationRecord>& r) {
    if (!r)
        return "N";
    if (!r->data)
        return "NULLDATA";
    std::string out;
    for (auto& [key, v] : *r->data)
        out += hex(key) + "=" +
               (!v.has_value()                             ? "N"
                : v.type() == typeid(std::string)          ? "String"
                : v.type() == typeid(int32_t)              ? "Int32"
                : v.type() == typeid(bool)                 ? "Boolean"
                : v.type() == typeid(double)               ? "Double"
                : v.type() == typeid(std::vector<uint8_t>) ? "Byte[]"
                                                           : "Object") +
               ",";
    return out;
}
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    std::string line;
    int index = 0;
    while (std::getline(std::cin, line)) {
        auto input = network::parse_osd_json(unhex(line));
        auto path = (std::filesystem::absolute(argv[1]) / std::to_string(index++)).string() + ".db";
        sqlite3* db = nullptr;
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
            return 3;
        auto setup = network::osd_string(input.get("setup"));
        if (sqlite3_exec(db, setup.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
            return 4;
        std::string trace;
        auto event = [&](std::string v) {
            trace += v + ";";
            if (v == network::osd_string(input.get("fail")))
                throw std::runtime_error("injected");
        };
        MigrationLog log{[&](auto s) { event("info:" + s); },
                         [&](std::string s) {
                             if (s.starts_with("[MIGRATIONS]: Cmd was ")) {
                                 auto at = s.find(" in SQL: ");
                                 if (at != s.npos)
                                     s = "[MIGRATIONS]: Cmd was SQLERROR" + s.substr(at);
                             }
                             event("debug:" + s);
                         },
                         [&](auto s) { event("error:" + s); }};
        int32_t tick = 0;
        auto state = std::make_shared<SqliteGenericState>();
        std::map<int, std::unique_ptr<SqliteAuthenticationPlugin>> slots;
        std::string output;
        framework::Uuid id("00000000-0000-0000-0000-000000000001");
        for (auto& op : input.get("ops").array) {
            std::string result = "-", form = "-";
            int slot = network::osd_integer(op.get("slot"));
            tick = network::osd_integer(op.get("tick"));
            try {
                auto kind = network::osd_string(op.get("op"));
                if (kind == "new") {
                    auto connection = network::osd_string(op.get("connection"));
                    for (size_t at = connection.find("$DB"); at != connection.npos;
                         at = connection.find("$DB", at + path.size()))
                        connection.replace(at, 3, path);
                    slots[slot] = std::make_unique<SqliteAuthenticationPlugin>(
                        connection, text(op.get("realm")).value_or(""), log, [&] { return tick; },
                        [&](const auto& channel, const auto& message, const auto&) {
                            event(channel == "console" ? "console" : "error:" + message);
                        },
                        state);
                } else if (kind == "raw") {
                    auto sql = network::osd_string(op.get("sql"));
                    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
                        throw std::runtime_error("raw");
                } else {
                    if (!slots.contains(slot))
                        throw std::runtime_error("Missing slot");
                    auto& p = *slots[slot];
                    if (kind == "get")
                        result = row(p.get(id));
                    else if (kind == "gc")
                        p.collect_readers();
                    else if (kind == "store") {
                        auto record = network::osd_boolean(op.get("nullrow"))
                                          ? std::shared_ptr<AuthenticationRecord>{}
                                          : std::make_shared<AuthenticationRecord>();
                        if (record) {
                            record->principal = id;
                            auto& d = op.get("data");
                            if (d.kind == Value::Kind::Array) {
                                record->data =
                                    std::make_shared<framework::OrderedDictionary<std::string, std::any>>();
                                for (auto& pair : d.array)
                                    (*record->data)[text(pair.array[0]).value()] = value(pair.array[1]);
                            }
                        }
                        try {
                            result = p.store(record) ? "1" : "0";
                        } catch (...) {
                            form = shape(record);
                            throw;
                        }
                        form = shape(record);
                    } else if (kind == "item")
                        result = p.set_data_item(id, text(op.get("item")), text(op.get("value"))) ? "1" : "0";
                    else if (kind == "set")
                        result =
                            p.set_token(id, text(op.get("token")), network::osd_integer(op.get("lifetime")))
                                ? "1"
                                : "0";
                    else if (kind == "check")
                        result =
                            p.check_token(id, text(op.get("token")), network::osd_integer(op.get("lifetime")))
                                ? "1"
                                : "0";
                }
            } catch (...) {
                result = "EX";
            }
            auto last = slots.contains(slot) ? std::to_string(slots[slot]->last_expire()) : "-";
            output += hex(result) + ":" + hex(form) + ":" + last + ":" +
                      (state->initialized.load() ? "1" : "0") + ":" + snapshot(db) + ";";
        }
        slots.clear();
        state.reset();
        sqlite3_close(db);
        std::cout << output << '|' << hex(trace) << '\n';
    }
}
