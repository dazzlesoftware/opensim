// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/native_asset_storage.hpp"
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
std::string scalar(sqlite3 *db, const char *sql) {
    sqlite3_stmt *q = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &q, nullptr) != SQLITE_OK) {
        if (q)
            sqlite3_finalize(q);
        return "X";
    }
    std::string out;
    if (sqlite3_step(q) == SQLITE_ROW && sqlite3_column_type(q, 0) != SQLITE_NULL) {
        auto value = sqlite3_column_text(q, 0);
        out.assign(reinterpret_cast<const char *>(value), sqlite3_column_bytes(q, 0));
    }
    sqlite3_finalize(q);
    return out;
}
std::string snapshot(sqlite3 *db) {
    return hex(scalar(
               db,
               "SELECT coalesce(group_concat(name||':'||quote(version),'|'),'') FROM (SELECT name,version FROM migrations ORDER BY name,version)")) +
           ":" +
           hex(scalar(
               db,
               "SELECT coalesce(group_concat(name||':'||type,'|'),'') FROM (SELECT name,type FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY name,type)"));
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    std::string line;
    int index = 0;
    while (std::getline(std::cin, line)) {
        auto input = network::parse_osd_json(unhex(line));
        auto path = (std::filesystem::absolute(argv[1]) / std::to_string(index++)).string() + ".db";
        sqlite3 *db = nullptr;
        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
            return 3;
        auto setup = network::osd_string(input.get("setup"));
        if (sqlite3_exec(db, setup.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_close(db);
            std::cout << "FIXTURE_EX\n";
            continue;
        }
        std::string trace;
        auto event = [&](const std::string &level, const std::string &message) {
            auto value = level + ":" + message;
            trace += value + ";";
            if (value == network::osd_string(input.get("fail")))
                throw std::runtime_error("injected");
        };
        SqliteAssetPlugin plugin([&](auto s) { event("warn", s); },
                                 {[&](auto s) { event("info", s); }, [&](auto s) { event("debug", s); },
                                  [&](auto s) { event("error", s); }});
        std::string output;
        framework::Uuid id("00000000-0000-0000-0000-000000000001");
        for (auto &op : input.get("ops").array) {
            auto kind = network::osd_string(op.get("op"));
            std::string result = "-";
            try {
                if (kind == "init") {
                    auto connection = network::osd_string(op.get("connection"));
                    for (size_t at = connection.find("$DB"); at != connection.npos;
                         at = connection.find("$DB", at + path.size()))
                        connection.replace(at, 3, path);
                    plugin.initialise(connection);
                } else if (kind == "get") {
                    auto asset = plugin.get(id);
                    result =
                        !asset ? "N"
                               : asset->id() + ":" + asset->name().value_or("") + ":" +
                                     (asset->data ? hex(std::string(asset->data->begin(), asset->data->end()))
                                                  : "N");
                } else if (kind == "store") {
                    auto asset = std::make_shared<framework::AssetBase>();
                    asset->full_id(id);
                    asset->name("asset");
                    asset->data =
                        std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{1, 2});
                    result = plugin.store(asset) ? "1" : "0";
                } else if (kind == "exists") {
                    result = "";
                    for (bool b : plugin.exists({id}))
                        result += b ? '1' : '0';
                } else if (kind == "empty")
                    result = std::to_string(plugin.exists({}).size());
                else if (kind == "invalid")
                    result = plugin.erase("bad") ? "1" : "0";
                else if (kind == "dispose")
                    plugin.dispose();
                else if (kind == "raw") {
                    auto sql = network::osd_string(op.get("sql"));
                    if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) != SQLITE_OK)
                        throw std::runtime_error("raw");
                }
            } catch (...) {
                result = "EX";
            }
            output += hex(result) + ":" + snapshot(db) + ";";
        }
        plugin.dispose();
        sqlite3_close(db);
        std::cout << output << '|' << hex(trace) << '\n';
    }
}
