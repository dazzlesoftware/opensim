// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/flotsam_cache_config.hpp"
#include "opensim/server/config.hpp"
#include "opensim/services/flotsam_memory_cache.hpp"
#include <stdexcept>
namespace opensim::services {
class FlotsamConfigValueError : public std::runtime_error {
  public:
    enum class Kind { Format, Overflow, Alias };
    Kind kind;
    std::string message;
    std::u16string raw_message;
    std::optional<std::u16string> parameter;
    std::uint32_t hresult() const {
        return kind == Kind::Format ? 0x80131537u : kind == Kind::Overflow ? 0x80131516u : 0x80070057u;
    }
    FlotsamConfigValueError(Kind, const std::string&);
    FlotsamConfigValueError(Kind, std::u16string, std::optional<std::u16string> parameter = {});
};
// Production reads the actual retained Nini section, including its owning
// ConfigSource aliases. UTF16 source diagnostics remain in raw_message.
std::shared_ptr<FlotsamConfigSource> native_flotsam_config_source(std::shared_ptr<const server::IniConfig>);
// Explicit controlled-fixture recipe: an isolated Nini alias source with the
// optional Application argv aliases. Never changes the production source graph.
std::shared_ptr<FlotsamConfigSource> native_flotsam_config_source(std::shared_ptr<const server::IniConfig>,
                                                                  bool application_boolean_aliases);
// Hosts must provide real cache-container factories and registration ownership.
// This factory binds native path and TimeSpan operations, never dummy caches.
std::shared_ptr<FlotsamConfigBackend>
native_flotsam_config_backend(std::function<std::shared_ptr<void>(std::int32_t)> key_cache,
                              std::function<std::shared_ptr<void>(std::int32_t)> memory_cache,
                              std::function<void(const std::string&)> info,
                              std::function<void(const std::string&)> debug,
                              std::function<void(const FlotsamConfigCommand&)> register_command);
struct NativeFlotsamContainers {
    std::shared_ptr<FlotsamMemoryCache::Containers> entries;
    std::shared_ptr<FlotsamConfigBackend> backend;
    // Serialize initialization/config edits with this recursive gate; share it
    // across bindings that share static config. Wrapper
    // settings and replacement publication use the same gate, including reentry.
    std::shared_ptr<std::recursive_mutex> gate;
};
NativeFlotsamContainers
native_flotsam_cache_containers(std::shared_ptr<FlotsamInstanceConfig>, std::shared_ptr<FlotsamStaticConfig>,
                                FlotsamMemoryCache::Clock, bool background,
                                std::function<void(const std::string&)> info,
                                std::function<void(const std::string&)> debug,
                                std::function<void(const FlotsamConfigCommand&)> register_command,
                                std::shared_ptr<std::recursive_mutex> gate = {});
}
