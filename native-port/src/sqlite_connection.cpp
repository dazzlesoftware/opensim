// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_connection.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <stdexcept>
#include <vector>
namespace opensim::services {
namespace {
std::string unwrap(std::string value) {
    if (!value.empty() &&
        ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
        if (value.size() < 2)
            throw std::invalid_argument("Invalid SQLite quoted string");
        return value.substr(1, value.size() - 2);
    }
    return value;
}
bool starts(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           framework::ordinal_equals_ignore_case(value.substr(0, prefix.size()), prefix);
}
}
bool SqliteConnectionKeyOrder::operator()(const std::string& a, const std::string& b) const {
    return framework::ordinal_compare_ignore_case(a, b) < 0;
}
SqliteConnectionFields parse_sqlite_connection(const std::string& connection, bool allow_name_only) {
    std::vector<std::string> parts;
    std::string part;
    bool escape = false, quoted = false;
    for (char c : connection) {
        if (escape) {
            if (c != '\\' && c != '"' && c != ';')
                part += '\\';
            part += c;
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            part += c;
            quoted = !quoted;
            continue;
        }
        if (c == ';' && !quoted) {
            parts.push_back(std::move(part));
            part.clear();
        } else
            part += c;
    }
    if (escape || quoted)
        throw std::invalid_argument("Unbalanced SQLite escape or quote");
    if (!part.empty())
        parts.push_back(std::move(part));
    SqliteConnectionFields result;
    for (auto& item : parts) {
        item = framework::trim_utf8(item);
        if (item.empty())
            continue;
        auto at = item.find('=');
        if (at == item.npos && !allow_name_only)
            throw std::invalid_argument("SQLite connection property has no equal sign");
        auto key = unwrap(framework::trim_utf8(item.substr(0, at)));
        auto value = at == item.npos ? std::string{} : unwrap(framework::trim_utf8(item.substr(at + 1)));
        if (!result.emplace(std::move(key), std::move(value)).second)
            throw std::invalid_argument("Duplicate SQLite connection property");
    }
    return result;
}
std::string sqlite_uri_path(const std::string& uri) {
    if (starts(uri, "file://"))
        return uri.substr(7);
    if (starts(uri, "file:"))
        return uri.substr(5);
    if (starts(uri, "/"))
        return uri;
    throw std::invalid_argument("Invalid SQLite URI");
}
std::string sqlite_connection_path(const SqliteConnectionFields& fields) {
    auto data = fields.find("Data Source");
    if (data == fields.end())
        data = fields.find("DataSource");
    if (data == fields.end())
        data = fields.find("Data_Source");
    if (data != fields.end() && !data->second.empty())
        return framework::ordinal_equals_ignore_case(data->second, ":memory:") ? ":memory:" : data->second;
    auto uri = fields.find("URI");
    if (uri != fields.end() && !uri->second.empty()) {
        auto path = sqlite_uri_path(uri->second);
        return framework::ordinal_equals_ignore_case(path, ":memory:") ? ":memory:" : path;
    }
    auto full = fields.find("FullUri");
    if (full != fields.end() && !full->second.empty())
        throw std::invalid_argument("FullUri SQLite open flags are not implemented");
    throw std::invalid_argument("SQLite data source is missing");
}
bool sqlite_boolean(const std::string& value) {
    if (value.size() <= 4 && starts("True", value))
        return true;
    if (value.size() <= 5 && starts("False", value))
        return false;
    for (auto s : {"1", "y", "yes", "on"})
        if (framework::ordinal_equals_ignore_case(value, s))
            return true;
    for (auto s : {"0", "n", "no", "off"})
        if (framework::ordinal_equals_ignore_case(value, s))
            return false;
    throw std::invalid_argument("Invalid SQLite Boolean");
}
}
