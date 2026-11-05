// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/services/flotsam_cache_module.hpp"
#include "opensim/services/flotsam_memory_cache.hpp"
namespace opensim::services {
using FlotsamProtectedAssets = std::unordered_set<framework::Uuid, framework::UuidHash>;
struct FlotsamCleanupBackend {
    using Path = std::filesystem::path;
    std::function<bool(const Path&)> directory_exists;
    std::function<std::vector<Path>(const Path&)> directories, files;
    std::function<std::string(const Path&)> file_name;
    std::function<framework::AssetDateTime(const Path&)> last_access;
    std::function<void(const Path&)> erase_file, erase_directory;
    std::function<void(const std::string&)> erase_weak;
    std::function<void()> reset_weak;
    std::function<void(const std::string&)> info, warn;
    std::function<void(int)> sleep;
    std::function<std::int64_t()> heap;
    std::function<framework::AssetDateTime()> now;
    // A complete successful scene gather is required before a sweep. The
    // original gatherer/scene adapter is separate required work, not an empty
    // default in this backend.
    std::function<FlotsamProtectedAssets()> gather;
};
class FlotsamCacheCleaner {
public:
    struct Options {
        std::filesystem::path directory;
        std::int64_t expiration_ticks = 48ll * 3600 * 10000000;
        std::int32_t warn_at = 30000;
    };
    FlotsamCacheCleaner(Options, std::shared_ptr<FlotsamCacheLifecycle>,
                        std::shared_ptr<FlotsamCacheSharedState>, std::shared_ptr<FlotsamCleanupBackend>);
    void elapsed();
    void clean_all(framework::AssetDateTime purge_line);
    std::int32_t clean_tree(const std::filesystem::path&, const FlotsamProtectedAssets&,
                            framework::AssetDateTime purge_line, std::int32_t cooldown = 0);
    // Caller supplies scene gathering and the heap metric explicitly. The
    // factory provides native I/O, clock, logging and exact weak-only maintenance.
    static std::shared_ptr<FlotsamCleanupBackend>
    native_backend(std::shared_ptr<FlotsamMemoryCache>, std::function<FlotsamProtectedAssets()>,
                   std::function<std::int64_t()>,
                   std::function<void(const std::string& level, const std::string& message)> log = {},
                   std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime = {});

private:
    Options options_;
    std::shared_ptr<FlotsamCacheLifecycle> life_;
    std::shared_ptr<FlotsamCacheSharedState> shared_;
    std::shared_ptr<FlotsamCleanupBackend> backend_;
};
}
