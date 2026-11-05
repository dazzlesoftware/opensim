// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_cleanup.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif
namespace opensim::services {
namespace {
using Date = framework::AssetDateTime;
using Path = std::filesystem::path;
constexpr std::int64_t epoch = 621355968000000000ll, maximum = 3155378975999999999ll;
std::int32_t add(std::int32_t n, int delta) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(n) + static_cast<std::uint32_t>(delta));
}
std::string text(const Path& path) {
    return framework::utf8(path.u16string());
}
std::string date_text(Date value) {
    using namespace std::chrono;
    auto elapsed = duration<std::int64_t, std::ratio<1, 10000000>>(value.ticks() - epoch);
    auto days = floor<std::chrono::days>(elapsed);
    year_month_day date{sys_days(days)};
    auto time = duration_cast<seconds>(elapsed - days);
    auto hour = duration_cast<hours>(time);
    auto minute = duration_cast<minutes>(time - hour);
    auto second = time - hour - minute;
    char out[40];
    std::snprintf(out, sizeof(out), "%02u/%02u/%04d %02lld:%02lld:%02lld", unsigned(date.month()),
                  unsigned(date.day()), int(date.year()), static_cast<long long>(hour.count()),
                  static_cast<long long>(minute.count()), static_cast<long long>(second.count()));
    return out;
}
[[noreturn]] void fs_error(std::error_code code, bool directory) {
    auto kind = code == std::errc::no_such_file_or_directory
                    ? (directory ? FlotsamFileFailure::MissingDirectory : FlotsamFileFailure::MissingFile)
                : code == std::errc::permission_denied ? FlotsamFileFailure::Unauthorized
                                                       : FlotsamFileFailure::Io;
    throw FlotsamFileError(kind, code.message());
}
std::vector<Path> enumerate(const Path& path, bool directories) {
    std::error_code ec;
    std::filesystem::directory_iterator it(path, ec), end;
    if (ec)
        fs_error(ec, true);
    std::vector<Path> entries;
    while (it != end) {
        bool is_dir = it->is_directory(ec);
        if (ec)
            fs_error(ec, true);
        if (is_dir == directories)
            entries.push_back(it->path());
        it.increment(ec);
        if (ec)
            fs_error(ec, true);
    }
    return entries;
}
Date native_access(const Path& path,
                   const std::shared_ptr<framework::NativeInvariantDateTimeContext>& datetime) {
    auto local_from_utc = [&](std::int64_t ticks) { return datetime->to_local_time(Date(ticks, 1)); };
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) {
        auto code = GetLastError();
        if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
            return local_from_utc(504911232000000000ll);
        throw FlotsamFileError(code == ERROR_ACCESS_DENIED ? FlotsamFileFailure::Unauthorized
                                                           : FlotsamFileFailure::Io,
                               std::system_category().message(static_cast<int>(code)));
    }
    auto ticks =
        (std::uint64_t(info.ftLastAccessTime.dwHighDateTime) << 32) | info.ftLastAccessTime.dwLowDateTime;
    if (ticks > static_cast<std::uint64_t>(maximum - 504911232000000000ll))
        throw std::out_of_range("File access time outside DateTime range");
    return datetime->to_local_time(datetime->from_file_time_utc(static_cast<std::int64_t>(ticks)));
#else
    struct stat status;
    if (::stat(path.c_str(), &status) < 0) {
        if (errno == ENOENT || errno == ENOTDIR)
            return local_from_utc(504911232000000000ll);
        fs_error({errno, std::generic_category()}, false);
    }
#ifdef __APPLE__
    auto seconds = status.st_atimespec.tv_sec;
    auto nanoseconds = status.st_atimespec.tv_nsec;
#else
    auto seconds = status.st_atim.tv_sec;
    auto nanoseconds = status.st_atim.tv_nsec;
#endif
    if (seconds < -62135596800ll || seconds > 253402300799ll)
        throw std::out_of_range("File access time outside DateTime range");
    return local_from_utc(epoch + std::int64_t(seconds) * 10000000ll + nanoseconds / 100);
#endif
}
}
FlotsamCacheCleaner::FlotsamCacheCleaner(Options options, std::shared_ptr<FlotsamCacheLifecycle> life,
                                         std::shared_ptr<FlotsamCacheSharedState> shared,
                                         std::shared_ptr<FlotsamCleanupBackend> backend)
    : options_(std::move(options)), life_(std::move(life)), shared_(std::move(shared)),
      backend_(std::move(backend)) {
    if (!life_ || !shared_ || !backend_ || !backend_->directory_exists || !backend_->directories ||
        !backend_->files || !backend_->file_name || !backend_->last_access || !backend_->erase_file ||
        !backend_->erase_directory || !backend_->erase_weak || !backend_->reset_weak || !backend_->info ||
        !backend_->warn || !backend_->sleep || !backend_->heap || !backend_->now || !backend_->gather)
        throw std::invalid_argument("Incomplete protected cache cleanup dependencies");
}
void FlotsamCacheCleaner::elapsed() {
    {
        std::lock_guard lock(life_->timer_mutex);
        if (!life_->timer_running || life_->cleanup_running ||
            !backend_->directory_exists(options_.directory))
            return;
        life_->cleanup_running = true;
    }
    auto now = backend_->now();
    auto expiration = options_.expiration_ticks;
    if (expiration > now.ticks() || expiration < now.ticks() - maximum)
        throw std::out_of_range("Cleanup purge line outside DateTime range");
    auto bits = (now.serialized_data() & 0xc000000000000000ull) |
                static_cast<std::uint64_t>(now.ticks() - expiration);
    clean_all(Date::from_serialized(bits));
}
void FlotsamCacheCleaner::clean_all(Date purge_line) {
    backend_->info("[FLOTSAM ASSET CACHE]: Start background expiring files older than " +
                   date_text(purge_line));
    auto heap = backend_->heap();
    auto protected_assets = backend_->gather();
    std::int32_t cooldown = 0;
    backend_->info("[FLOTSAM ASSET CACHE] start asset files expire");
    for (auto& directory : backend_->directories(options_.directory)) {
        if (!life_->cleanup_running)
            break;
        cooldown = clean_tree(directory, protected_assets, purge_line, cooldown);
        cooldown = add(cooldown, 1);
        if (cooldown >= 10) {
            backend_->sleep(120);
            cooldown = 0;
        }
    }
    backend_->reset_weak();
    {
        std::lock_guard lock(life_->timer_mutex);
        if (life_->timer_running) {
            if (!life_->timer)
                throw std::invalid_argument("Null cache cleanup timer");
            life_->timer->start();
        }
        life_->cleanup_running = false;
    }
    auto delta = std::bit_cast<std::int64_t>(static_cast<std::uint64_t>(backend_->heap()) -
                                             static_cast<std::uint64_t>(heap));
    backend_->info("[FLOTSAM ASSET CACHE]: Finished expiring files, heap delta: " +
                   std::to_string(delta / (1024 * 1024)) + "MB.");
}
std::int32_t FlotsamCacheCleaner::clean_tree(const Path& dir, const FlotsamProtectedAssets& protected_assets,
                                             Date purge_line, std::int32_t cooldown) {
    try {
        if (!life_->cleanup_running)
            return cooldown;
        std::int32_t size = 0;
        for (auto& child : backend_->directories(dir)) {
            if (!life_->cleanup_running)
                return cooldown;
            size = add(size, 1);
            cooldown = clean_tree(child, protected_assets, purge_line, cooldown);
            cooldown = add(cooldown, 1);
            if (cooldown > 10) {
                backend_->sleep(60);
                cooldown = 0;
            }
        }
        for (auto& file : backend_->files(dir)) {
            if (!life_->cleanup_running)
                return cooldown;
            size = add(size, 1);
            auto id = backend_->file_name(file);
            if (id.empty())
                continue;
            framework::Uuid uuid;
            bool is_default;
            {
                std::lock_guard lock(shared_->defaults_mutex);
                is_default = shared_->default_assets.load()->contains(id);
            }
            if (is_default || (framework::try_parse_sdk_uuid(id, uuid) && protected_assets.contains(uuid))) {
                cooldown = add(cooldown, 1);
                continue;
            }
            if (backend_->last_access(file).ticks() < purge_line.ticks()) {
                try {
                    backend_->erase_file(file);
                    backend_->erase_weak(id);
                } catch (...) {
                }
                cooldown = add(cooldown, 5);
                size = add(size, -1);
            }
            cooldown = add(cooldown, 1);
            if (cooldown >= 20) {
                backend_->sleep(60);
                cooldown = 0;
            }
        }
        if (life_->cleanup_running && size == 0) {
            try {
                backend_->erase_directory(dir);
            } catch (...) {
            }
            cooldown = add(cooldown, 5);
            if (cooldown >= 20) {
                backend_->sleep(60);
                cooldown = 0;
            }
        } else if (size >= options_.warn_at) {
            backend_->warn("[FLOTSAM ASSET CACHE]: Cache folder exceeded CacheWarnAt limit " + text(dir) +
                           " " + std::to_string(size) +
                           ". Suggest increasing tiers, tier length, or reducing cache expiration");
        }
    } catch (const FlotsamFileError& e) {
        if (e.kind != FlotsamFileFailure::MissingDirectory)
            backend_->warn("[FLOTSAM ASSET CACHE]: Could not complete clean of expired files in " +
                           text(dir) + ": " + e.what());
    } catch (const std::exception& e) {
        backend_->warn("[FLOTSAM ASSET CACHE]: Could not complete clean of expired files in " + text(dir) +
                       ": " + e.what());
    }
    return cooldown;
}
std::shared_ptr<FlotsamCleanupBackend> FlotsamCacheCleaner::native_backend(
    std::shared_ptr<FlotsamMemoryCache> cache, std::function<FlotsamProtectedAssets()> gather,
    std::function<std::int64_t()> heap, std::function<void(const std::string&, const std::string&)> log,
    std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime) {
    if (!datetime)
        datetime = framework::shared_native_invariant_datetime_context();
    if (!cache || !gather || !heap)
        throw std::invalid_argument("Missing native cache/gather/heap dependency");
    auto backend = std::make_shared<FlotsamCleanupBackend>();
    backend->gather = std::move(gather);
    backend->heap = std::move(heap);
    backend->directory_exists = [](const Path& path) {
        std::error_code ec;
        return std::filesystem::is_directory(path, ec);
    };
    backend->directories = [](auto& path) { return enumerate(path, true); };
    backend->files = [](auto& path) { return enumerate(path, false); };
    backend->file_name = [](auto& path) { return text(path.filename()); };
    backend->last_access = [datetime](const Path& path) { return native_access(path, datetime); };
    backend->erase_file = native_flotsam_file_backend({})->remove_file;
    backend->erase_directory = [](auto& path) {
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) {
            if (ec)
                fs_error(ec, true);
            throw FlotsamFileError(FlotsamFileFailure::MissingDirectory, "Directory not found");
        }
        std::filesystem::remove(path, ec);
        if (ec)
            fs_error(ec, true);
    };
    backend->erase_weak = [cache](auto& id) {
        std::lock_guard lock(cache->mutex_);
        cache->weak_.erase(id);
    };
    backend->reset_weak = [cache] {
        std::lock_guard lock(cache->mutex_);
        cache->weak_.clear();
        cache->stats_.weak_hits = 0;
    };
    backend->info = [log](auto& text) {
        if (log)
            log("INFO", text);
    };
    backend->warn = [log](auto& text) {
        if (log)
            log("WARN", text);
    };
    backend->sleep = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    backend->now = [datetime] { return datetime->now(); };
    return backend;
}
}
