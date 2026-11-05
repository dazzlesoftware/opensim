#include "opensim/framework/file_stream_policy.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/time_zone.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <bit>
#include <cstdlib>
using namespace opensim::framework;
namespace {
RuntimeEnvironmentText text(std::u16string value) {
    return std::make_shared<const std::u16string>(std::move(value));
}
RuntimeEnvironmentText dec(const std::string& x) {
    if (x == "-")
        return {};
    std::u16string v;
    if (x != ".")
        for (std::size_t i = 0; i < x.size(); i += 4)
            v += static_cast<char16_t>(std::stoul(x.substr(i, 2), nullptr, 16) |
                                       (std::stoul(x.substr(i + 2, 2), nullptr, 16) << 8));
    return text(std::move(v));
}
std::string hex(std::u16string_view x) {
    if (x.empty())
        return ".";
    std::string out;
    for (auto c : x)
        for (unsigned char b : {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8)}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out;
}
std::vector<std::string> split(std::string_view s, char separator) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        auto end = s.find(separator, at);
        out.emplace_back(s.substr(at, end == s.npos ? s.npos : end - at));
        if (end == s.npos)
            return out;
        at = end + 1;
    }
}
bool boolean(RuntimeEnvironmentText value) {
    auto name = text(u"OPENSIM_POLICY_BOOLEAN");
    runtime_environment_set(name, std::move(value));
    return file_stream_boolean_config_value(runtime_environment_get(name));
}
std::string run(std::string_view commands) {
    std::string output;
    std::optional<NiniFileStreamProvider> factory;
    std::unique_ptr<NativeInvariantDateTimeContext> context;
    for (auto& command : split(commands, ',')) {
        auto a = split(command, ':');
        std::string result = a[0];
        try {
            if (a[0] == "env")
                runtime_environment_set(dec(a.at(1)), dec(a.at(2)));
            else if (a[0] == "raw") {
#ifndef _WIN32
                auto key = utf8(*dec(a.at(1)));
                auto value = dec(a.at(2));
                if (value)
                    ::setenv(key.c_str(), utf8(*value).c_str(), 1);
                else
                    ::unsetenv(key.c_str());
#else
                throw std::runtime_error("raw libc is Unix-only");
#endif
            } else if (a[0] == "factory")
                factory = native_nini_file_stream_provider();
            else if (a[0] == "context")
                context = std::make_unique<NativeInvariantDateTimeContext>();
            else if (a[0] == "open") {
                if (!factory)
                    factory = native_nini_file_stream_provider();
                auto stream = (*factory)(dec(a.at(1)), std::stoi(a.at(2)), a.at(2) == "3" ? 1 : 2,
                                         std::stoi(a.at(3)), 4096, 0);
                stream->close();
            } else if (a[0] == "p") {
#ifdef _WIN32
                result = "P:NA";
#else
                result = std::string("P:") + (file_stream_disable_locking() ? "1" : "0");
#endif
            } else if (a[0] == "sw")
                set_file_stream_disable_locking_switch(a.at(1) == "1");
            else if (a[0] == "clear") {
                if (context)
                    context->clear_cached_time_zone_data();
                time_zones().clear();
            } else if (a[0] == "offset") {
                if (!context)
                    context = std::make_unique<NativeInvariantDateTimeContext>();
                result = "O:" + std::to_string(context->callbacks().utc_offset(std::stoll(a.at(1))).ticks);
            } else if (a[0] == "dst")
                result =
                    std::string("D:") + (time_zones().local()->daylight_at(std::stoll(a.at(1))) ? "1" : "0");
            else if (a[0] == "find")
                result =
                    std::string("D:") +
                    (time_zones().find(utf8(*dec(a.at(1))))->daylight_at(std::stoll(a.at(2))) ? "1" : "0");
            else
                throw std::runtime_error("operation");
        } catch (const NiniStreamError& e) {
            result = "EX:" + e.source_type + ":" + std::to_string(std::bit_cast<std::uint32_t>(e.hresult)) +
                     ":" + hex(e.message) + ":" + (e.parameter ? hex(*e.parameter) : "-");
        } catch (const RuntimeEnvironmentError& e) {
            result = "EX:" + e.source_type + ":" + std::to_string(std::bit_cast<std::uint32_t>(e.hresult)) +
                     ":" + hex(e.message) + ":" + (e.parameter ? hex(*e.parameter) : "-");
        }
        if (!output.empty())
            output += '|';
        output += result;
    }
    return output;
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--override") {
            auto flag = text(u"DOTNET_SYSTEM_IO_DISABLEFILELOCKING");
            runtime_environment_set(flag, text(u"false"));
            bool disabled = std::string_view(argv[4]) == "1";
            if (std::string_view(argv[3]) == "nini") {
                auto provider = native_nini_file_stream_provider(disabled);
                auto stream = provider(dec(argv[2]), 3, 1, 1, 4096, 0);
                stream->close();
            } else {
                runtime_environment_set(text(u"TZ"), dec(argv[2]));
                NativeInvariantDateTimeContextOptions options;
                options.disable_file_locking = disabled;
                NativeInvariantDateTimeContext context(options);
                context.callbacks().utc_offset(638409600000000000ll);
            }
            runtime_environment_set(flag, text(u"true"));
            if (!file_stream_disable_locking())
                throw std::runtime_error("Explicit consumer override forced global policy initialization");
            std::cout << "PASS explicit consumer override\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--bool") {
            std::string row;
            while (std::getline(std::cin, row))
                std::cout << (boolean(dec(row)) ? '1' : '0') << '\n';
            return 0;
        }
        if (argc > 1) {
            std::cout << run(argv[1]) << '\n';
            return 0;
        }
        if (file_stream_disable_locking(true) != true || file_stream_disable_locking(false) != false)
            throw std::runtime_error("Explicit override");
        runtime_environment_set(text(u"DOTNET_SYSTEM_IO_DISABLEFILELOCKING"), text(u"true"));
        if (!file_stream_disable_locking())
            throw std::runtime_error("Override must not initialize latch");
        runtime_environment_set(text(u"DOTNET_SYSTEM_IO_DISABLEFILELOCKING"), text(u"false"));
        if (!file_stream_disable_locking())
            throw std::runtime_error("Process latch must persist");
        std::cout << "PASS shared FileStream policy\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
