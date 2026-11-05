// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_expiring_entries.hpp"
#include <bit>
#include <chrono>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <vector>
using namespace opensim;
using namespace services;
using Asset = FlotsamAssetEntries::Asset;
namespace {
std::string bits(double n) {
    return std::to_string(std::bit_cast<std::uint64_t>(n));
}
std::string show(const Asset& asset) {
    return asset ? asset->name().value_or("-") : "-";
}
Asset make(int n) {
    auto asset =
        std::make_shared<framework::AssetBase>(std::optional<std::string>("asset" + std::to_string(n)),
                                               "asset" + std::to_string(n), std::int8_t(0), "creator");
    asset->data = std::make_shared<std::vector<std::uint8_t>>(1, static_cast<std::uint8_t>(n));
    return asset;
}
struct Probe {
    double now = 0, step = 0;
    std::unique_ptr<FlotsamKeyEntries> keys;
    std::unique_ptr<FlotsamAssetEntries> assets;
    std::vector<std::unique_ptr<FlotsamKeyEntries>> retired_keys;
    std::vector<std::unique_ptr<FlotsamAssetEntries>> retired_assets;
    std::map<int, Asset> held;
    std::map<int, std::weak_ptr<framework::AssetBase>> weak;
    double clock() {
        auto value = now;
        now += step;
        return value;
    }
    void pump() {
        if (keys)
            keys->pump();
        if (assets)
            assets->pump();
        for (auto& value : retired_keys)
            value->pump();
        for (auto& value : retired_assets)
            value->pump();
        std::erase_if(retired_keys, [](auto& value) { return !value->next_due(); });
        std::erase_if(retired_assets, [](auto& value) { return !value->next_due(); });
    }
    std::string run(const std::string& line) {
        std::istringstream in(line);
        std::string op, result;
        in >> op;
        Asset out = held.contains(999) ? held.at(999) : Asset{};
        try {
            if (op == "init") {
                int key_check, asset_check;
                std::string initial, increment;
                in >> key_check >> asset_check >> initial >> increment;
                keys.reset();
                assets.reset();
                retired_keys.clear();
                retired_assets.clear();
                held.clear();
                weak.clear();
                now = std::bit_cast<double>(std::stoull(initial, nullptr, 16));
                step = std::bit_cast<double>(std::stoull(increment, nullptr, 16));
                keys = std::make_unique<FlotsamKeyEntries>(key_check, [this] { return clock(); }, false);
                assets =
                    std::make_unique<FlotsamAssetEntries>(asset_check, [this] { return clock(); }, false);
                step = 0;
                held[999] = make(999);
                result = "init";
            } else if (op == "jump" || op == "advance") {
                double delta;
                in >> delta;
                now += delta;
                if (op == "advance")
                    pump();
                result = op;
            } else if (op == "pump") {
                pump();
                result = op;
            } else if (op == "newk") {
                int check;
                in >> check;
                if (keys->next_due())
                    retired_keys.push_back(std::move(keys));
                keys = std::make_unique<FlotsamKeyEntries>(check, [this] { return clock(); }, false);
                result = op;
            } else if (op == "newa") {
                int check;
                in >> check;
                if (assets->next_due())
                    retired_assets.push_back(std::move(assets));
                assets = std::make_unique<FlotsamAssetEntries>(check, [this] { return clock(); }, false);
                result = op;
            } else if (op == "keyclear") {
                keys->clear();
                result = op;
            } else if (op == "assetclear") {
                assets->clear();
                result = op;
            } else if (op == "keydispose") {
                keys->dispose();
                result = op;
            } else if (op == "assetdispose") {
                assets->dispose();
                result = op;
            } else {
                int id;
                in >> id;
                auto name = "key" + std::to_string(id);
                if (op == "keyadd") {
                    std::int32_t ttl;
                    in >> ttl;
                    keys->add(name, ttl);
                    result = op;
                } else if (op == "assetadd") {
                    std::int32_t ttl;
                    int tag;
                    in >> ttl >> tag;
                    Asset value;
                    if (tag >= 0) {
                        if (!held.contains(tag))
                            held[tag] = make(tag);
                        value = held[tag];
                        weak[tag] = value;
                    }
                    assets->add(name, value, ttl);
                    result = op;
                } else if (op == "keyget")
                    result = keys->contains(name) ? "1" : "0";
                else if (op == "assetget") {
                    bool found = assets->try_get(name, out);
                    result = (found ? "1:" : "0:") + show(out);
                } else if (op == "keyremove")
                    result = keys->remove(name) ? "1" : "0";
                else if (op == "assetremove")
                    result = assets->remove(name) ? "1" : "0";
                else if (op == "drop") {
                    held.erase(id);
                    result = op;
                } else if (op == "alive")
                    result = weak.contains(id) && !weak[id].expired() ? "1" : "0";
                else if (op == "mutate") {
                    if (held.contains(id))
                        held[id]->name(held[id]->name().value_or("") + "X");
                    result = op;
                } else
                    throw std::runtime_error("Unknown entries operation");
            }
        } catch (const FlotsamEntriesDisposed& e) {
            result = std::string("EX:NullReferenceException:") + e.what() + ":OUT=" + show(out);
        }
        auto due = [](const auto& value) {
            auto n = value->next_due();
            return n ? bits(*n) : "-";
        };
        return result + "|" + std::to_string(keys->count()) + "," + std::to_string(assets->count()) + "|" +
               due(keys) + "," + due(assets) + "|" + bits(keys->epoch()) + "," +
               std::to_string(keys->check_period()) + "," + bits(assets->epoch()) + "," +
               std::to_string(assets->check_period()) + "|" + bits(now);
    }
};
template <class F> void until(F condition, const char* message) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!condition() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!condition())
        throw std::runtime_error(message);
}
void real() {
    std::weak_ptr<framework::AssetBase> retained;
    {
        auto entries = std::make_unique<FlotsamAssetEntries>(500);
        auto asset = make(1);
        retained = asset;
        entries->add("retained", asset, 750);
        asset.reset();
        entries.reset();
    }
    if (retained.expired())
        throw std::runtime_error("Armed worker did not retain asset after wrapper destruction");
    until([&] { return retained.expired(); }, "Retired container did not expire its asset");
    {
        FlotsamAssetEntries entries(500);
        auto asset = make(2);
        std::weak_ptr<framework::AssetBase> weak = asset;
        entries.add("disposed", asset, 0);
        asset.reset();
        entries.dispose();
        if (entries.count() != 1 || weak.expired() || entries.next_due())
            throw std::runtime_error("Dispose must retain values/count and stop timer");
        Asset out = make(999);
        try {
            entries.try_get("disposed", out);
            throw std::runtime_error("Disposed map lock did not fail");
        } catch (const FlotsamEntriesDisposed&) {
            if (show(out) != "asset999")
                throw std::runtime_error("Disposed get modified caller output");
        }
    }
    FlotsamKeyEntries keys(500);
    keys.add("native", 500);
    until([&] { return !keys.contains("native"); }, "Actual key timer failed to expire");
    keys.dispose();
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--retained") {
            real();
            std::cout << "1:1:1:1\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            Probe p;
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << p.run(line) << '\n';
            return 0;
        }
        real();
        std::cout << "PASS: actual expiring key/asset timers, armed-state ownership and disposed retention\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
