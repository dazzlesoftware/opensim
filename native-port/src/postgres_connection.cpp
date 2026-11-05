// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "postgres_connection.hpp"

#include "opensim/framework/culture_prefix.hpp"
#include "opensim/services/sqlite_connection.hpp"

#include <stdexcept>

namespace opensim::services {
namespace {
std::string postgres_connection_string(const std::string& connection)
{
    char* error = nullptr;
    auto* native = PQconninfoParse(connection.c_str(), &error);
    if (error)
        PQfreemem(error);
    if (native) {
        PQconninfoFree(native);
        return connection;
    }
    constexpr std::pair<const char*, const char*> aliases[] = {
        {"Server", "host"}, {"Host", "host"}, {"Port", "port"}, {"Database", "dbname"},
        {"User Id", "user"}, {"Username", "user"}, {"Password", "password"},
        {"Timeout", "connect_timeout"}, {"Application Name", "application_name"}, {"SSL Mode", "sslmode"}
    };
    std::string result;
    for (const auto& [key, value] : parse_sqlite_connection(connection)) {
        bool matched = false;
        for (const auto& [legacy, target] : aliases) {
            if (!framework::ordinal_equals_ignore_case(key, legacy))
                continue;
            matched = true;
            auto normalized = value;
            if (std::string_view(target) == "sslmode") {
                for (auto& c : normalized)
                    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
                if (normalized == "verifyca") normalized = "verify-ca";
                if (normalized == "verifyfull") normalized = "verify-full";
            }
            result += std::string(target) + "='";
            for (const char c : normalized) {
                if (c == '\\' || c == '\'') result += '\\';
                result += c;
            }
            result += "' ";
            break;
        }
        if (!matched)
            throw std::invalid_argument("Unsupported PostgreSQL connection property");
    }
    return result;
}
}

PostgresConnection connect_postgres(const std::string& connection) {
    if (connection.empty())
        throw std::invalid_argument("PostgreSQL connection string is empty");
    PostgresConnection handle(PQconnectdb(postgres_connection_string(connection).c_str()), PQfinish);
    if (PQstatus(handle.get()) != CONNECTION_OK || PQsetClientEncoding(handle.get(), "UTF8") != 0)
        throw std::runtime_error("Cannot connect to PostgreSQL database");
    return handle;
}
}
