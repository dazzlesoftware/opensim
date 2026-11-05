// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_file_access.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/filesystem.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <chrono>
#include <atomic>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#endif
using namespace opensim;
using namespace opensim::services;
namespace {
double now = 0;
std::string trace;
int point = 0, kind = 0;
void fail(int p) {
    if (p != point || !kind)
        return;
    auto f = kind == 1   ? FlotsamFileFailure::MissingFile
             : kind == 2 ? FlotsamFileFailure::MissingDirectory
             : kind == 3 ? FlotsamFileFailure::Unauthorized
             : kind == 4 ? FlotsamFileFailure::Io
                         : FlotsamFileFailure::Other;
    throw FlotsamFileError(f, "test");
}
struct Entries : FlotsamAccessEntries {
    std::shared_ptr<FlotsamAccessEntries> values = flotsam_access_entries([] { return now; }, false);
    bool contains(const std::filesystem::path& p) override {
        trace += "C";
        fail(1);
        return values->contains(p);
    }
    void add(const std::filesystem::path& p, int ms) override {
        trace += "A" + std::to_string(ms) + ",";
        fail(3);
        values->add(p, ms);
    }
    void pump() override {
        values->pump();
    }
};
void probe() {
    std::shared_ptr<Entries> entries = std::make_shared<Entries>();
    auto make = [&] {
        return std::make_shared<FlotsamFileAccess>(entries, [](const auto&) {
            trace += "S";
            fail(2);
        });
    };
    auto access = make();
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op, key, result = "OK";
        in >> op;
        trace.clear();
        try {
            if (op == "init") {
                int enabled;
                in >> enabled >> now;
                point = kind = 0;
                entries = enabled ? std::make_shared<Entries>() : nullptr;
                access = make();
            } else if (op == "failure")
                in >> point >> kind;
            else if (op == "touch") {
                in >> key;
                access->touch(key);
            } else if (op == "check") {
                in >> key;
                result = access->check_update(key) ? "1" : "0";
            } else if (op == "note") {
                int ms;
                in >> key >> ms;
                access->note(key, ms);
            } else if (op == "jump" || op == "advance") {
                double amount;
                in >> amount;
                now += amount;
                if (op == "advance" && entries)
                    entries->pump();
            } else if (op == "pump" && entries)
                entries->pump();
        } catch (...) {
            result = "EX";
        }
        std::string bits = "---";
        if (entries) {
            bits.clear();
            for (int i = 0; i < 3; ++i)
                bits += entries->values->contains(std::to_string(i)) ? '1' : '0';
        }
        std::cout << result << '|' << trace << '|' << bits << '\n';
    }
}
void set_old(const std::filesystem::path& path) {
#ifdef _WIN32
    {
        auto h = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             0, nullptr);
        FILETIME old{0, 30000000};
        if (h == INVALID_HANDLE_VALUE || !SetFileTime(h, nullptr, &old, nullptr))
            throw std::runtime_error("Set old fixture access time");
        CloseHandle(h);
    }
#else
    {
        timespec old[2]{};
        old[0].tv_sec = 1000000000;
        old[1].tv_nsec = UTIME_OMIT;
        if (::utimensat(AT_FDCWD, path.c_str(), old, 0) < 0)
            throw std::runtime_error("Set old fixture access time");
    }
#endif
}
std::uint64_t access_stamp(const std::filesystem::path& path) {
#ifdef _WIN32
    auto h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                         nullptr);
    FILETIME time{};
    if (h == INVALID_HANDLE_VALUE || !GetFileTime(h, nullptr, &time, nullptr))
        throw std::runtime_error("Get queued file atime");
    CloseHandle(h);
    return (std::uint64_t(time.dwHighDateTime) << 32) | time.dwLowDateTime;
#else
    struct stat status;
    if (::stat(path.c_str(), &status) < 0)
        throw std::runtime_error("Get queued file atime");
    return static_cast<std::uint64_t>(status.st_atime);
#endif
}
void actual() {
    auto root =
        std::filesystem::temp_directory_path() / ("opensim-access-" + framework::Uuid::random().str());
    std::filesystem::create_directory(root);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove_all(p, e);
        }
    } cleanup{root};
    auto path = root / "asset";
    {
        std::ofstream file(path);
        file << "asset";
    }
    set_old(path);
    auto modified = std::filesystem::last_write_time(path);
    auto entries = flotsam_access_entries();
    auto access = std::make_shared<FlotsamFileAccess>(entries, native_flotsam_set_access_time);
    if (!access->check_update(path) || !entries->contains(path) ||
        std::filesystem::last_write_time(path) != modified)
        throw std::runtime_error("Actual access time operation/mtime preservation");
#ifdef _WIN32
    auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              0, nullptr);
    FILETIME at{};
    if (handle == INVALID_HANDLE_VALUE || !GetFileTime(handle, nullptr, &at, nullptr))
        throw std::runtime_error("Get actual access time");
    CloseHandle(handle);
    ULARGE_INTEGER actual{}, current{};
    actual.LowPart = at.dwLowDateTime;
    actual.HighPart = at.dwHighDateTime;
    auto context = framework::shared_native_invariant_datetime_context();
    current.QuadPart = static_cast<std::uint64_t>(context->to_file_time_utc(context->utc_now()));
    if (actual.QuadPart > current.QuadPart || current.QuadPart - actual.QuadPart > 100000000ull)
        throw std::runtime_error("Actual atime outside current wall clock");
#else
    struct stat status;
    if (::stat(path.c_str(), &status) < 0)
        throw std::runtime_error("stat actual access time");
    auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    if (status.st_atime > seconds || seconds - status.st_atime > 10)
        throw std::runtime_error("Actual atime outside current wall clock");
#endif
    if (access->check_update(root / "missing"))
        throw std::runtime_error("Missing file must return false");
    auto backend = std::make_shared<FlotsamFileBackend>();
    attach_flotsam_file_access(backend, access);
    backend->note_access(root / "noted", 900000);
    if (!entries->contains(root / "noted"))
        throw std::runtime_error("Write tracker binding");
    backend->touch_access(path);
    FlotsamFileAccess no_tracker({}, native_flotsam_set_access_time);
    no_tracker.touch(root / "missing");
    if (!no_tracker.check_update(path))
        throw std::runtime_error("Null tracker check still touches file");
    std::atomic<std::int64_t> clock{0};
    auto timed = flotsam_access_entries([&] { return clock.load(); }, false);
    auto file_access = std::make_shared<FlotsamFileAccess>(timed, native_flotsam_set_access_time);
    auto jobs = std::make_shared<framework::ObjectJobEngine>(
        [](std::any job) { std::any_cast<std::function<void()>>(job)(); }, "FlotsamAccessIntegration", 1000,
        1);
    struct StopJobs {
        std::shared_ptr<framework::ObjectJobEngine> q;
        ~StopJobs() {
            q->dispose();
            while (q->active_threads())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } stop{jobs};
    auto io = native_flotsam_file_backend(jobs);
    attach_flotsam_file_access(io, file_access);
    FlotsamFileStore store({root / "cache", 1, 3, true}, io, std::make_shared<FlotsamFileState>());
    auto asset = std::make_shared<framework::AssetBase>(framework::Uuid::random(), "Access integration",
                                                        std::int8_t(7), "creator");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(std::initializer_list<std::uint8_t>{1, 2, 3});
    auto id = asset->id();
    auto cached = store.file_name(id);
    auto write = [&] {
        store.cache(id, asset);
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (store.writing(id) && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (store.writing(id))
            throw std::runtime_error("Access integration queue timeout");
    };
    write();
    if (!timed->contains(cached) || !store.check(id))
        throw std::runtime_error("Queued write did not note final cache path");
    set_old(cached);
    auto old = access_stamp(cached);
    auto write_time = std::filesystem::last_write_time(cached);
    write();
    if (access_stamp(cached) != old)
        throw std::runtime_error("Keep-existing write bypassed access throttle");
    clock = 900000;
    timed->pump();
    write();
    if (access_stamp(cached) != old)
        throw std::runtime_error("Strict expiry boundary lost");
    clock = 1200000;
    timed->pump();
    write();
    if (access_stamp(cached) == old || std::filesystem::last_write_time(cached) != write_time ||
        !timed->contains(cached))
        throw std::runtime_error("Expired access throttle did not touch and re-track real queued file");
    // Controlled source timers are checked against the C# class by the parity
    // test; this verifies the real worker can shut down from a pending timer.
}
}
int sample_clock(const std::filesystem::path& path) {
    auto context = framework::shared_native_invariant_datetime_context();
    auto modified = std::filesystem::last_write_time(path);
    auto before = context->utc_now().ticks();
    native_flotsam_set_access_time(path);
    auto after = context->utc_now().ticks();
    std::int64_t actual = 0;
    bool quantum = true;
#ifdef _WIN32
    auto handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              0, nullptr);
    FILETIME at{};
    if (handle == INVALID_HANDLE_VALUE || !GetFileTime(handle, nullptr, &at, nullptr))
        throw std::runtime_error("Clock fixture read failed");
    CloseHandle(handle);
    actual = context
                 ->from_file_time_utc(
                     static_cast<std::int64_t>((std::uint64_t(at.dwHighDateTime) << 32) | at.dwLowDateTime))
                 .ticks();
#else
    struct stat status{};
    if (::stat(path.c_str(), &status) < 0)
        throw std::runtime_error("Clock fixture stat failed");
#ifdef __APPLE__
    auto stamp = status.st_atimespec;
#else
    auto stamp = status.st_atim;
#endif
    actual =
        621355968000000000ll + static_cast<std::int64_t>(stamp.tv_sec) * 10000000ll + stamp.tv_nsec / 100;
    quantum = stamp.tv_nsec % 100 == 0;
#endif
    std::cout << before << '|' << actual << '|' << after << '|'
              << (modified == std::filesystem::last_write_time(path)) << '|' << quantum << '\n';
    return 0;
}
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--clock")
            return sample_clock(framework::utf8_path(argv[2]));
        if (argc > 1 && std::string_view(argv[1]) == "--probe")
            probe();
        else if (argc == 5 && std::string_view(argv[1]) == "--real") {
            FlotsamFileAccess access(std::string_view(argv[4]) == "1" ? flotsam_access_entries() : nullptr,
                                     native_flotsam_set_access_time);
            std::string encoded = argv[2], decoded;
            for (std::size_t i = 0; i < encoded.size(); i += 2)
                decoded += static_cast<char>(std::stoi(encoded.substr(i, 2), nullptr, 16));
            auto path = framework::utf8_path(decoded);
            if (std::string_view(argv[3]) == "check")
                std::cout << (access.check_update(path) ? "1" : "0") << '\n';
            else {
                access.touch(path);
                std::cout << "OK\n";
            }
        } else {
            actual();
            std::cout << "PASS: native access times preserve last-write time and bind file-store hooks\n";
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
