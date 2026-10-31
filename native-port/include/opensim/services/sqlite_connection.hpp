// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <map>
#include <string>
namespace opensim::services {
struct SqliteConnectionKeyOrder {
 bool operator()(const std::string& a,const std::string& b)const;
};
using SqliteConnectionFields=std::map<std::string,std::string,SqliteConnectionKeyOrder>;
// Bundled System.Data.SQLite default parser (not the optional framework/legacy parsers).
SqliteConnectionFields parse_sqlite_connection(const std::string& connection,bool allow_name_only=false);
std::string sqlite_uri_path(const std::string& uri);
std::string sqlite_connection_path(const SqliteConnectionFields& fields);
bool sqlite_boolean(const std::string& value);
}
