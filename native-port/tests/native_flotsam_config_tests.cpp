// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/native_flotsam_config.hpp"
#include <bit>
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace services;
namespace {
std::string hex(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out.empty() ? "." : out;
}
std::string decode(const std::string& value) {
    if (value == ".")
        return {};
    std::string out;
    for (std::size_t i = 0; i < value.size(); i += 2)
        out.push_back(static_cast<char>(std::stoi(value.substr(i, 2), nullptr, 16)));
    return out;
}
std::string probe(const std::string& line) {
    std::istringstream in(line);
    std::string operation, text;
    int aliases, present;
    in >> operation >> aliases >> present >> text;
    auto ini = std::make_shared<server::IniConfig>();
    ini->set("Probe", "dummy", "1");
    if (present)
        ini->set("Probe", "x", decode(text));
    auto source = native_flotsam_config_source(ini, aliases != 0);
    auto section = source->section("Probe");
    try {
        if (operation == "s" || operation == "n") {
            auto result =
                section->text("x", operation == "s" ? std::optional<std::string>("fallback") : std::nullopt);
            return result ? "S:" + hex(*result) : "S:-";
        }
        if (operation == "b")
            return std::string("B:") + (section->boolean("x", true) ? "1" : "0");
        if (operation == "i")
            return "I:" + std::to_string(section->integer("x", -99));
        if (operation == "l")
            return "L:" + std::to_string(section->long_integer("x", INT64_MIN));
        if (operation == "r")
            return "R:" + std::to_string(std::bit_cast<std::uint64_t>(section->real("x", -0.0)));
    } catch (const FlotsamConfigValueError& error) {
        auto type = error.kind == FlotsamConfigValueError::Kind::Format     ? "FormatException"
                    : error.kind == FlotsamConfigValueError::Kind::Overflow ? "OverflowException"
                                                                            : "ArgumentException";
        return std::string("EX:") + type + ":" + std::to_string(error.hresult()) + ":-:" + hex(error.message);
    }
    throw std::runtime_error("Unexpected typed config operation");
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            for (std::string line; std::getline(std::cin, line);)
                std::cout << probe(line) << '\n';
            return 0;
        }
        if (probe("b 1 1 4f6e") != "B:1" || probe("i 1 1 2b32313437343833363437") != "I:2147483647" ||
            probe("l 1 1 2d39323233333732303336383534373735383038") != "L:-9223372036854775808")
            throw std::runtime_error("Native Nini typed values");
        auto config = std::make_shared<server::IniConfig>();
        config->set("Probe", "x", "5");
        auto source = native_flotsam_config_source(config);
        auto section = source->section("Probe");
        config->set("Probe", "x", "7");
        if (section->integer("x", 0) != 7 || source->section("missing"))
            throw std::runtime_error("Config adapter ownership and live values");
        bool rejected = false;
        try {
            native_flotsam_config_backend({}, {}, {}, {}, {});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Native config host dependencies must be required");
        auto backend = native_flotsam_config_backend(
            [](auto n) { return std::make_shared<int>(n); }, [](auto n) { return std::make_shared<int>(n); },
            [](const auto&) {}, [](const auto&) {}, [](const auto&) {});
        if (backend->hours_to_ticks(.5) != 18000000000ll || backend->total_hours(18000000000ll) != .5 ||
            !backend->full_path(std::string("assetcache")))
            throw std::runtime_error("Native path/TimeSpan provider binding");
        {
            double now = 100;
            std::vector<double> epochs;
            auto instance = std::make_shared<FlotsamInstanceConfig>();
            auto common = std::make_shared<FlotsamStaticConfig>();
            auto ini = std::make_shared<server::IniConfig>();
            ini->set("Modules", "AssetCaching", "FlotsamAssetCache");
            ini->set("AssetCache", "FileCacheEnabled", "Off");
            ini->set("AssetCache", "MemoryCacheEnabled", "On");
            ini->set("AssetCache", "MemoryCacheTimeout", ".0001");
            // This controlled source explicitly owns its On/Off aliases. File sources do
            // not acquire aliases from a separately merged Application argv source.
            ini->native_source()->alias()->add_alias(framework::nini_writer_text(u"On"), true);
            ini->native_source()->alias()->add_alias(framework::nini_writer_text(u"Off"), false);
            int registrations = 0;
            auto binding = native_flotsam_cache_containers(
                instance, common,
                [&] {
                    epochs.push_back(now);
                    return now;
                },
                false, [&](const auto&) { now += 100; }, [](const auto&) {},
                [&](const auto&) { ++registrations; });
            FlotsamCacheConfig initializer(instance, common, binding.backend);
            {
                std::lock_guard lock(*binding.gate);
                initializer.initialise(native_flotsam_config_source(ini));
            }
            auto negative = std::static_pointer_cast<FlotsamKeyEntries>(instance->negative_entries);
            auto memory = std::static_pointer_cast<FlotsamAssetEntries>(instance->memory_entries);
            if (epochs != std::vector<double>{100, 200} || negative != binding.entries->negative.load() ||
                memory != binding.entries->memory.load() || registrations != 5)
                throw std::runtime_error(
                    "Full initializer must create actual containers in source order at the getters' current clock");
            auto calls = epochs.size();
            FlotsamMemoryCache cache(binding.entries->live_options(), binding.entries);
            if (epochs.size() != calls)
                throw std::runtime_error("Adopted wrapper must not recreate initialized containers");
            auto asset = std::make_shared<framework::AssetBase>(framework::Uuid::random(), "retired",
                                                                std::int8_t(0), "");
            std::weak_ptr<framework::AssetBase> weak = asset;
            cache.cache(asset);
            asset.reset();
            now = 500;
            {
                std::lock_guard lock(*binding.gate);
                initializer.initialise(native_flotsam_config_source(ini));
            }
            if (weak.expired() || memory == binding.entries->memory.load() || memory->count() != 1)
                throw std::runtime_error(
                    "Reinitialization must retain armed old memory rather than Dispose it");
            now = 802;
            cache.pump();
            if (!weak.expired() || memory->count())
                throw std::runtime_error("Retired initialized container did not expire under host pumping");
            auto previous = instance->memory_entries;
            now = 5000;
            cache.clear();
            if (instance->memory_entries == previous ||
                instance->memory_entries != binding.entries->memory.load() ||
                instance->negative_entries != binding.entries->negative.load())
                throw std::runtime_error(
                    "Data clear must publish actual replacements back into initializer state");
        }
        std::cout << "PASS: native Nini typed configuration and real path/time-span binding\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
