// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "sql_service_connection.hpp"

#ifdef OPENSIM_HAVE_MYSQL
#include "mysql_connection.hpp"
#endif
#ifdef OPENSIM_HAVE_POSTGRESQL
#include "postgres_connection.hpp"
#endif

#include <charconv>
#include <stdexcept>

namespace opensim::services::sql {
namespace {
std::string bind_parameters(const std::string& sql, const Parameters& values, const auto& parameter) {
    std::string statement;
    std::size_t start = 0;
    std::size_t index = 0;
    for (const auto& value : values) {
        const auto marker = sql.find('?', start);
        if (marker == std::string::npos)
            throw std::logic_error("Too many service SQL parameters");
        statement.append(sql, start, marker - start);
        statement += parameter(value, ++index);
        start = marker + 1;
    }
    if (sql.find('?', start) != std::string::npos)
        throw std::logic_error("Missing service SQL parameters");
    statement.append(sql, start, std::string::npos);
    return statement;
}

}
int initialize_migrations(const Execute& execute, const std::string& store) {
    execute("CREATE TABLE IF NOT EXISTS migrations (name varchar(100), version int)", {});
    execute("INSERT INTO migrations (name,version) SELECT 'migrations',1"
            " WHERE NOT EXISTS (SELECT 1 FROM migrations WHERE name='migrations' AND version >= 1)", {});
    const auto result = execute("SELECT COALESCE(MAX(version),0) FROM migrations WHERE name=?", {store});
    const auto& value = result.rows.at(0).at(0);
    int version = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), version);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw std::runtime_error("Invalid SQL service schema version");
    return version;
}

void record_migration(const Execute& execute, const std::string& store, int previous_version, int version) {
    if (previous_version > 0)
        execute("UPDATE migrations SET version=? WHERE name=?", {std::to_string(version), store});
    else
        execute("INSERT INTO migrations (name,version) VALUES (?,?)", {store, std::to_string(version)});
}

#ifdef OPENSIM_HAVE_MYSQL
Execute mysql(const std::string& connection) {
    const auto db = std::make_shared<MysqlConnection>(connect_mysql(connection, CLIENT_FOUND_ROWS));
    return [db](const std::string& sql, const Parameters& parameters) {
        const auto statement = bind_parameters(sql, parameters, [](const auto& value, std::size_t) {
            if (!value)
                return std::string("NULL");
            constexpr char digits[] = "0123456789abcdef";
            std::string literal = "CONVERT(X'";
            for (const unsigned char byte : *value) {
                literal.push_back(digits[byte >> 4]);
                literal.push_back(digits[byte & 15]);
            }
            return literal + "' USING utf8mb4)";
        });
        if (mysql_real_query(db->get(), statement.data(), static_cast<unsigned long>(statement.size())) != 0)
            throw std::runtime_error("MySQL service query failed (client error " +
                                     std::to_string(mysql_errno(db->get())) + ")");
        std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)> rows(mysql_store_result(db->get()),
                                                                     mysql_free_result);
        if (!rows && mysql_field_count(db->get()) != 0)
            throw std::runtime_error("MySQL service result failed");
        Result result;
        if (!rows) {
            result.affected = mysql_affected_rows(db->get()) > 0;
            return result;
        }
        const auto count = mysql_num_fields(rows.get());
        const auto fields = mysql_fetch_fields(rows.get());
        for (unsigned index = 0; index < count; ++index)
            result.names.emplace_back(fields[index].name);
        while (const auto row = mysql_fetch_row(rows.get())) {
            const auto lengths = mysql_fetch_lengths(rows.get());
            auto& columns = result.rows.emplace_back();
            for (unsigned index = 0; index < count; ++index)
                columns.emplace_back(row[index] ? std::string(row[index], lengths[index]) : std::string{});
        }
        return result;
    };
}
#endif

#ifdef OPENSIM_HAVE_POSTGRESQL
Execute postgres(const std::string& connection) {
    const auto db = std::make_shared<PostgresConnection>(connect_postgres(connection));
    return [db](const std::string& sql, const Parameters& parameters) {
        const auto statement = bind_parameters(sql, parameters, [](const auto&, std::size_t index) {
            return "$" + std::to_string(index);
        });
        std::vector<const char*> values;
        for (const auto& value : parameters) {
            if (value && value->find('\0') != std::string::npos)
                throw std::invalid_argument("PostgreSQL service value contains NUL");
            values.push_back(value ? value->c_str() : nullptr);
        }
        std::unique_ptr<PGresult, decltype(&PQclear)> rows(
            PQexecParams(db->get(), statement.c_str(), static_cast<int>(values.size()), nullptr, values.data(),
                         nullptr, nullptr, 0), PQclear);
        const auto status = PQresultStatus(rows.get());
        if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK)
            throw std::runtime_error("PostgreSQL service query failed");
        Result result;
        result.affected = status == PGRES_COMMAND_OK && PQcmdTuples(rows.get())[0] != '\0' &&
                          PQcmdTuples(rows.get())[0] != '0';
        for (int index = 0; index < PQnfields(rows.get()); ++index)
            result.names.emplace_back(PQfname(rows.get(), index));
        for (int row = 0; row < PQntuples(rows.get()); ++row) {
            auto& columns = result.rows.emplace_back();
            for (int index = 0; index < PQnfields(rows.get()); ++index)
                columns.emplace_back(PQgetvalue(rows.get(), row, index));
        }
        return result;
    };
}
#endif
}
