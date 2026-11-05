// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/scene/uuid_gatherer.hpp"
namespace opensim::scene {
// Owns an ordinary gatherer whose retrieval dependency implements the original
// HG override. Composition keeps existing UuidGatherer ownership unchanged.
class HGUuidGatherer {
public:
    using Asset = UuidGatherer::Asset;
    using ForeignGet = std::function<Asset(const std::string&, const std::optional<std::string>&, bool)>;
    HGUuidGatherer(std::shared_ptr<UuidGathererBackend> local, ForeignGet, std::optional<std::string> url,
                   std::function<int()> http_debug_level, std::function<void(const std::string&)> hg_debug,
                   std::shared_ptr<UuidGathererState> state = std::make_shared<UuidGathererState>());
    HGUuidGatherer(std::shared_ptr<services::AssetService>, std::optional<std::string> url,
                   std::function<int()> http_debug_level, std::function<void(const std::string&)> error,
                   std::function<void(const std::string&)> debug,
                   std::shared_ptr<UuidGathererState> state = std::make_shared<UuidGathererState>());
    // Direct fetch deliberately bypasses URL whitespace fallback and collection.
    Asset fetch_asset(framework::Uuid);
    void asset_server_url(std::optional<std::string>);
    std::optional<std::string> asset_server_url() const;
    UuidGatherer& core() {
        return core_;
    }
    const UuidGatherer& core() const {
        return core_;
    }
    bool complete() const {
        return core_.complete();
    }
    bool add_gathered(framework::Uuid id, std::int8_t type) {
        return core_.add_gathered(id, type);
    }
    bool add_for_inspection(framework::Uuid id) {
        return core_.add_for_inspection(id);
    }
    void add_for_inspection(framework::Uuid id, std::int8_t type) {
        core_.add_for_inspection(id, type);
    }
    bool gather_next() {
        return core_.gather_next();
    }
    bool gather_all(bool report = false) {
        return core_.gather_all(report);
    }
    std::shared_ptr<UuidGathererState> state() const {
        return core_.state();
    }

private:
    struct Retrieval;
    std::shared_ptr<Retrieval> retrieval_;
    UuidGatherer core_;
    static std::shared_ptr<UuidGathererBackend> decorate(std::shared_ptr<UuidGathererBackend>,
                                                         const std::shared_ptr<Retrieval>&);
};
}
