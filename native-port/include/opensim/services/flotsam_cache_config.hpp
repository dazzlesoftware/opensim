// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
namespace opensim::services {
// Typed configuration is supplied by the host. Implementations must preserve
// their provider's null/default/conversion behavior rather than silently coerce.
class FlotsamConfigSection {
public:
    virtual ~FlotsamConfigSection() = default;
    virtual std::optional<std::string> text(const std::string&, const std::optional<std::string>&) = 0;
    virtual bool boolean(const std::string&, bool) = 0;
    virtual std::int32_t integer(const std::string&, std::int32_t) = 0;
    virtual std::int64_t long_integer(const std::string&, std::int64_t) = 0;
    virtual double real(const std::string&, double) = 0;
};
class FlotsamConfigSource {
public:
    virtual ~FlotsamConfigSource() = default;
    virtual std::shared_ptr<FlotsamConfigSection> section(const std::string&) = 0;
};
// Original fields are static and are consulted by old as well as new instances.
// Hosts serialize initialization and bind file/cache/cleaner adapters to this
// live state; copying its settings per instance would lose that source behavior.
struct FlotsamStaticConfig {
    std::int32_t tiers = 1, tier_length = 3, warn_at = 30000;
    bool update_access = false;
    std::shared_ptr<void> access_entries;
};
struct FlotsamInstanceConfig {
    bool enabled = false, file = true, memory = false, negative = true;
    std::optional<std::string> directory = std::string("assetcache"), loader, loader_arguments;
    double memory_seconds = .016;
    std::int32_t negative_milliseconds = 120, log_level = 0;
    std::uint64_t hit_rate_display = 100;
    std::int64_t expiration_ticks = 48ll * 3600 * 10000000, cleanup_ticks = 3600ll * 10000000;
    std::shared_ptr<void> negative_entries, memory_entries;
};
struct FlotsamConfigCommand {
    std::string category;
    bool shared;
    std::string command, usage, description;
};
struct FlotsamConfigBackend {
    std::function<std::optional<std::string>(const std::optional<std::string>&)> full_path;
    // The required provider implements TimeSpan.FromHours, including its faults.
    std::function<std::int64_t(double)> hours_to_ticks;
    std::function<double(std::int64_t)> total_hours;
    std::function<std::shared_ptr<void>(std::int32_t)> make_key_cache, make_memory_cache;
    std::function<void(const std::string&)> info, debug;
    // Registration binds the host's HandleConsoleCommand equivalent and owns the
    // handler lifetime; this core does not start tasks or enable file-cache hosting.
    std::function<void(const FlotsamConfigCommand&)> register_command;
};
class FlotsamCacheConfig {
public:
    FlotsamCacheConfig(std::shared_ptr<FlotsamInstanceConfig>, std::shared_ptr<FlotsamStaticConfig>,
                       std::shared_ptr<FlotsamConfigBackend>);
    void initialise(std::shared_ptr<FlotsamConfigSource>);
    static constexpr const char* name = "FlotsamAssetCache";

private:
    std::shared_ptr<FlotsamInstanceConfig> instance_;
    std::shared_ptr<FlotsamStaticConfig> shared_;
    std::shared_ptr<FlotsamConfigBackend> backend_;
};
}
