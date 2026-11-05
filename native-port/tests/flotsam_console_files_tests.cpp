// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_console_files.hpp"
#include "opensim/framework/text.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
using namespace opensim;
using namespace services;
namespace {
std::string hex(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::optional<std::string> decode(const std::string& text) {
    if (text == "~")
        return {};
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out += static_cast<char>(std::stoi(text.substr(i, 2), nullptr, 16));
    return out;
}
std::string error(const FlotsamConsoleFilesError& e) {
    std::ostringstream hr;
    hr << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << e.hresult();
    return std::string("EX|") + e.source_type() + "|" + hr.str() + "|" +
           (e.parameter ? hex(*e.parameter) : "~") + "|" + hex(e.message);
}
std::string run(const std::string& line) {
    std::istringstream in(line);
    std::string op, input;
    in >> op >> input;
    auto path = decode(input);
#ifdef _WIN32
    HANDLE blocker = INVALID_HANDLE_VALUE;
#endif
    try {
#ifdef _WIN32
        if (op == "enum-lock" || op == "time-lock") {
            auto name = framework::utf16_replacing(path.value_or(""));
            blocker = CreateFileW(reinterpret_cast<const wchar_t*>(name.c_str()), 0, 0, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (blocker == INVALID_HANDLE_VALUE)
                throw std::runtime_error("Test lock failed");
        }
#endif
        std::string result;
        if (op == "enum" || op == "enum-lock") {
            result = "OK|";
            auto paths = native_flotsam_status_files(path);
            for (std::size_t i = 0; i < paths.size(); ++i) {
                if (i)
                    result += ';';
                result += hex(paths[i].value());
            }
        } else {
            auto date = native_flotsam_last_write(path);
            result = "OK|" + std::to_string(date.ticks()) + "|" + std::to_string(date.kind()) + "|" +
                     std::to_string(date.serialized_data());
        }
#ifdef _WIN32
        if (blocker != INVALID_HANDLE_VALUE)
            CloseHandle(blocker);
#endif
        return result;
    } catch (const FlotsamConsoleFilesError& e) {
#ifdef _WIN32
        if (blocker != INVALID_HANDLE_VALUE)
            CloseHandle(blocker);
#endif
        return error(e);
    } catch (const std::exception& e) {
#ifdef _WIN32
        if (blocker != INVALID_HANDLE_VALUE)
            CloseHandle(blocker);
#endif
        return std::string("OTHER|") + hex(e.what());
    }
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << run(line) << '\n';
            return 0;
        }
        auto parent = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
        auto root = parent / ("opensim-console-files-" +
                              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        {
            std::ofstream file(root / "one.fac");
            file << "one";
        }
        {
            std::ofstream file(root / "two.FAC");
            file << "two";
        }
        std::filesystem::create_directory(root / "directory.fac");
        auto utf = root.u8string();
        std::string path(reinterpret_cast<const char*>(utf.data()), utf.size());
        auto files = native_flotsam_status_files(path);
#if defined(_WIN32) || defined(__APPLE__)
        constexpr std::size_t expected = 2;
#else
        constexpr std::size_t expected = 1;
#endif
        if (files.size() != expected || native_flotsam_last_write(path).kind() != 2)
            throw std::runtime_error("Native status file filtering/date kind");
        auto verified = std::filesystem::weakly_canonical(root);
        if (verified.parent_path() != parent ||
            !verified.filename().string().starts_with("opensim-console-files-"))
            throw std::runtime_error("Unexpected cleanup target");
        std::filesystem::remove_all(verified);
        std::cout << "PASS: native status enumeration and local timestamps\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
