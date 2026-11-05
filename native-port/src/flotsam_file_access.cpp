// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_file_access.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <optional>
#include <system_error>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#endif
namespace opensim::services {
namespace {
constexpr int check_ms = 300000;
std::int32_t narrow(double elapsed) {
    return std::isfinite(elapsed) && elapsed >= -2147483648.0 && elapsed < 2147483648.0
               ? static_cast<std::int32_t>(elapsed)
               : INT32_MIN;
}
class TimedEntries final : public FlotsamAccessEntries {
  public:
    TimedEntries(std::function<double()> clock, bool background)
        : clock_(clock ? std::move(clock) : [] {
              const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
              return std::chrono::duration<double, std::milli>(elapsed).count();
          }),
          start_(clock_()) {
        if (background)
            worker_ = std::thread([this] { run(); });
    }
    ~TimedEntries() override {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
            changed_.notify_all();
        }
        if (worker_.joinable())
            worker_.join();
    }
    bool contains(const std::filesystem::path& path) override {
        std::lock_guard lock(mutex_);
        return values_.contains(path.u16string());
    }
    void add(const std::filesystem::path& path, int ms) override {
        auto now = clock_();
        auto expiry = ms > 0 ? std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(narrow(now - start_)) +
                                                           static_cast<std::uint32_t>(std::max(ms, check_ms)))
                             : INT32_MIN;
        std::lock_guard lock(mutex_);
        values_[path.u16string()] = expiry;
        if (!due_)
            due_ = clock_() + check_ms;
        changed_.notify_all();
    }
    void pump() override {
        std::lock_guard lock(mutex_);
        purge();
    }

  private:
    std::function<double()> clock_;
    double start_;
    std::optional<double> due_;
    std::unordered_map<std::u16string, std::int32_t> values_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    bool stop_ = false;
    void purge() {
        auto now = clock_();
        if (!due_ || *due_ > now)
            return;
        auto elapsed = narrow(now - start_);
        for (auto it = values_.begin(); it != values_.end();)
            if (it->second > 0 && it->second < elapsed)
                it = values_.erase(it);
            else
                ++it;
        if (values_.empty())
            due_.reset();
        else
            due_ = clock_() + check_ms;
    }
    void run() {
        std::unique_lock lock(mutex_);
        while (!stop_) {
            purge();
            if (!due_) {
                changed_.wait(lock);
                continue;
            }
            changed_.wait_for(lock,
                              std::chrono::duration<double, std::milli>(std::max(1.0, *due_ - clock_())));
        }
    }
};
}
std::shared_ptr<FlotsamAccessEntries> flotsam_access_entries(std::function<double()> clock, bool background) {
    return std::make_shared<TimedEntries>(std::move(clock), background);
}
FlotsamFileAccess::FlotsamFileAccess(std::shared_ptr<FlotsamAccessEntries> entries, SetTime set)
    : entries_(std::move(entries)), set_time_(std::move(set)) {
    if (!set_time_)
        throw std::invalid_argument("Missing file access-time operation");
}
bool FlotsamFileAccess::check_update(const std::filesystem::path& path) const noexcept {
    try {
        set_time_(path);
        if (entries_)
            entries_->add(path, 900000);
        return true;
    } catch (const FlotsamFileError& e) {
        return e.kind != FlotsamFileFailure::MissingFile;
    } catch (...) {
        return true;
    }
}
void FlotsamFileAccess::touch(const std::filesystem::path& path) const noexcept {
    try {
        if (entries_ && !entries_->contains(path)) {
            set_time_(path);
            entries_->add(path, 900000);
        }
    } catch (...) {
    }
}
void FlotsamFileAccess::note(const std::filesystem::path& path, int ms) const {
    if (entries_)
        entries_->add(path, ms);
}
FlotsamFileAccess::SetTime
native_flotsam_access_time_setter(std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime) {
    if (!datetime)
        datetime = framework::shared_native_invariant_datetime_context();
    auto local_offset = datetime->callbacks().local_offset_no_throw;
    return [datetime, local_offset](const std::filesystem::path& path) {
        // The argument DateTime.Now is evaluated before File.SetLastAccessTime opens
        // the path. Its DateTimeOffset conversion uses historical Local/NoThrow rules.
        auto local = datetime->now();
        auto utc_ticks = local.ticks() - local_offset(local);

#ifdef _WIN32
        auto bits =
            static_cast<std::uint64_t>(datetime->to_file_time_utc(framework::AssetDateTime(utc_ticks, 1)));
        FILETIME now{static_cast<DWORD>(bits), static_cast<DWORD>(bits >> 32)};
        auto handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            auto code = GetLastError();
            auto kind = code == ERROR_FILE_NOT_FOUND   ? FlotsamFileFailure::MissingFile
                        : code == ERROR_PATH_NOT_FOUND ? FlotsamFileFailure::MissingDirectory
                        : code == ERROR_ACCESS_DENIED  ? FlotsamFileFailure::Unauthorized
                                                       : FlotsamFileFailure::Io;
            throw FlotsamFileError(kind, std::system_category().message(static_cast<int>(code)));
        }
        auto result = SetFileTime(handle, nullptr, &now, nullptr);
        auto code = GetLastError();
        CloseHandle(handle);
        if (!result)
            throw FlotsamFileError(FlotsamFileFailure::Io,
                                   std::system_category().message(static_cast<int>(code)));
#else
        auto delta = utc_ticks - 621355968000000000ll;
        auto seconds = delta / 10000000ll;
        auto fraction = delta % 10000000ll;
        if (fraction < 0) {
            --seconds;
            fraction += 10000000ll;
        }
        timespec times[2]{};
        times[0].tv_sec = static_cast<time_t>(seconds);
        times[0].tv_nsec = static_cast<long>(fraction * 100);
        times[1].tv_nsec = UTIME_OMIT;
        if (::utimensat(AT_FDCWD, path.c_str(), times, 0) < 0) {
            auto code = errno;
            std::error_code ec;
            auto parent = path.parent_path();
            bool missing_directory =
                code == ENOTDIR ||
                (code == ENOENT &&
                 !std::filesystem::is_directory(parent.empty() ? std::filesystem::path(".") : parent, ec));
            auto kind = missing_directory                 ? FlotsamFileFailure::MissingDirectory
                        : code == ENOENT                  ? FlotsamFileFailure::MissingFile
                        : code == EACCES || code == EPERM ? FlotsamFileFailure::Unauthorized
                                                          : FlotsamFileFailure::Io;
            throw FlotsamFileError(kind, std::generic_category().message(code));
        }
#endif
    };
}
void native_flotsam_set_access_time(const std::filesystem::path& path) {
    static auto setter = native_flotsam_access_time_setter();
    setter(path);
}
void attach_flotsam_file_access(const std::shared_ptr<FlotsamFileBackend>& backend,
                                const std::shared_ptr<FlotsamFileAccess>& access) {
    if (!backend || !access)
        throw std::invalid_argument("Missing Flotsam file access dependency");
    backend->note_access = [access](auto& path, int ms) { access->note(path, ms); };
    backend->touch_access = [access](auto& path) { access->touch(path); };
}
}
