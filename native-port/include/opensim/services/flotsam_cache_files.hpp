// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/flotsam_memory_cache.hpp"
#include "opensim/services/flotsam_file_store.hpp"
namespace opensim::services {
std::shared_ptr<FlotsamMemoryCache::FileOperations>
flotsam_cache_files(std::shared_ptr<FlotsamFileStore>, std::shared_ptr<FlotsamFileBackend>,
                    std::function<void(const std::string&)> debug = {},
                    std::function<void(const std::string&)> warn = {});
}
