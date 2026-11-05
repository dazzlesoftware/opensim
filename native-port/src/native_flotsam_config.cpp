// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/native_flotsam_config.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/full_path.hpp"
#include "opensim/framework/time_span.hpp"
#include "opensim/network/osd_value.hpp"
#include <bit>
#include <limits>
#include <type_traits>
#include <chrono>
#include <algorithm>
namespace opensim::services {
FlotsamConfigValueError::FlotsamConfigValueError(Kind kind, const std::string& text)
    : FlotsamConfigValueError(kind, framework::utf16_replacing(text)) {
}
FlotsamConfigValueError::FlotsamConfigValueError(Kind value, std::u16string text,
                                                 std::optional<std::u16string> param)
    : runtime_error(framework::utf8(text)), kind(value), message(framework::utf8(text)),
      raw_message(std::move(text)), parameter(std::move(param)) {
}
namespace {
framework::NiniWriterText key_text(const std::string& text) {
    return framework::nini_writer_text(framework::utf16_replacing(text));
}
template <class F> auto typed(F action) {
    try {
        return action();
    } catch (const framework::NiniConfigError& e) {
        using E = framework::NiniConfigError;
        using K = FlotsamConfigValueError::Kind;
        if (e.kind == E::Kind::Format)
            throw FlotsamConfigValueError(K::Format, e.message, e.parameter);
        if (e.kind == E::Kind::Overflow)
            throw FlotsamConfigValueError(K::Overflow, e.message, e.parameter);
        if (e.kind == E::Kind::Argument)
            throw FlotsamConfigValueError(K::Alias, e.message, e.parameter);
        throw;
    }
}
class Section final : public FlotsamConfigSection {
    std::shared_ptr<framework::NiniConfig> config_, fixture_boolean_;

  public:
    Section(std::shared_ptr<framework::NiniConfig> config, std::optional<bool> fixture_aliases)
        : config_(std::move(config)) {
        if (fixture_aliases) {
            server::NiniConfigView fixture;
            auto source = fixture.native_source();
            if (*fixture_aliases) {
                for (auto text : {u"On", u"Yes"})
                    source->alias()->add_alias(framework::nini_writer_text(text), true);
                for (auto text : {u"Off", u"No"})
                    source->alias()->add_alias(framework::nini_writer_text(text), false);
            }
            fixture_boolean_ = source->add_config(framework::nini_writer_text(u"Fixture"));
        }
    }
    std::optional<std::string> text(const std::string& key,
                                    const std::optional<std::string>& fallback) override {
        return typed([&]() -> std::optional<std::string> {
            auto value = config_->get(key_text(key));
            return value ? std::optional<std::string>(framework::utf8(*value)) : fallback;
        });
    }
    bool boolean(const std::string& key, bool fallback) override {
        return typed([&] {
            auto name = key_text(key);
            if (!fixture_boolean_)
                return config_->get_boolean(name, fallback);
            auto value = config_->get(name);
            if (!value)
                return fallback;
            fixture_boolean_->set_text(name, value);
            return fixture_boolean_->get_boolean(name, fallback);
        });
    }
    std::int32_t integer(const std::string& key, std::int32_t fallback) override {
        return typed([&] { return config_->get_int(key_text(key), fallback); });
    }
    std::int64_t long_integer(const std::string& key, std::int64_t fallback) override {
        return typed([&] { return config_->get_long(key_text(key), fallback); });
    }
    double real(const std::string& key, double fallback) override {
        return typed([&] { return config_->get_double(key_text(key), fallback); });
    }
};
class Source final : public FlotsamConfigSource {
    std::shared_ptr<framework::NiniConfigSourceBase> source_;
    std::optional<bool> fixture_aliases_;

  public:
    Source(const std::shared_ptr<const server::IniConfig>& source, std::optional<bool> aliases)
        : fixture_aliases_(aliases) {
        if (!source)
            throw std::invalid_argument("Null native INI configuration");
        source_ = source->native_source();
    }
    std::shared_ptr<FlotsamConfigSection> section(const std::string& name) override {
        auto config = source_->configs()->at(key_text(name));
        return config ? std::make_shared<Section>(std::move(config), fixture_aliases_) : nullptr;
    }
};
}
std::shared_ptr<FlotsamConfigSource>
native_flotsam_config_source(std::shared_ptr<const server::IniConfig> source) {
    return std::make_shared<Source>(source, std::nullopt);
}
std::shared_ptr<FlotsamConfigSource>
native_flotsam_config_source(std::shared_ptr<const server::IniConfig> source, bool aliases) {
    return std::make_shared<Source>(source, aliases);
}
std::shared_ptr<FlotsamConfigBackend>
native_flotsam_config_backend(std::function<std::shared_ptr<void>(std::int32_t)> keys,
                              std::function<std::shared_ptr<void>(std::int32_t)> memory,
                              std::function<void(const std::string&)> info,
                              std::function<void(const std::string&)> debug,
                              std::function<void(const FlotsamConfigCommand&)> registration) {
    if (!keys || !memory || !info || !debug || !registration)
        throw std::invalid_argument("Missing native Flotsam config host dependencies");
    auto backend = std::make_shared<FlotsamConfigBackend>();
    backend->full_path = [](const auto& path) {
        return std::optional<std::string>(framework::get_full_path(path));
    };
    backend->hours_to_ticks = framework::timespan_from_hours;
    backend->total_hours = framework::timespan_total_hours;
    backend->make_key_cache = std::move(keys);
    backend->make_memory_cache = std::move(memory);
    backend->info = std::move(info);
    backend->debug = std::move(debug);
    backend->register_command = std::move(registration);
    return backend;
}
NativeFlotsamContainers
native_flotsam_cache_containers(std::shared_ptr<FlotsamInstanceConfig> instance,
                                std::shared_ptr<FlotsamStaticConfig> common, FlotsamMemoryCache::Clock clock,
                                bool background, std::function<void(const std::string&)> info,
                                std::function<void(const std::string&)> debug,
                                std::function<void(const FlotsamConfigCommand&)> registration,
                                std::shared_ptr<std::recursive_mutex> gate) {
    if (!instance || !common)
        throw std::invalid_argument("Missing native Flotsam configuration state");
    if (!gate)
        gate = std::make_shared<std::recursive_mutex>();
    auto provider =
        std::make_shared<FlotsamMemoryCache::Clock>(clock ? std::move(clock) : FlotsamMemoryCache::Clock([] {
            return std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now().time_since_epoch())
                .count();
        }));
    auto time = [provider] { return (*provider)(); };
    auto entries = std::make_shared<FlotsamMemoryCache::Containers>();
    entries->make_key = [time, background](auto period) {
        return std::make_shared<FlotsamKeyEntries>(period, time, background);
    };
    entries->make_memory = [time, background](auto period) {
        return std::make_shared<FlotsamAssetEntries>(period, time, background);
    };
    auto retired_keys = std::make_shared<std::vector<std::shared_ptr<FlotsamKeyEntries>>>();
    auto retired_memory = std::make_shared<std::vector<std::shared_ptr<FlotsamAssetEntries>>>();
    entries->live_options = [instance, common, gate] {
        std::lock_guard lock(*gate);
        return FlotsamMemoryCache::Options{instance->memory,         instance->negative,
                                           instance->memory_seconds, instance->negative_milliseconds,
                                           instance->file,           common->update_access,
                                           instance->log_level};
    };
    entries->publish_key = [instance, gate, retired_keys, background](auto next) {
        std::lock_guard lock(*gate);
        if (!background && instance->negative_entries) {
            auto old = std::static_pointer_cast<FlotsamKeyEntries>(instance->negative_entries);
            if (old != next && old->next_due())
                retired_keys->push_back(old);
        }
        instance->negative_entries = std::move(next);
    };
    entries->publish_memory = [instance, gate, retired_memory, background](auto next) {
        std::lock_guard lock(*gate);
        if (!background && instance->memory_entries) {
            auto old = std::static_pointer_cast<FlotsamAssetEntries>(instance->memory_entries);
            if (old != next && old->next_due())
                retired_memory->push_back(old);
        }
        instance->memory_entries = std::move(next);
    };
    entries->pump_retired = [retired_keys, retired_memory, common, gate] {
        std::vector<std::shared_ptr<FlotsamKeyEntries>> keys;
        std::vector<std::shared_ptr<FlotsamAssetEntries>> memory;
        {
            std::lock_guard lock(*gate);
            keys = *retired_keys;
            memory = *retired_memory;
            if (common->access_entries)
                keys.push_back(std::static_pointer_cast<FlotsamKeyEntries>(common->access_entries));
        }
        for (auto& value : keys)
            value->pump();
        for (auto& value : memory)
            value->pump();
        std::lock_guard lock(*gate);
        std::erase_if(*retired_keys, [](auto& value) { return !value->next_due(); });
        std::erase_if(*retired_memory, [](auto& value) { return !value->next_due(); });
    };
    auto keys = [entries, common, retired_keys, background](std::int32_t period) -> std::shared_ptr<void> {
        auto next = entries->make_key(period);
        if (period == 2000) {
            auto old = entries->negative.exchange(next);
            if (!background && old && old->next_due())
                retired_keys->push_back(std::move(old));
        } else if (period == 300000 && !background && common->access_entries) {
            auto old = std::static_pointer_cast<FlotsamKeyEntries>(common->access_entries);
            if (old->next_due())
                retired_keys->push_back(std::move(old));
        }
        return next;
    };
    auto memory = [entries, retired_memory, background](std::int32_t period) -> std::shared_ptr<void> {
        auto next = entries->make_memory(period);
        auto old = entries->memory.exchange(next);
        if (!background && old && old->next_due())
            retired_memory->push_back(std::move(old));
        return next;
    };
    auto backend = native_flotsam_config_backend(std::move(keys), std::move(memory), std::move(info),
                                                 std::move(debug), std::move(registration));
    return {std::move(entries), std::move(backend), std::move(gate)};
}
}
