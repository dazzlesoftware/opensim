// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/appearance_notecard.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace opensim;
using namespace framework;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct Cache final : IAssetCache {
    std::shared_ptr<AssetBase> asset;
    std::vector<std::string> calls;
    bool fail_expire = false;

    void cache(const std::shared_ptr<AssetBase> &, bool) override {
        calls.push_back("cache");
    }

    void cache_negative(const std::string &) override {
        throw std::logic_error("Unexpected negative cache");
    }

    bool get(const std::string &id, std::shared_ptr<AssetBase> &result) override {
        calls.push_back("get:" + id);
        result = asset;
        return false;
    }

    std::shared_ptr<AssetBase> get_cached(const std::string &) override {
        return asset;
    }

    bool get_from_memory(const std::string &, std::shared_ptr<AssetBase> &) override {
        return false;
    }

    bool check(const std::string &) override {
        return bool(asset);
    }

    void expire(const std::string &id) override {
        calls.push_back("expire:" + id);
        if (fail_expire)
            throw std::runtime_error("Expire failed");
        asset.reset();
    }

    void clear() override {
        asset.reset();
    }
};

void persistence() {
    const auto texture = Uuid::random();
    const auto cache = std::make_shared<Cache>();
    unsigned stores = 0;
    services::persist_cached_appearance_bake(texture, {}, [&](const auto &) { ++stores; });
    services::persist_cached_appearance_bake(texture, cache, [&](const auto &) { ++stores; });
    check(stores == 0 && cache->calls == std::vector<std::string>{"get:" + texture.str()},
          "Absent caches and missing assets must not store a bake");

    const auto asset = std::make_shared<AssetBase>(texture, "Bake", 0, "Creator");
    asset->id("https://foreign.example/assets/" + texture.str());
    asset->local(true);
    asset->temporary(true);
    asset->flags(32);
    asset->data = std::make_shared<std::vector<std::uint8_t>>(3, 42);
    const auto data = asset->data;
    cache->asset = asset;
    cache->calls.clear();
    services::persist_cached_appearance_bake(texture, cache, [&](const auto &saved) {
        ++stores;
        check(saved == asset && saved->data == data && !cache->asset,
              "Bake promotion must expire the cache and store the original shared asset");
        check(saved->id() == texture.str() && saved->description() == "NPC BAKED" && !saved->local() &&
                  !saved->temporary() && saved->flags() == 32,
              "Bake promotion must normalize the ID and clear only local and temporary metadata");
    });
    check(stores == 1 && cache->calls == std::vector<std::string>{"get:" + texture.str(), "expire:" + texture.str()},
          "Bake promotion must ignore the cache lookup boolean when an asset is returned");

    cache->asset = asset;
    cache->calls.clear();
    services::persist_cached_appearance_bake(texture, cache, [&](const auto &) { ++stores; });
    check(stores == 1 && cache->calls.size() == 1, "An already permanent asset must not be expired or stored");

    asset->local(true);
    asset->temporary(true);
    cache->fail_expire = true;
    try {
        services::persist_cached_appearance_bake(texture, cache, [&](const auto &) { ++stores; });
        throw std::logic_error("Cache expiry failure was swallowed");
    } catch (const std::runtime_error &error) {
        check(std::string(error.what()) == "Expire failed" && asset->local() && asset->temporary(),
              "Cache expiry must fail before the shared asset is mutated");
    }
    cache->fail_expire = false;
    try {
        services::persist_cached_appearance_bake(texture, cache,
                                                 [&](const auto &) { throw std::runtime_error("Store failed"); });
        throw std::logic_error("Storage failure was swallowed");
    } catch (const std::runtime_error &error) {
        check(std::string(error.what()) == "Store failed" && !asset->local() && !asset->temporary() && !cache->asset &&
                  cache->calls.back() == "expire:" + texture.str(),
              "A storage exception must retain source metadata mutations and expired-cache state");
    }
}

void notecard() {
    AvatarAppearance appearance;
    appearance.set_attachment(2, Uuid::random(), Uuid::random());
    appearance.set_attachment(31, Uuid::random(), Uuid::random());
    const auto all = services::appearance_notecard_xml(appearance, false);
    const auto filtered = services::appearance_notecard_xml(appearance, true);
    check(all.find("\n  <map>") != std::string::npos,
          "Appearance notecards must use the original two-space XML indentation");
    check(all.find("<array />") == std::string::npos && all.find("<array/>") == std::string::npos,
          "Appearance notecards must retain the SDK full end tags for empty arrays");
    check(network::parse_llsd_xml(all).get("attachments").array.size() == 2 &&
              network::parse_llsd_xml(filtered).get("attachments").array.size() == 1 &&
              appearance.attachments().size() == 2,
          "Appearance notecards must filter HUDs without changing the live appearance");
}
} // namespace

int main() {
    try {
        persistence();
        notecard();
        std::cout << "PASS: appearance notecard XML and shared baked-asset persistence\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
