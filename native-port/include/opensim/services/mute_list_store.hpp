// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/framework/uuid.hpp"

#include <functional>
#include <string>

namespace opensim::services {
struct MuteListStore {
    std::function<std::string(framework::Uuid)> read;
    std::function<bool(framework::Uuid, framework::Uuid, const std::string&,
                       std::int32_t, std::int32_t, std::int64_t)> write;
    std::function<bool(framework::Uuid, framework::Uuid, const std::string&)> erase;
};

MuteListStore mysql_mute_list_store(const std::string& connection);
MuteListStore postgres_mute_list_store(const std::string& connection);
}
