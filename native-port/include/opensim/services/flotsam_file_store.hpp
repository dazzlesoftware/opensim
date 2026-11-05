// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/object_job_engine.hpp"
#include <filesystem>
#include <functional>
#include <mutex>
#include <set>
#include <stdexcept>
namespace opensim::services {
enum class FlotsamFileFailure { Io, Unauthorized, MissingFile, MissingDirectory, Serialization, Other };
struct FlotsamFileError : std::runtime_error {
    FlotsamFileFailure kind;
    FlotsamFileError(FlotsamFileFailure k, const std::string& message) : runtime_error(message), kind(k) {}
};
struct FlotsamFileReader {
    std::function<std::uint64_t()> length;
    std::function<std::shared_ptr<framework::AssetBase>()> deserialize;
    std::function<void()> close;
};
struct FlotsamFileBackend {
    using Path = std::filesystem::path;
    std::function<bool()> worker_available;
    std::function<void(std::function<void()>)> enqueue;
    std::function<bool(const Path&)> file_exists, directory_exists;
    std::function<Path()> random_file_name;
    std::function<void(const Path&)> create_directory, remove_file;
    std::function<std::vector<Path>(const Path&)> list_directories;
    std::function<void(const Path&)> remove_directory_tree;
    std::function<void(const Path&, const Path&)> move_file;
    std::function<void(const Path&, const std::shared_ptr<framework::AssetBase>&)> write_asset;
    std::function<FlotsamFileReader(const Path&)> open_read;
    std::function<void(const Path&, int)> note_access;
    std::function<void(const Path&)> touch_access;
    std::function<void(const std::string&)> warn;
};
struct FlotsamFileState {
    mutable std::mutex mutex;
    std::set<std::u16string> writing;
};
// Original file path/queue/write/read transactions. Module lifecycle, access-time
// throttling, cache composition and protected-scene cleanup remain separate.
class FlotsamFileStore {
public:
    // Only the writing guard is shared across cache instances. Source read
    // counters belong to each module/store, even when paths and guard coincide.
    struct Statistics {
        std::uint64_t requests_in_progress = 0, disk_hits = 0;
    };
    Statistics statistics() const;
    struct Options {
        std::filesystem::path directory;
        int tiers = 1, tier_length = 3;
        bool update_access = false;
    };
    FlotsamFileStore(Options, std::shared_ptr<FlotsamFileBackend>,
                     std::shared_ptr<FlotsamFileState> state = {});
    std::u16string file_name_text(const std::string& id) const;
    std::filesystem::path file_name(const std::string& id) const;
    void cache(const std::string& id, std::shared_ptr<framework::AssetBase>, bool replace = false);
    std::shared_ptr<framework::AssetBase> get(const std::string& id);
    bool check(const std::string& id) const;
    bool writing(const std::string& id) const;
    std::shared_ptr<FlotsamFileState> state() const {
        return state_;
    }
    const std::filesystem::path& directory() const {
        return options_.directory;
    }

private:
    Options options_;
    std::shared_ptr<FlotsamFileBackend> backend_;
    std::shared_ptr<FlotsamFileState> state_;
    mutable std::mutex statistics_mutex_;
    Statistics statistics_;
    static void write(const std::filesystem::path&, std::shared_ptr<framework::AssetBase>, bool, bool,
                      const std::shared_ptr<FlotsamFileBackend>&, const std::shared_ptr<FlotsamFileState>&);
};
// Caller owns/quiesces the executor. Backend captures it weakly, so queued jobs
// retain I/O dependencies without owning their own executor or its lifetime.
std::shared_ptr<FlotsamFileBackend>
native_flotsam_file_backend(std::shared_ptr<framework::ObjectJobQueue>,
                            std::function<void(const std::string&)> warn = {});
}
