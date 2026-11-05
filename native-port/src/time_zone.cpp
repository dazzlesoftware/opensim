// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Lookup/read control flow derived from dotnet/runtime v8.0.31.
// .NET Foundation and Contributors, MIT: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/time_zone.hpp"
#include "invariant_datetime_context_platform.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/runtime_environment.hpp"
#include "opensim/framework/file_stream_policy.hpp"
#include "opensim/framework/full_path.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace opensim::framework {
NativeTimeZoneError::NativeTimeZoneError(std::string t, std::uint32_t h, std::u16string m,
                                         std::optional<std::u16string> p,
                                         std::shared_ptr<const NativeTimeZoneError> i)
    : std::runtime_error(utf8(m)), source_type(std::move(t)), hresult(h), message(std::move(m)),
      parameter(std::move(p)), inner(std::move(i)) {
}
namespace {
std::string lower(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return value;
}
// ICU is used only for identifier conversion, never as a substitute for OS rule data.
struct Icu {
    using IanaToWindows = std::int32_t (*)(const char16_t*, std::int32_t, char16_t*, std::int32_t,
                                           std::int32_t*);
    using WindowsToIana = std::int32_t (*)(const char16_t*, std::int32_t, const char*, char16_t*,
                                           std::int32_t, std::int32_t*);
    IanaToWindows to_windows = nullptr;
    WindowsToIana to_iana = nullptr;
#ifdef _WIN32
    HMODULE module = nullptr;
    void* symbol(const std::string& name) {
        return reinterpret_cast<void*>(GetProcAddress(module, name.c_str()));
    }
#else
    void* module = nullptr;
    void* symbol(const std::string& name) {
        return dlsym(module, name.c_str());
    }
#endif
    Icu() {
#ifdef _WIN32
        module = LoadLibraryExW(L"icu.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
#elif defined(__APPLE__)
        module = dlopen("/usr/lib/libicucore.A.dylib", RTLD_LAZY | RTLD_LOCAL);
#else
        module = dlopen("libicui18n.so", RTLD_LAZY | RTLD_LOCAL);
        for (int v = 120; !module && v >= 40; --v) {
            auto name = "libicui18n.so." + std::to_string(v);
            module = dlopen(name.c_str(), RTLD_LAZY | RTLD_LOCAL);
        }
#endif
        if (!module)
            return;
        std::string suffix;
        if (!symbol("ucal_getWindowsTimeZoneID")) {
            for (int v = 120; v >= 40; --v)
                if (symbol("ucal_getWindowsTimeZoneID_" + std::to_string(v))) {
                    suffix = "_" + std::to_string(v);
                    break;
                }
        }
        to_windows = reinterpret_cast<IanaToWindows>(symbol("ucal_getWindowsTimeZoneID" + suffix));
        to_iana = reinterpret_cast<WindowsToIana>(symbol("ucal_getTimeZoneIDForWindowsID" + suffix));
    }
    ~Icu() {
#ifdef _WIN32
        if (module)
            FreeLibrary(module);
#else
        if (module)
            dlclose(module);
#endif
    }
    std::optional<std::u16string> convert(std::u16string_view id, bool windows) {
        if (id.find_first_of(u"\\\n\r") != std::u16string_view::npos)
            return {};
        auto input = id;
        std::array<char16_t, 100> buffer{};
        std::int32_t error = 0, n = 0;
        if (windows) {
            if (!to_windows)
                return {};
            n = to_windows(input.data(), static_cast<std::int32_t>(input.size()), buffer.data(),
                           static_cast<std::int32_t>(buffer.size()), &error);
        } else {
            if (!to_iana)
                return {};
            n = to_iana(input.data(), static_cast<std::int32_t>(input.size()), nullptr, buffer.data(),
                        static_cast<std::int32_t>(buffer.size()), &error);
        }
        if (error > 0 || n <= 0 || n > static_cast<int>(buffer.size()))
            return {};
        return std::u16string(buffer.data(), n);
    }
};
[[maybe_unused]] Icu& icu() {
    static Icu value;
    return value;
}
#ifdef _WIN32
struct MissingNativeZone : std::exception {};
std::shared_ptr<const InvariantTimeZoneRules> windows_named(std::u16string_view id) {
    auto model =
        invariant_datetime_platform::windows_registry_zone_lookup(std::wstring(id.begin(), id.end()));
    if (!model)
        return {};
    return std::make_shared<InvariantTimeZoneRules>(model->base_offset, std::move(model->rules));
}
#else
[[noreturn]] void zone_io_error(int code, const std::u16string& path) {
    if (code == ENOENT) {
        auto trimmed = std::filesystem::path(utf8(path));
        if (trimmed.filename().empty())
            trimmed = trimmed.parent_path();
        auto parent = trimmed.parent_path();
        struct stat status{};
        int result;
        do {
            result = ::stat(parent.c_str(), &status);
        } while (result < 0 && errno == EINTR);
        if (result == 0 && S_ISDIR(status.st_mode)) {
            NativeTimeZoneError error("System.IO.FileNotFoundException", 0x80070002u,
                                      u"Could not find file '" + path + u"'.");
            error.file_name = path;
            throw error;
        }
    }
    if (code == ENOENT || code == ENOTDIR)
        throw NativeTimeZoneError("System.IO.DirectoryNotFoundException", 0x80070003u,
                                  u"Could not find a part of the path '" + path + u"'.");
    if (code == EACCES || code == EPERM || code == EBADF)
        throw NativeTimeZoneError(
            "System.UnauthorizedAccessException", 0x80070005u,
            u"Access to the path '" + path + u"' is denied.", {},
            std::make_shared<NativeTimeZoneError>("System.IO.IOException", static_cast<std::uint32_t>(code),
                                                  utf16_replacing(std::generic_category().message(code))));
    if (code == ECANCELED)
        throw NativeTimeZoneError("System.OperationCanceledException", 0x8013153bu,
                                  u"The operation was canceled.");
    if (code == EFBIG)
        throw NativeTimeZoneError(
            "System.ArgumentOutOfRangeException", 0x80131502u,
            u"Specified file length was too large for the file system. (Parameter 'value')", u"value");
    if (code == EEXIST)
        throw NativeTimeZoneError("System.IO.IOException", static_cast<std::uint32_t>(code),
                                  u"The file '" + path + u"' already exists.");
    if (code == ENAMETOOLONG)
        throw NativeTimeZoneError("System.IO.PathTooLongException", 0x800700ceu,
                                  u"The path '" + path +
                                      u"' is too long, or a component of the specified path is too long.");
    if (code == EWOULDBLOCK)
        throw NativeTimeZoneError("System.IO.IOException", static_cast<std::uint32_t>(code),
                                  u"The process cannot access the file '" + path +
                                      u"' because it is being used by another process.");
    throw NativeTimeZoneError("System.IO.IOException", static_cast<std::uint32_t>(code),
                              utf16_replacing(std::generic_category().message(code)) + u" : '" + path + u"'");
}
struct TzFile {

    std::shared_ptr<const InvariantTimeZoneRules> rules;
    bool at(std::int64_t utc_ticks) const {
        return rules->utc_is_daylight(utc_ticks);
    }
    static std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path,
                                                std::optional<bool> override_locking = {}) {
        struct File {
            int fd = -1;
            bool locked = false;
            ~File() {
                if (fd >= 0) {
                    if (locked) {
                        int result;
                        do {
                            result = ::flock(fd, LOCK_UN);
                        } while (result < 0 && errno == EINTR);
                    }
                    ::close(fd);
                }
            }
        } file;
        auto full = get_full_path(path.native());
        auto raw_path = utf16_replacing(full);
        do {
            file.fd = ::open(full.c_str(), O_RDONLY | O_CLOEXEC);
        } while (file.fd < 0 && errno == EINTR);
        if (file.fd < 0)
            zone_io_error(errno, raw_path);
        auto status = [&]() {
            struct stat st{};
            int result;
            do {
                result = ::fstat(file.fd, &st);
            } while (result < 0 && errno == EINTR);
            if (result < 0)
                zone_io_error(errno, raw_path);
            return st;
        };
        auto initial = status();
        if (S_ISDIR(initial.st_mode))
            zone_io_error(EACCES, raw_path);
        if (!file_stream_disable_locking(override_locking)) {
            int result;
            do {
                result = ::flock(file.fd, LOCK_SH | LOCK_NB);
            } while (result < 0 && errno == EINTR);
            file.locked = result == 0;
            if (result < 0 && errno == EWOULDBLOCK)
                zone_io_error(errno, raw_path);
        }
        if (!S_ISREG(initial.st_mode)) {
            off_t result;
            do {
                result = ::lseek(file.fd, 0, SEEK_CUR);
            } while (result < 0 && errno == EINTR);
            if (result < 0)
                throw NativeTimeZoneError("System.IO.IOException", 0x80131620u,
                                          u"Unsupported unseekable file.");
        }
        // Preserve the source's repeated fs.Length evaluation and its short circuit/error order.
        auto length = [&]() { return status().st_size; };
        if (length() == 0 || length() > 20 * 1024 * 1024)
            throw NativeTimeZoneError(
                "System.IO.IOException", 0x80131620u,
                length() == 0 ? u"The read operation returned an invalid length."
                              : u"IO operation will not work. Most likely the file will become too long.");
        auto size = length();
        if (size < 0)
            throw NativeTimeZoneError("System.OverflowException", 0x80131516u,
                                      u"Arithmetic operation resulted in an overflow.");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        std::size_t index = 0;
        bool random_access = true;
        std::array<std::uint8_t, 4096> buffer;
        while (index < bytes.size()) {
            auto remaining = bytes.size() - index;
            bool buffered = remaining < buffer.size();
            auto target = buffered ? buffer.data() : bytes.data() + index;
            auto requested = buffered ? buffer.size() : remaining;
            ssize_t count;
            do {
                count = random_access ? ::pread(file.fd, target, requested, static_cast<off_t>(index))
                                      : ::read(file.fd, target, requested);
            } while (count < 0 && errno == EINTR);
            if (count < 0 && random_access && (errno == ENXIO || errno == ESPIPE)) {
                random_access = false;
                do {
                    count = ::read(file.fd, target, requested);
                } while (count < 0 && errno == EINTR);
            }
            if (count < 0)
                zone_io_error(errno, raw_path);
            if (count == 0)
                throw NativeTimeZoneError("System.IO.EndOfStreamException", 0x80070026u,
                                          u"Unable to read beyond the end of the stream.");
            auto used = std::min<std::size_t>(static_cast<std::size_t>(count), remaining);
            if (buffered)
                std::copy_n(buffer.data(), used, bytes.data() + index);
            index += used;
        }
        return bytes; // SafeFileHandle disposal precedes TimeZoneInfo construction/rule parsing.
    }
    static std::shared_ptr<TzFile> parse(const std::vector<std::uint8_t>& bytes,
                                         const std::string& requested_id) {
        invariant_datetime_platform::ZoneModel model;
        try {
            model = invariant_datetime_platform::unix_tzif_zone(bytes, requested_id, false, []() {
                return invariant_datetime_platform::native_utc_now().ticks();
            });
        } catch (const std::out_of_range&) {
            throw NativeTimeZoneError("System.IndexOutOfRangeException", 0x80131508u,
                                      u"Index was outside the bounds of the array.");
        } catch (const std::overflow_error&) {
            throw NativeTimeZoneError("System.OverflowException", 0x80131516u,
                                      u"Arithmetic operation resulted in an overflow.");
        } catch (const std::runtime_error&) {
            try {
                model = invariant_datetime_platform::unix_tzif_zone(bytes, requested_id, true, []() {
                    return invariant_datetime_platform::native_utc_now().ticks();
                });
            } catch (const std::out_of_range&) {
                throw NativeTimeZoneError("System.IndexOutOfRangeException", 0x80131508u,
                                          u"Index was outside the bounds of the array.");
            } catch (const std::overflow_error&) {
                throw NativeTimeZoneError("System.OverflowException", 0x80131516u,
                                          u"Arithmetic operation resulted in an overflow.");
            } catch (const std::runtime_error&) {
                throw std::invalid_argument("Invalid timezone data");
            }
        }
        auto result = std::make_shared<TzFile>();
        result->rules = std::make_shared<InvariantTimeZoneRules>(model.base_offset, std::move(model.rules));
        return result;
    }
};
std::filesystem::path zone_directory() {
    auto value = runtime_environment_get(std::make_shared<const std::u16string>(u"TZDIR"));
    auto path = value ? utf8(*value) : std::string("/usr/share/zoneinfo/");
    if (!path.ends_with('/'))
        path += '/';
    return std::filesystem::path(path);
}
std::shared_ptr<TzFile> named_file(const std::u16string& id, std::optional<bool> override_locking = {}) {
    bool invalid = id.starts_with(u'/');
    for (auto c : id)
        if (!((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'/' ||
              c == u'-' || c == u'_' || c == u'+'))
            invalid = true;
    if (invalid)
        throw NativeTimeZoneError("System.TimeZoneNotFoundException", 0x80131500u,
                                  u"The time zone ID '" + id + u"' is invalid.");
    auto path = zone_directory() / std::filesystem::path(utf8(id));
    auto invalid_message = u"The time zone ID '" + id +
                           u"' was found on the local computer, but the file at '" +
                           utf16_replacing(path.native()) + u"' was corrupt.";
    std::vector<std::uint8_t> bytes;
    try {
        bytes = TzFile::read_bytes(path, override_locking);
    } catch (const NativeTimeZoneError& e) {
        if (!e.source_type.starts_with("System.IO.") && e.source_type != "System.UnauthorizedAccessException")
            throw;
        if (e.source_type == "System.UnauthorizedAccessException" ||
            e.source_type == "System.IO.FileNotFoundException" ||
            e.source_type == "System.IO.DirectoryNotFoundException")
            throw;
        throw NativeTimeZoneError("System.InvalidTimeZoneException", 0x80131500u, invalid_message, {},
                                  std::make_shared<NativeTimeZoneError>(e));
    } catch (const std::bad_alloc&) {
        throw NativeTimeZoneError("System.InvalidTimeZoneException", 0x80131500u, invalid_message, {},
                                  std::make_shared<NativeTimeZoneError>(
                                      "System.OutOfMemoryException", 0x8007000eu,
                                      u"Exception of type 'System.OutOfMemoryException' was thrown."));
    }
    try {
        return TzFile::parse(bytes, utf8(id));
    } catch (const std::invalid_argument&) {
        throw NativeTimeZoneError("System.InvalidTimeZoneException", 0x80131500u, invalid_message);
    } catch (const std::bad_alloc&) {
        throw NativeTimeZoneError("System.OutOfMemoryException", 0x8007000eu,
                                  u"Exception of type 'System.OutOfMemoryException' was thrown.");
    }
}
#endif
}
TimeZoneText time_zone_utc_id_text() {
    static auto value = std::make_shared<const std::u16string>(u"UTC");
    return value;
}
TimeZone::TimeZone(std::string id, std::function<bool(std::int64_t)> daylight)
    : TimeZone(std::make_shared<const std::u16string>(utf16_replacing(id)), std::move(daylight)) {
}
TimeZone::TimeZone(TimeZoneText id, std::function<bool(std::int64_t)> daylight)
    : id_(utf8(*id)), daylight_(std::move(daylight)), raw_id_(std::move(id)) {
}
bool TimeZone::daylight_at(std::int64_t utc_seconds) const {
    if (utc_seconds < -62135596800ll || utc_seconds > 253402300799ll)
        throw std::out_of_range("Timezone date is outside DateTime range");
    return daylight_at_ticks(621355968000000000ll + utc_seconds * 10000000ll);
}
bool TimeZone::daylight_at_ticks(std::int64_t utc_ticks) const {
    if (utc_ticks < 0 || utc_ticks > 3155378975999999999ll)
        throw std::out_of_range("Timezone date is outside DateTime range");
    return daylight_(utc_ticks);
}
struct TimeZoneDatabase::State {
    std::shared_ptr<NativeInvariantDateTimeContext> owner;
    NativeTimeZoneLookupOptions options;
};
TimeZoneDatabase::TimeZoneDatabase() : TimeZoneDatabase(std::make_shared<NativeInvariantDateTimeContext>()) {
}
TimeZoneDatabase::TimeZoneDatabase(std::shared_ptr<NativeInvariantDateTimeContext> owner)
    : TimeZoneDatabase(std::move(owner), {}) {
}
TimeZoneDatabase::TimeZoneDatabase(std::shared_ptr<NativeInvariantDateTimeContext> owner,
                                   NativeTimeZoneLookupOptions options)
    : state_(std::make_unique<State>(State{std::move(owner), std::move(options)})) {
    if (!state_->owner)
        throw std::invalid_argument("Null timezone cache owner");
}
TimeZoneDatabase::~TimeZoneDatabase() = default;
std::shared_ptr<TimeZone> TimeZoneDatabase::find(const std::optional<std::string>& id) {
    return find_utf16(id ? std::optional(utf16_replacing(*id)) : std::nullopt);
}
std::shared_ptr<TimeZone> TimeZoneDatabase::find_utf16(const std::optional<std::u16string>& id) {
    return find_text(id ? std::make_shared<const std::u16string>(*id) : TimeZoneText{});
}
std::shared_ptr<TimeZone> TimeZoneDatabase::find_text(TimeZoneText id) {
    if (!id)
        throw NativeTimeZoneError("System.ArgumentNullException", 0x80004003u,
                                  u"Value cannot be null. (Parameter 'id')", u"id");
    auto missing = [&](std::shared_ptr<const NativeTimeZoneError> inner = {}) {
        return NativeTimeZoneError("System.TimeZoneNotFoundException", 0x80131500u,
                                   u"The time zone ID '" + *id + u"' was not found on the local computer.",
                                   {}, std::move(inner));
    };
    if (id->empty() || id->size() > 255 || id->find(u'\0') != id->npos)
        throw missing();
    auto requested = utf8(*id);
    auto key = lower(requested);
    if (key == "utc") {
        static const auto utc =
            std::shared_ptr<TimeZone>(new TimeZone(time_zone_utc_id_text(), [](auto) { return false; }));
        return utc;
    }
    auto owner = state_->owner;
    auto raw = *id;
    auto converter = state_->options.windows_to_iana;
    auto comparer = state_->options.ordinal_ignore_case_equal;
    auto lease = owner->legacy_named_cache_lease();
    return owner->cached_legacy_named_raw(
        raw,
        [owner, requested, key, raw, converter, id, comparer, lease] {
            std::function<bool(std::int64_t)> daylight;
#ifdef _WIN32
            std::shared_ptr<const NativeTimeZoneError> error;
            std::shared_ptr<const InvariantTimeZoneRules> rules;
            try {
                rules = windows_named(raw);
            } catch (const NativeTimeZoneError& e) {
                if (e.source_type == "System.Security.SecurityException")
                    throw;
                error = std::make_shared<NativeTimeZoneError>(e);
            }
            if (rules)
                daylight = [rules](auto seconds) { return rules->utc_is_daylight(seconds); };
            else {
                auto alternative = owner->legacy_iana_to_windows_raw(*id);
                if (alternative) {
                    try {
                        auto alternate_text = std::make_shared<const std::u16string>(*alternative);
                        auto alternate = owner->cached_legacy_named_raw(
                            *alternative,
                            [alternate_text] {
                                auto alternate_rules = windows_named(*alternate_text);
                                if (!alternate_rules)
                                    throw MissingNativeZone{};
                                return std::shared_ptr<TimeZone>(
                                    new TimeZone(alternate_text, [alternate_rules](auto seconds) {
                                        return alternate_rules->utc_is_daylight(seconds);
                                    }));
                            },
                            comparer, lease);
                        daylight = alternate->daylight_;
                        error.reset();
                    } catch (const MissingNativeZone&) {
                        error.reset();
                    } catch (const NativeTimeZoneError& e) {
                        if (e.source_type == "System.Security.SecurityException")
                            throw;
                        error = std::make_shared<NativeTimeZoneError>(e);
                    }
                }
                if (error)
                    throw *error;
                if (!daylight)
                    throw NativeTimeZoneError("System.TimeZoneNotFoundException", 0x80131500u,
                                              u"The time zone ID '" + raw +
                                                  u"' was not found on the local computer.");
            }

#else
            std::shared_ptr<TzFile> data;
            std::shared_ptr<const NativeTimeZoneError> error;
            try {
                data = named_file(raw, owner->legacy_file_locking_override());
            } catch (const NativeTimeZoneError& e) {
                if (e.source_type != "System.InvalidTimeZoneException" &&
                    e.source_type != "System.TimeZoneNotFoundException" &&
                    e.source_type != "System.UnauthorizedAccessException" &&
                    e.source_type != "System.IO.FileNotFoundException" &&
                    e.source_type != "System.IO.DirectoryNotFoundException")
                    throw;
                error = std::make_shared<NativeTimeZoneError>(e);
                auto alternative = owner->legacy_name_mapping_allowed()
                                       ? (converter ? converter(*id) : icu().convert(*id, false))
                                       : std::nullopt;
                if (alternative) {
                    try {
                        auto alternate_text = std::make_shared<const std::u16string>(*alternative);
                        auto alternate = owner->cached_legacy_named_raw(
                            *alternative,
                            [owner, alternate_text] {
                                auto alternate_data =
                                    named_file(*alternate_text, owner->legacy_file_locking_override());
                                return std::shared_ptr<TimeZone>(
                                    new TimeZone(alternate_text, [alternate_data](auto seconds) {
                                        return alternate_data->at(seconds);
                                    }));
                            },
                            comparer, lease);
                        daylight = alternate->daylight_;
                        error.reset();
                    } catch (const NativeTimeZoneError& a) {
                        if (a.source_type != "System.InvalidTimeZoneException" &&
                            a.source_type != "System.TimeZoneNotFoundException" &&
                            a.source_type != "System.UnauthorizedAccessException" &&
                            a.source_type != "System.IO.FileNotFoundException" &&
                            a.source_type != "System.IO.DirectoryNotFoundException")
                            throw;
                        error = std::make_shared<NativeTimeZoneError>(a);
                    }
                }
            }
            if (error) {
                if (error->source_type == "System.InvalidTimeZoneException")
                    throw *error;
                if (error->source_type == "System.UnauthorizedAccessException")
                    throw NativeTimeZoneError(
                        "System.Security.SecurityException", 0x8013150au,
                        u"The time zone ID '" + raw +
                            u"' was found on the local computer, but the application does not have permission to read the file.",
                        {}, error);
                throw NativeTimeZoneError(
                    "System.TimeZoneNotFoundException", 0x80131500u,
                    u"The time zone ID '" + raw + u"' was not found on the local computer.", {}, error);
            }
            if (data)
                daylight = [data](auto seconds) { return data->at(seconds); };
#endif
            return std::shared_ptr<TimeZone>(new TimeZone(id, std::move(daylight)));
        },
        comparer, lease);
}
std::shared_ptr<TimeZone> TimeZoneDatabase::local() {
    return state_->owner->cached_legacy_local([](std::shared_ptr<const InvariantTimeZoneRules> rules) {
        return std::shared_ptr<TimeZone>(
            new TimeZone("Local", [rules](std::int64_t seconds) { return rules->utc_is_daylight(seconds); }));
    });
}
void TimeZoneDatabase::clear() {
    state_->owner->clear_cached_time_zone_data();
}
TimeZoneDatabase& time_zones() {
    static TimeZoneDatabase value(shared_native_invariant_datetime_context());
    return value;
}
}
