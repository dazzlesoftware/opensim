// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_config.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>
using namespace opensim::services;
namespace {
std::string hex(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string text(const std::optional<std::string>& value) {
    return value ? hex(*value) : "~";
}
struct Handle {
    int tag;
};
struct Probe;
struct Section : FlotsamConfigSection {
    Probe& p;
    std::string name;
    std::map<std::string, std::string> values;
    Section(Probe& p, std::string name) : p(p), name(std::move(name)) {
    }
    std::optional<std::string> text(const std::string&, const std::optional<std::string>&) override;
    bool boolean(const std::string&, bool) override;
    std::int32_t integer(const std::string&, std::int32_t) override;
    std::int64_t long_integer(const std::string&, std::int64_t) override;
    double real(const std::string&, double) override;
};
struct Source : FlotsamConfigSource {
    Probe& p;
    std::map<std::string, std::shared_ptr<Section>> sections;
    explicit Source(Probe& p) : p(p) {
    }
    std::shared_ptr<FlotsamConfigSection> section(const std::string&) override;
};
struct Probe {
    int point = 0, nth = 1, seen = 0, serial = 0;
    std::vector<std::string> trace;
    std::shared_ptr<FlotsamStaticConfig> shared;
    std::shared_ptr<FlotsamInstanceConfig> instances[2];
    std::shared_ptr<FlotsamCacheConfig> modules[2];
    void hit(int n) {
        if (n == point && ++seen == nth)
            throw std::runtime_error("fault");
    }
    void log(const std::string& s) {
        trace.push_back(s);
    }
    void reset() {
        point = seen = serial = 0;
        nth = 1;
        shared = std::make_shared<FlotsamStaticConfig>();
        for (int n = 0; n < 2; ++n) {
            instances[n] = std::make_shared<FlotsamInstanceConfig>();
            auto b = std::make_shared<FlotsamConfigBackend>();
            b->full_path = [this](auto& value) {
                log("P:" + text(value));
                hit(7);
                if (!value || value->empty())
                    throw std::runtime_error("path");
                return std::optional<std::string>("FULL:" + *value);
            };
            b->hours_to_ticks = [this](double value) {
                log("T:" + std::to_string(std::bit_cast<std::uint64_t>(value)));
                hit(8);
                double ticks = value * 36000000000.0;
                if (std::isnan(ticks) || ticks > static_cast<double>(INT64_MAX) ||
                    ticks < static_cast<double>(INT64_MIN))
                    throw std::runtime_error("timespan");
                if (ticks == static_cast<double>(INT64_MAX))
                    return INT64_MAX;
                return static_cast<std::int64_t>(ticks);
            };
            b->total_hours = [this](std::int64_t ticks) {
                log("H:" + std::to_string(ticks));
                hit(14);
                return static_cast<double>(ticks) * (1.0 / 36000000000.0);
            };
            b->make_key_cache = [this](int interval) {
                int tag = ++serial;
                log("K:" + std::to_string(tag) + ":" + std::to_string(interval));
                hit(9);
                return std::static_pointer_cast<void>(std::make_shared<Handle>(Handle{tag}));
            };
            b->make_memory_cache = [this](int interval) {
                int tag = ++serial;
                log("M:" + std::to_string(tag) + ":" + std::to_string(interval));
                hit(10);
                return std::static_pointer_cast<void>(std::make_shared<Handle>(Handle{tag}));
            };
            b->info = [this](auto& s) {
                log("I:" + s);
                hit(11);
            };
            b->debug = [this](auto& s) {
                log("D:" + s);
                hit(12);
            };
            b->register_command = [this](auto& c) {
                log("C:" + c.category + ":" + (c.shared ? "1" : "0") + ":" + c.command + ":" + c.usage + ":" +
                    c.description);
                hit(13);
            };
            modules[n] = std::make_shared<FlotsamCacheConfig>(instances[n], shared, b);
        }
    }
    std::shared_ptr<Source> source(int profile) {
        if (profile < 0)
            return {};
        auto s = std::make_shared<Source>(*this);
        auto add = [&](const std::string& name) {
            auto section = std::make_shared<Section>(*this, name);
            s->sections[name] = section;
            return section;
        };
        if (profile == 0)
            return s;
        auto modules = add("Modules");
        if (profile == 1)
            return s;
        modules->values["AssetCaching"] = profile == 2 ? "wrong" : "FlotsamAssetCache";
        if (profile < 4)
            return s;
        auto assets = add("AssetCache");
        if (profile == 4)
            return s;
        assets->values = {{"FileCacheEnabled", "true"},
                          {"MemoryCacheEnabled", "true"},
                          {"MemoryCacheTimeout", "0.25"},
                          {"NegativeCacheEnabled", "false"},
                          {"NegativeCacheTimeout", "-2147484"},
                          {"UpdateFileTimeOnCacheHit", "true"},
                          {"LogLevel", "2"},
                          {"HitRateDisplay", "-1"},
                          {"FileCacheTimeout", "1.25"},
                          {"FileCleanupTimer", "0.001"},
                          {"CacheDirectoryTiers", "9"},
                          {"CacheDirectoryTierLength", "0"},
                          {"CacheWarnAt", "0"}};
        auto service = add("AssetService");
        service->values = {{"DefaultAssetLoader", "loader"}, {"AssetLoaderArgs", "args"}};
        switch (profile) {
        case 6:
            assets->values["FileCacheEnabled"] = "false";
            assets->values["MemoryCacheTimeout"] = "NaN";
            assets->values["NegativeCacheEnabled"] = "true";
            assets->values["NegativeCacheTimeout"] = "2147483647";
            assets->values["LogLevel"] = "-2147483648";
            assets->values["HitRateDisplay"] = "9223372036854775807";
            assets->values["FileCacheTimeout"] = "-1";
            assets->values["FileCleanupTimer"] = "0";
            assets->values["CacheDirectoryTiers"] = "-9";
            assets->values["CacheDirectoryTierLength"] = "99";
            assets->values["CacheWarnAt"] = "-1";
            service->values["AssetLoaderArgs"] = " \xe2\x80\x83 ";
            break;
        case 7:
            assets->values["FileCacheTimeout"] = "NaN";
            break;
        case 8:
            assets->values["FileCacheTimeout"] = "Infinity";
            break;
        case 9:
            assets->values["FileCacheTimeout"] = "1e20";
            break;
        case 10:
            assets->values["FileCacheEnabled"] = "bogus";
            break;
        case 11:
            assets->values["NegativeCacheTimeout"] = "2147483648";
            break;
        case 12:
            assets->values["MemoryCacheTimeout"] = "x";
            break;
        case 13:
            assets->values["CacheDirectory"] = "";
            break;
        case 14:
            assets->values["MemoryCacheTimeout"] = "Infinity";
            service->values["DefaultAssetLoader"] = "\xe2\x80\x83";
            break;
        case 15:
            assets->values["MemoryCacheTimeout"] = "-0";
            assets->values["CacheDirectory"] = "\xc3\xa9/\xe6\xbc\xa2";
            service->values["AssetLoaderArgs"] = "\xe2\x80\x8b";
            break;
        }
        return s;
    }
    int tag(const std::shared_ptr<void>& h) {
        return h ? std::static_pointer_cast<Handle>(h)->tag : 0;
    }
    std::string snapshot() {
        std::string out;
        for (auto& p : instances) {
            auto& s = *p;
            if (!out.empty())
                out += ';';
            out += (s.enabled ? "1" : "0") + std::string(s.file ? "1" : "0") + (s.memory ? "1" : "0") +
                   (s.negative ? "1" : "0") + "," + text(s.directory) + "," +
                   std::to_string(std::bit_cast<std::uint64_t>(s.memory_seconds)) + "," +
                   std::to_string(s.negative_milliseconds) + "," + std::to_string(s.log_level) + "," +
                   std::to_string(s.hit_rate_display) + "," + std::to_string(s.expiration_ticks) + "," +
                   std::to_string(s.cleanup_ticks) + "," + text(s.loader) + "," + text(s.loader_arguments) +
                   "," + std::to_string(tag(s.negative_entries)) + "," +
                   std::to_string(tag(s.memory_entries));
        }
        return out + "|" + std::to_string(shared->tiers) + "," + std::to_string(shared->tier_length) + "," +
               std::to_string(shared->warn_at) + "," + (shared->update_access ? "1" : "0") + "," +
               std::to_string(tag(shared->access_entries)) + "," + std::to_string(serial);
    }
    std::string run(const std::string& line) {
        trace.clear();
        std::istringstream in(line);
        std::string op, result;
        int n = 0;
        in >> op >> n;
        try {
            if (op == "reset") {
                reset();
                result = "reset";
            } else if (op == "fail") {
                point = n;
                in >> nth;
                seen = 0;
                result = "fail";
            } else {
                int profile;
                in >> profile;
                modules[n]->initialise(source(profile));
                result = "OK";
            }
        } catch (...) {
            result = "EX";
        }
        std::string events;
        for (auto& s : trace) {
            if (!events.empty())
                events += ';';
            events += s;
        }
        return result + "|" + hex(events) + "|" + snapshot();
    }
};
std::shared_ptr<FlotsamConfigSection> Source::section(const std::string& name) {
    p.log("S:" + name);
    p.hit(1);
    auto i = sections.find(name);
    return i == sections.end() ? nullptr : i->second;
}
std::optional<std::string> Section::text(const std::string& key, const std::optional<std::string>& fallback) {
    p.log("G:" + name + ":" + key + ":" + ::text(fallback));
    p.hit(2);
    auto i = values.find(key);
    return i == values.end() ? fallback : std::optional<std::string>(i->second);
}
bool Section::boolean(const std::string& key, bool fallback) {
    p.log("B:" + name + ":" + key + ":" + (fallback ? "1" : "0"));
    p.hit(3);
    auto i = values.find(key);
    if (i == values.end())
        return fallback;
    if (i->second == "true")
        return true;
    if (i->second == "false")
        return false;
    throw std::runtime_error("boolean");
}
std::int32_t Section::integer(const std::string& key, std::int32_t fallback) {
    p.log("N:" + name + ":" + key + ":" + std::to_string(fallback));
    p.hit(5);
    auto i = values.find(key);
    if (i == values.end())
        return fallback;
    std::size_t end;
    auto v = std::stoll(i->second, &end);
    if (end != i->second.size() || v < INT32_MIN || v > INT32_MAX)
        throw std::runtime_error("integer");
    return static_cast<std::int32_t>(v);
}
std::int64_t Section::long_integer(const std::string& key, std::int64_t fallback) {
    p.log("L:" + name + ":" + key + ":" + std::to_string(fallback));
    p.hit(6);
    auto i = values.find(key);
    if (i == values.end())
        return fallback;
    std::size_t end;
    auto v = std::stoll(i->second, &end);
    if (end != i->second.size())
        throw std::runtime_error("long");
    return v;
}
double Section::real(const std::string& key, double fallback) {
    p.log("R:" + name + ":" + key + ":" + std::to_string(std::bit_cast<std::uint64_t>(fallback)));
    p.hit(4);
    auto i = values.find(key);
    if (i == values.end())
        return fallback;
    if (i->second == "NaN")
        return std::bit_cast<double>(std::uint64_t(0xfff8000000000000ull));
    std::size_t end;
    auto v = std::stod(i->second, &end);
    if (end != i->second.size())
        throw std::runtime_error("double");
    return v;
}
}
int main(int argc, char** argv) {
    try {
        Probe p;
        p.reset();
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << p.run(line) << '\n';
            return 0;
        }
        p.run("init 0 5");
        if (!p.instances[0]->enabled || p.shared->tiers != 3 || p.shared->tier_length != 1 ||
            p.instances[0]->negative_milliseconds != 2147483296)
            throw std::runtime_error("Initializer clamps/wrap");
        p.run("init 1 6");
        if (p.shared->update_access || p.shared->tiers != 1 ||
            p.instances[0]->directory != std::optional<std::string>("FULL:assetcache"))
            throw std::runtime_error("Shared settings or instance state");
        std::cout << "PASS: Flotsam initializer state, shared fields and partial-failure core\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
