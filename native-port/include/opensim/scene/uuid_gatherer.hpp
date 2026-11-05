// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/asset_service.hpp"
#include "opensim/framework/ordered_dictionary.hpp"
#include <deque>
#include <functional>
#include <map>
namespace opensim::scene {
class UuidGatherer;
class GatherObject;
class GatherPart;
// Shared dictionaries/sets preserve the source constructor's caller ownership.
class GathererIds {
    framework::OrderedDictionary<std::string, bool> values_;

public:
    bool contains(framework::Uuid id) const {
        return values_.contains(id.str());
    }
    bool add(framework::Uuid id) {
        auto key = id.str();
        if (values_.contains(key))
            return false;
        values_[key] = true;
        return true;
    }
    bool erase(framework::Uuid id) {
        return values_.erase(id.str());
    }
    void clear() {
        values_.clear();
    }
    std::size_t size() const {
        return values_.size();
    }
    std::vector<framework::Uuid> values() const {
        std::vector<framework::Uuid> out;
        for (auto& [key, value] : values_)
            out.emplace_back(key);
        return out;
    }
};
using GatheredAssetMap = framework::OrderedDictionary<framework::Uuid, std::int8_t, framework::UuidHash>;
struct UuidGathererState {
    GatheredAssetMap gathered;
    GathererIds failed, uncertain;
};
struct UuidGathererBackend {
    using Asset = std::shared_ptr<framework::AssetBase>;
    using Inspector = std::function<void(UuidGatherer&, const Asset&)>;
    std::function<Asset(framework::Uuid)> get_asset;
    // Required inspectors; never silently omit a reference-bearing asset family.
    Inspector wearable, gesture, notecard, embedded, material, object;
    std::function<void(const std::string&)> error, debug;
};
// Source UuidGatherer queue, typed dispatch, failure accounting and shared state.
// Scene-object traversal and asset-format inspectors are separate dependencies.
class UuidGatherer {
public:
    using Uuid = framework::Uuid;
    using Asset = std::shared_ptr<framework::AssetBase>;
    explicit UuidGatherer(std::shared_ptr<UuidGathererBackend>,
                          std::shared_ptr<UuidGathererState> = std::make_shared<UuidGathererState>());
    bool complete() const {
        return queue_.empty();
    }
    std::optional<Uuid> next() const {
        return complete() ? std::nullopt : std::optional(queue_.front());
    }
    bool add_gathered(Uuid, std::int8_t);
    bool add_for_inspection(Uuid);
    void add_for_inspection(Uuid, std::int8_t);
    std::vector<Uuid> pending() const {
        return {queue_.begin(), queue_.end()};
    }
    bool gather_next();
    bool gather_all(bool report = false);
    std::shared_ptr<UuidGathererState> state() const {
        return state_;
    }
    std::int32_t possible_not_asset_count = 0, error_count = 0, asset_get_count = 0;
    bool verbose() const {
        return verbose_;
    }
    static bool skipped(Uuid);
    // Original byte/string embedded scanner, including its strict final boundary
    // and post-candidate skip. UTF8 bytes and UTF16 units are distinct overloads.
    void inspect_object(const Asset&);
    void add_for_inspection(const std::shared_ptr<GatherObject>&,
                            const std::function<void(const std::string&)>& warn);
    void inspect_notecard(const Asset&);
    void inspect_wearable(const Asset&);
    void inspect_gesture(const Asset&);
    void inspect_material(const Asset&);
    void inspect_embedded(std::span<const std::uint8_t>);
    void inspect_embedded(std::u16string_view);

private:
    void inspect_live_materials(const std::shared_ptr<GatherPart>&,
                                const std::function<void(const std::string&)>& warn);
    void get_asset_uuids(Uuid);
    void record_failure(Uuid);
    std::deque<Uuid> queue_;
    bool verbose_ = true;
    std::shared_ptr<UuidGathererBackend> backend_;
    std::shared_ptr<UuidGathererState> state_;
};
// Native service binding. An explicit object inspector can override the native
// serialized-object reader when a caller supplies a different object format.
std::shared_ptr<UuidGathererBackend>
native_uuid_gatherer_backend(std::shared_ptr<services::AssetService>, UuidGathererBackend::Inspector object,
                             std::function<void(const std::string&)> error,
                             std::function<void(const std::string&)> debug);
std::shared_ptr<UuidGathererBackend>
native_uuid_gatherer_backend(std::shared_ptr<services::AssetService>,
                             std::function<void(const std::string&)> error,
                             std::function<void(const std::string&)> debug);
}
