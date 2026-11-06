// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opensim::services::sql {
using Parameters = std::vector<std::optional<std::string>>;

struct Result {
    std::vector<std::string> names;
    std::vector<std::vector<std::string>> rows;
    bool affected = false;
};

using Execute = std::function<Result(const std::string&, const Parameters&)>;

Execute mysql(const std::string& connection);
Execute postgres(const std::string& connection);
// Column views are valid only while the callback is running.
using RowVisitor = std::function<void(const std::vector<std::string_view>&)>;
void stream_mysql(const std::string& connection, const std::string& statement, const RowVisitor& visit);
void stream_postgres(const std::string& connection, const std::string& statement, const RowVisitor& visit);
int initialize_migrations(const Execute& execute, const std::string& store);
void record_migration(const Execute& execute, const std::string& store, int previous_version, int version);
}
