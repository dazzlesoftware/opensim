// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/sqlite_generic_connection.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include <functional>
struct sqlite3_stmt;
namespace opensim::services {
using SqliteTableFields = framework::OrderedDictionary<std::string, std::optional<std::string>>;
using SqliteTableStrings = std::vector<std::optional<std::string>>;
using SqliteTableParameters = std::vector<std::pair<std::string, std::optional<std::string>>>;
class SqliteRow {
  public:
    explicit SqliteRow(sqlite3_stmt* statement) : statement_(statement) {
    }
    int column(const std::string& name) const;
    std::string string_value(int column) const;
    std::string blob_value(int column) const;
    std::string value_text(int column) const;
    std::int32_t int32_value(int column) const;

  private:
    sqlite3_stmt* statement_;
};
class SqliteTableOperations {
  public:
    using Connection = std::function<std::shared_ptr<SqliteDatabase>()>;
    SqliteTableOperations(Connection connection, std::string realm,
                          std::shared_ptr<SqliteQueryState> query_state = {})
        : connection_(std::move(connection)), realm_(std::move(realm)),
          query_(query_state ? std::move(query_state) : std::make_shared<SqliteQueryState>()) {
    }
    void query(const std::string& sql, const SqliteTableParameters& parameters,
               const std::vector<std::string>& fields,
               const std::function<void(const SqliteRow&, const std::vector<std::string>&)>& consume);
    int execute(const std::string& sql, const SqliteTableParameters& parameters);
    const std::string& realm() const {
        return realm_;
    }
    static std::string where(const SqliteTableStrings& fields, const SqliteTableStrings& keys,
                             SqliteTableParameters& parameters);

  private:
    Connection connection_;
    std::string realm_;
    std::shared_ptr<SqliteQueryState> query_;
};
// Explicit C++ field bindings replace CLR FieldInfo. Keep declaration order and
// bind only fields declared by the original record type, including its Data field.
template <class T> struct SqliteTableSchema {
    struct Field {
        std::string name;
        std::function<std::optional<std::string>(T&)> write;
        std::function<void(T&, const SqliteRow&, int)> read;
    };
    std::vector<Field> fields;
    std::function<std::shared_ptr<SqliteTableFields>&(T&)> data;
};
template <class T> class SqliteGenericTable {
  public:
    using Schema = SqliteTableSchema<T>;
    using Rows = std::vector<std::shared_ptr<T>>;
    SqliteGenericTable(SqliteTableOperations::Connection connection, std::string realm, Schema schema,
                       std::shared_ptr<SqliteQueryState> query_state = {})
        : operations_(std::move(connection), std::move(realm), std::move(query_state)),
          schema_(std::move(schema)) {
    }
    Rows get_field(std::optional<std::string> field, std::optional<std::string> key) {
        SqliteTableStrings fields{std::move(field)}, keys{std::move(key)};
        return get_fields(&fields, &keys);
    }
    Rows get_fields(const SqliteTableStrings* fields, const SqliteTableStrings* keys) {
        if (!fields || !keys)
            throw std::invalid_argument("Null SQLite query array");
        if (fields->size() != keys->size())
            return {};
        SqliteTableParameters p;
        auto where = SqliteTableOperations::where(*fields, *keys, p);
        return query("select * from " + operations_.realm() + " where " + where, p);
    }
    Rows get_where(std::optional<std::string> where) {
        return query("select * from " + operations_.realm() + " where " + where.value_or(""), {});
    }
    Rows query(const std::string& sql, const SqliteTableParameters& parameters) {
        Rows result;
        std::vector<std::string> fields;
        for (auto& f : schema_.fields)
            fields.push_back(f.name);
        operations_.query(sql, parameters, fields, [&](const auto& row, const auto& columns) {
            auto item = std::make_shared<T>();
            for (auto& f : schema_.fields)
                f.read(*item, row, row.column(f.name));
            if (schema_.data) {
                auto data = std::make_shared<SqliteTableFields>();
                for (auto& name : columns)
                    (*data)[name] = row.value_text(row.column(name));
                schema_.data(*item) = std::move(data);
            }
            result.push_back(std::move(item));
        });
        return result;
    }
    bool store(const std::shared_ptr<T>& row) {
        if (!row)
            throw std::invalid_argument("Null SQLite row");
        std::vector<std::string> names;
        SqliteTableParameters p;
        for (auto& f : schema_.fields) {
            auto value = f.write(*row);
            if (!value)
                throw std::invalid_argument("Null SQLite row field");
            names.push_back(f.name);
            p.emplace_back(":" + f.name, std::move(value));
        }
        if (schema_.data) {
            auto& data = schema_.data(*row);
            if (!data)
                throw std::invalid_argument("Null SQLite row Data");
            for (auto& [name, value] : *data) {
                names.push_back(name);
                p.emplace_back(":" + name, value);
            }
        }
        std::string columns, values;
        for (auto& name : names) {
            if (!columns.empty()) {
                columns += "`,`";
                values += ",";
            }
            columns += name;
            values += ":" + name;
        }
        return operations_.execute("replace into " + operations_.realm() + " (`" + columns + "`) values (" +
                                       values + ")",
                                   p) > 0;
    }
    bool delete_field(std::optional<std::string> field, std::optional<std::string> key) {
        SqliteTableStrings fields{std::move(field)}, keys{std::move(key)};
        return delete_fields(&fields, &keys);
    }
    bool delete_fields(const SqliteTableStrings* fields, const SqliteTableStrings* keys) {
        if (!fields || !keys)
            throw std::invalid_argument("Null SQLite delete array");
        if (fields->size() != keys->size())
            return false;
        SqliteTableParameters p;
        auto where = SqliteTableOperations::where(*fields, *keys, p);
        return operations_.execute("delete from " + operations_.realm() + " where " + where, p) > 0;
    }
    int execute(const std::string& sql, const SqliteTableParameters& parameters) {
        return operations_.execute(sql, parameters);
    }

  private:
    SqliteTableOperations operations_;
    Schema schema_;
};
}
