// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "mysql_connection.hpp"

#include "opensim/services/sqlite_connection.hpp"

#include <charconv>
#include <cctype>
#include <stdexcept>

namespace opensim::services {
MysqlConnection connect_mysql(const std::string& connection, unsigned long client_flags) {
    if (connection.empty())
        throw std::invalid_argument("MySQL connection string is empty");
    const auto options = parse_sqlite_connection(connection);
    const auto get = [&](std::initializer_list<const char*> aliases) {
        for (const auto* alias : aliases)
            if (const auto found = options.find(alias); found != options.end())
                return found->second;
        return std::string{};
    };
    const auto host = get({"Server", "Host", "Data Source"});
    const auto user = get({"User ID", "Uid", "Username", "User"});
    const auto password = get({"Password", "Pwd"});
    const auto database = get({"Database", "Initial Catalog"});
    const auto socket = get({"Unix Socket"});
    const auto port_text = get({"Port"});
    unsigned port = 0;
    if (!port_text.empty()) {
        const auto end = port_text.data() + port_text.size();
        const auto parsed = std::from_chars(port_text.data(), end, port);
        if (parsed.ec != std::errc{} || parsed.ptr != end || port > 65535)
            throw std::invalid_argument("Invalid MySQL port");
    }
    MysqlConnection handle(mysql_init(nullptr), mysql_close);
    auto* db = handle.get();
    if (!db)
        throw std::runtime_error("Cannot allocate MySQL connection");
    auto ssl_mode = get({"SSL Mode", "SslMode"});
    for (auto& character : ssl_mode)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    if (ssl_mode == "disable" || ssl_mode == "disabled" || ssl_mode == "none") {
#ifdef MARIADB_BASE_VERSION
        const my_bool disabled = 0;
        if (mysql_options(db, MYSQL_OPT_SSL_ENFORCE, &disabled) ||
            mysql_options(db, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &disabled))
#else
        const unsigned disabled = SSL_MODE_DISABLED;
        if (mysql_options(db, MYSQL_OPT_SSL_MODE, &disabled))
#endif
            throw std::runtime_error("Cannot configure MySQL SSL mode");
    }
    const auto timeout_text = get({"Connection Timeout", "ConnectionTimeout", "Connect Timeout"});
    if (!timeout_text.empty()) {
        unsigned timeout{};
        const auto end = timeout_text.data() + timeout_text.size();
        const auto parsed = std::from_chars(timeout_text.data(), end, timeout);
        if (parsed.ec != std::errc{} || parsed.ptr != end ||
            mysql_options(db, MYSQL_OPT_CONNECT_TIMEOUT, &timeout) != 0)
            throw std::invalid_argument("Invalid MySQL connection timeout");
    }
    if (!mysql_real_connect(db, host.empty() ? nullptr : host.c_str(), user.c_str(), password.c_str(),
                                   database.c_str(), port, socket.empty() ? nullptr : socket.c_str(), client_flags) ||
        mysql_set_character_set(db, "utf8mb4") != 0)
        throw std::runtime_error("Cannot connect to MySQL database (client error " +
                                 std::to_string(mysql_errno(db)) + ")");
    return handle;
}
}
