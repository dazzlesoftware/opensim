// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
namespace opensim::services {
class FlotsamEntriesDisposed final : public std::runtime_error {
public:
    FlotsamEntriesDisposed() : runtime_error("Object reference not set to an instance of an object.") {}
};
// Used ExpiringKey/ExpiringCacheOS surface. Each actual container captures its
// own constructor epoch. Checks/reads never purge. Clear preserves epoch;
// Dispose cancels timers and preserves values/count but invalidates lock-using
// operations. An armed background worker owns State, never a raw wrapper.
class FlotsamKeyEntries {
public:
    using Clock = std::function<double()>;
    explicit FlotsamKeyEntries(std::int32_t check_ms, Clock = {}, bool background = true);
    ~FlotsamKeyEntries();
    void add(const std::string&, std::int32_t ttl);
    bool contains(const std::string&) const;
    bool remove(const std::string&);
    void clear();
    void pump();
    void dispose();
    std::int32_t count() const;
    double epoch() const;
    std::int32_t check_period() const;
    std::optional<double> next_due() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
class FlotsamAssetEntries {
public:
    using Asset = std::shared_ptr<framework::AssetBase>;
    using Clock = std::function<double()>;
    explicit FlotsamAssetEntries(std::int32_t check_ms, Clock = {}, bool background = true);
    ~FlotsamAssetEntries();
    void add(const std::string&, Asset, std::int32_t ttl);
    bool try_get(const std::string&, Asset&) const;
    bool remove(const std::string&);
    void clear();
    void pump();
    void dispose();
    std::int32_t count() const;
    double epoch() const;
    std::int32_t check_period() const;
    std::optional<double> next_due() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};
}
