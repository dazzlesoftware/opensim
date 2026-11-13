// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_maintenance.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <chrono>
#include <thread>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
namespace opensim::services {
namespace {
using Path = std::filesystem::path;
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::string text(const Path& path) {
    return framework::utf8(path.u16string());
}
[[noreturn]] void io_error(int code, const Path& path) {
#ifdef _WIN32
    auto kind = code == ERROR_ACCESS_DENIED    ? FlotsamFileFailure::Unauthorized
                : code == ERROR_FILE_NOT_FOUND ? FlotsamFileFailure::MissingFile
                : code == ERROR_PATH_NOT_FOUND ? FlotsamFileFailure::MissingDirectory
                                               : FlotsamFileFailure::Io;
    auto full = std::filesystem::absolute(path).lexically_normal();
    full.make_preferred();
    auto name = text(full);
    std::string message =
        code == ERROR_ACCESS_DENIED    ? "Access to the path '" + name + "' is denied."
        : code == ERROR_FILE_NOT_FOUND ? "Could not find file '" + name + "'."
        : code == ERROR_PATH_NOT_FOUND ? "Could not find a part of the path '" + name + "'."
        : code == ERROR_SHARING_VIOLATION
            ? "The process cannot access the file '" + name + "' because it is being used by another process."
            : std::system_category().message(code);
    throw FlotsamFileError(kind, message);
#else
    std::error_code ec;
    auto parent = path.parent_path();
    bool missing_directory =
        code == ENOTDIR ||
        (code == ENOENT && !std::filesystem::is_directory(parent.empty() ? Path(".") : parent, ec));
    auto kind = missing_directory                                   ? FlotsamFileFailure::MissingDirectory
                : code == ENOENT                                    ? FlotsamFileFailure::MissingFile
                : code == EACCES || code == EPERM || code == EISDIR ? FlotsamFileFailure::Unauthorized
                                                                    : FlotsamFileFailure::Io;
    auto name = text(std::filesystem::absolute(path).lexically_normal());
    auto message = missing_directory ? "Could not find a part of the path '" + name + "'."
                   : kind == FlotsamFileFailure::MissingFile  ? "Could not find file '" + name + "'."
                   : kind == FlotsamFileFailure::Unauthorized ? "Access to the path '" + name + "' is denied."
                                                              : std::generic_category().message(code);
    throw FlotsamFileError(kind, message);
#endif
}
void write_text(const Path& path, const std::string& value) {
#ifdef _WIN32
    auto handle = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        io_error(static_cast<int>(GetLastError()), path);
    std::size_t offset = 0;
    while (offset < value.size()) {
        DWORD written = 0;
        if (!WriteFile(handle, value.data() + offset,
                       static_cast<DWORD>(std::min<std::size_t>(value.size() - offset, 0x7fffffff)), &written,
                       nullptr) ||
            !written) {
            auto code = GetLastError();
            CloseHandle(handle);
            io_error(static_cast<int>(code), path);
        }
        offset += written;
    }
    if (!CloseHandle(handle))
        io_error(static_cast<int>(GetLastError()), path);
#else
    auto fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        io_error(errno, path);
    std::size_t offset = 0;
    while (offset < value.size()) {
        auto count = ::write(fd, value.data() + offset, value.size() - offset);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            auto code = errno;
            ::close(fd);
            io_error(code, path);
        }
        offset += static_cast<std::size_t>(count);
    }
    if (::close(fd) < 0)
        io_error(errno, path);
#endif
}
void set_write(const Path& path, std::int64_t ticks,
               const std::shared_ptr<framework::NativeInvariantDateTimeContext>& datetime) {
#ifdef _WIN32
    auto value = static_cast<std::uint64_t>(datetime->to_file_time_utc(framework::AssetDateTime(ticks, 1)));
    FILETIME time{static_cast<DWORD>(value), static_cast<DWORD>(value >> 32)};
    auto handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              0, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        io_error(static_cast<int>(GetLastError()), path);
    auto ok = SetFileTime(handle, nullptr, nullptr, &time);
    auto code = GetLastError();
    CloseHandle(handle);
    if (!ok)
        io_error(static_cast<int>(code), path);
#else
    (void)datetime;
    constexpr std::int64_t epoch = 621355968000000000ll;
    auto delta = ticks - epoch;
    auto seconds = delta / 10000000ll;
    auto fraction = delta % 10000000ll;
    if (fraction < 0) {
        --seconds;
        fraction += 10000000ll;
    }
    timespec times[2]{};
    times[0].tv_nsec = UTIME_OMIT;
    times[1].tv_sec = static_cast<time_t>(seconds);
    times[1].tv_nsec = static_cast<long>(fraction * 100);
    if (::utimensat(AT_FDCWD, path.c_str(), times, 0) < 0)
        io_error(errno, path);
#endif
}
std::vector<Path> entries(const Path& path, bool dirs) {
    std::vector<Path> result;
    std::error_code ec;
    std::filesystem::directory_iterator iterator(path, ec), end;
    if (ec)
        throw std::filesystem::filesystem_error("cache enumeration", path, ec);
    for (; iterator != end; iterator.increment(ec)) {
        if (ec)
            throw std::filesystem::filesystem_error("cache enumeration", path, ec);
        bool directory = iterator->is_directory(ec);
        if (ec)
            throw std::filesystem::filesystem_error("cache entry", iterator->path(), ec);
        if (directory == dirs)
            result.push_back(iterator->path());
    }
    if (ec)
        throw std::filesystem::filesystem_error("cache enumeration", path, ec);
    return result;
}
}
namespace {
std::shared_ptr<FlotsamMaintenanceBackend> native_filesystem_backend(
    std::function<void(const std::string&, const std::string&)> log,
    std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime);
}
FlotsamCacheMaintenance::FlotsamCacheMaintenance(
    Path dir, std::function<void(const std::string&, const std::string&)> log)
    : directory_(std::move(dir)), backend_(native_filesystem_backend(std::move(log), {})) {}

FlotsamCacheMaintenance::FlotsamCacheMaintenance(Path dir, std::shared_ptr<FlotsamMaintenanceBackend> backend)
    : directory_(std::move(dir)), backend_(std::move(backend)) {
    auto& b = backend_;
    if (!b || !b->combine || !b->file_exists || !b->now_utc_ticks || !b->set_write_utc || !b->write_text ||
        !b->directories || !b->file_count || !b->info || !b->warn || !b->sleep || !b->gather || !b->running ||
        !b->file_name || !b->check_access || !b->get_asset || !b->directory_exists || !b->files ||
        !b->remove_directory || !b->remove_file)
        throw std::invalid_argument("Incomplete Flotsam maintenance dependencies");
}
void FlotsamCacheMaintenance::clear_files() {
    if (!backend_->directory_exists(directory_))
        return;
    for (const auto& dir : backend_->directories(directory_)) {
        try {
            backend_->remove_directory(dir);
        } catch (const std::exception& e) {
            backend_->warn("[FLOTSAM ASSET CACHE]: Couldn't clear asset cache directory " +
                           framework::utf8(dir.u16string()) + " from " +
                           framework::utf8(directory_.u16string()) + ": " + e.what());
        }
    }
    for (const auto& file : backend_->files(directory_)) {
        try {
            backend_->remove_file(file);
        } catch (const std::exception& e) {
            backend_->warn("[FLOTSAM ASSET CACHE]: Couldn't clear asset cache file " +
                           framework::utf8(file.u16string()) + " from " +
                           framework::utf8(directory_.u16string()) + ": " + e.what());
        }
    }
}
void FlotsamCacheMaintenance::stamp(framework::Uuid region) {
    auto path = backend_->combine(directory_, "RegionStatus_" + region.str() + ".fac");
    try {
        if (backend_->file_exists(path))
            backend_->set_write_utc(path, backend_->now_utc_ticks());
        else
            backend_->write_text(path, std::string(status_text));
    } catch (const std::exception& e) {
        backend_->warn("[FLOTSAM ASSET CACHE]: Could not stamp region status file for region " +
                       region.str() + ": " + e.what());
    }
}
std::int32_t FlotsamCacheMaintenance::count(const Path& path) const noexcept {
    try {
        std::int32_t total = 0, cooldown = 0;
        for (auto& sub : backend_->directories(path)) {
            total = add(total, count(sub));
            cooldown = add(cooldown, 1);
            if (cooldown > 50) {
                backend_->sleep(100);
                cooldown = 0;
            }
        }
        return add(total, backend_->file_count(path));
    } catch (...) {
        return 0;
    }
}
std::int32_t FlotsamCacheMaintenance::touch(bool uncached) {
    if (!backend_->gather)
        throw std::logic_error("Flotsam scene gathering is unavailable");
    backend_->info("[FLOTSAM ASSET CACHE] start touch files of assets in use");
    auto state = backend_->gather();
    if (!state)
        throw std::runtime_error("Object reference not set to an instance of an object.");
    std::int32_t cooldown = 0;
    for (auto [id, type] : state->gathered) {
        if (!backend_->running())
            break;
        auto key = id.str();
        if (!backend_->check_access(backend_->file_name(key)) && uncached) {
            cooldown = add(cooldown, 5);
            backend_->get_asset(key);
        }
        cooldown = add(cooldown, 1);
        if (cooldown > 50) {
            backend_->sleep(50);
            cooldown = 0;
        }
    }
    return static_cast<std::int32_t>(state->gathered.size());
}
namespace {
std::shared_ptr<FlotsamMaintenanceBackend> native_filesystem_backend(
    std::function<void(const std::string&, const std::string&)> log,
    std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime) {
    if (!datetime)
        datetime = framework::shared_native_invariant_datetime_context();
    if (!log)
        throw std::invalid_argument("Native Flotsam maintenance requires a logger");
    auto b = std::make_shared<FlotsamMaintenanceBackend>();
    b->combine = [](auto& root, auto& name) { return root / Path(name); };
    auto io = native_flotsam_file_backend({});
    b->file_exists = io->file_exists;
    b->directory_exists = io->directory_exists;
    b->remove_directory = io->remove_directory_tree;
    b->remove_file = io->remove_file;
    b->files = [](auto& p) { return entries(p, false); };
    b->now_utc_ticks = [datetime] { return datetime->utc_now().ticks(); };
    b->set_write_utc = [datetime](const Path& path, std::int64_t ticks) { set_write(path, ticks, datetime); };
    b->write_text = write_text;
    b->directories = [](auto& p) { return entries(p, true); };
    b->file_count = [](auto& p) { return static_cast<std::int32_t>(entries(p, false).size()); };
    b->info = [log](auto& text) { log("info", text); };
    b->warn = [log](auto& text) { log("warn", text); };
    b->sleep = [](int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); };
    return b;
}
}
std::shared_ptr<FlotsamMaintenanceBackend> FlotsamCacheMaintenance::native_backend(
    std::shared_ptr<FlotsamFileStore> store, std::shared_ptr<FlotsamFileAccess> access,
    std::shared_ptr<AssetService> service, std::function<std::shared_ptr<scene::UuidGathererState>()> gather,
    std::function<bool()> running, std::function<void(const std::string&, const std::string&)> log,
    std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime) {
    if (!store || !access || !service || !gather || !running || !log)
        throw std::invalid_argument(
            "Native maintenance requires cache/access/service/scene/log dependencies");
    auto b = native_filesystem_backend(std::move(log), std::move(datetime));
    b->gather = std::move(gather);
    b->running = std::move(running);
    b->file_name = [store](auto& id) { return store->file_name(id); };
    b->check_access = [access](auto& path) { return access->check_update(path); };
    b->get_asset = [service](auto& id) { service->get_asset(id); };
    return b;
}
}
