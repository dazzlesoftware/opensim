// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt. Original animation data retains its license.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <filesystem>
#include <map>
namespace opensim::scene {
struct DefaultAnimations {
    std::map<std::string, framework::Uuid> by_name;
    std::map<framework::Uuid, std::string> by_id, states;
    explicit DefaultAnimations(const std::filesystem::path& file);
    framework::Uuid resolve(std::string name) const;
    std::string name(framework::Uuid id) const;
};
}
