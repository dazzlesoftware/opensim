// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <functional>
#include <optional>
#include <string>
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
int initialize_migrations(const Execute& execute, const std::string& store);
void record_migration(const Execute& execute, const std::string& store, int previous_version, int version);
}
