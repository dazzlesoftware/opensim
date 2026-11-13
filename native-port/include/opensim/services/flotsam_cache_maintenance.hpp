// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/services/flotsam_file_store.hpp"
#include "opensim/services/flotsam_file_access.hpp"
#include "opensim/scene/live_asset_gather.hpp"
namespace opensim::services {
struct FlotsamMaintenanceBackend {
    using Path = std::filesystem::path;
    std::function<Path(const Path&, const std::string&)> combine;
    std::function<bool(const Path&)> file_exists, directory_exists;
    std::function<std::vector<Path>(const Path&)> files;
    std::function<void(const Path&)> remove_directory, remove_file;
    // UTC instant is the native boundary for File.SetLastWriteTime(DateTime.Now).
    std::function<std::int64_t()> now_utc_ticks;
    std::function<void(const Path&, std::int64_t)> set_write_utc;
    std::function<void(const Path&, const std::string&)> write_text;
    std::function<std::vector<Path>(const Path&)> directories;
    std::function<std::int32_t(const Path&)> file_count;
    std::function<void(const std::string&)> info, warn;
    std::function<void(int)> sleep;
    std::function<std::shared_ptr<scene::UuidGathererState>()> gather;
    std::function<bool()> running;
    std::function<Path(const std::string&)> file_name;
    std::function<bool(const Path&)> check_access;
    std::function<void(const std::string&)> get_asset;
};
class FlotsamCacheMaintenance {
  public:
    using Path = std::filesystem::path;
    FlotsamCacheMaintenance(Path, std::shared_ptr<FlotsamMaintenanceBackend>);
    FlotsamCacheMaintenance(Path, std::function<void(const std::string&, const std::string&)> log);
    void stamp(framework::Uuid);
    // Console clear removes root files as well as subdirectories.
    void clear_files();
    std::int32_t count(const Path&) const noexcept;
    std::int32_t touch(bool try_get_uncached);
    static constexpr std::string_view status_text =
        "Please do not delete this file unless you are manually clearing your Flotsam Asset Cache.";
    static std::shared_ptr<FlotsamMaintenanceBackend>
    native_backend(std::shared_ptr<FlotsamFileStore>, std::shared_ptr<FlotsamFileAccess>,
                   std::shared_ptr<AssetService>, std::function<std::shared_ptr<scene::UuidGathererState>()>,
                   std::function<bool()>, std::function<void(const std::string&, const std::string&)> log,
                   std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime = {});

  private:
    Path directory_;
    std::shared_ptr<FlotsamMaintenanceBackend> backend_;
};
}
