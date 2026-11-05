// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/hg_uuid_gatherer.hpp"
#include "opensim/services/asset_store.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
using namespace opensim;
using namespace scene;
namespace {
framework::Uuid id(int n) {
    if (n == -1)
        return framework::Uuid("968cbad0-4dad-d64e-71b5-72bf13ad051a");
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12) << n;
    return framework::Uuid(s.str());
}
int number(framework::Uuid key) {
    return key == id(-1) ? -1 : std::stoi(key.str().substr(24), nullptr, 16);
}
std::string hex(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::optional<std::string> unhex(const std::string& s) {
    if (s == "-")
        return {};
    std::string out;
    if (s == ".")
        return out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out.push_back(static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return out;
}
std::string show_url(const std::optional<std::string>& url) {
    return url ? "V:" + hex(*url) : "N";
}
struct RecordingService : services::StoredAssetService {
    int local_calls = 0, foreign_calls = 0;
    std::vector<std::optional<std::string>> urls;
    explicit RecordingService(std::shared_ptr<services::AssetStore> db) : StoredAssetService(std::move(db)) {
    }
    Asset get_asset(const std::string& key) override {
        ++local_calls;
        return StoredAssetService::get_asset(key);
    }
    Asset get_foreign(const std::string& key, const std::optional<std::string>& url, bool local) override {
        if (!local)
            throw std::runtime_error("HG must store on the local grid");
        ++foreign_calls;
        urls.push_back(url);
        return StoredAssetService::get_asset(key);
    }
};
void integration() {
    auto db = std::make_shared<services::SqliteAssetStore>(":memory:", true);
    auto script = std::make_shared<framework::AssetBase>(id(1), "HG script", std::int8_t(10), "");
    auto text = id(2).str() + " trailing";
    script->data = std::make_shared<std::vector<std::uint8_t>>(text.begin(), text.end());
    auto texture = std::make_shared<framework::AssetBase>(id(2), "HG texture", std::int8_t(0), "");
    texture->data = std::make_shared<std::vector<std::uint8_t>>(1, 7);
    if (!db->store(script) || !db->store(texture))
        throw std::runtime_error("HG SQLite seed");
    auto service = std::make_shared<RecordingService>(db);
    auto error = [](const std::string& text) { throw std::runtime_error(text); };
    auto debug = [](const std::string&) {};
    HGUuidGatherer remote(service, "http://asset-service/", [] { return 0; }, error, debug);
    remote.add_for_inspection(id(1));
    remote.gather_all();
    if (service->foreign_calls != 2 || service->local_calls || remote.state()->gathered.size() != 2 ||
        remote.core().asset_get_count != 2 ||
        service->urls !=
            std::vector<std::optional<std::string>>{"http://asset-service/", "http://asset-service/"})
        throw std::runtime_error("Native HG backend/SQLite/embedded traversal integration");
    HGUuidGatherer local(service, "\xe3\x80\x80", [] { return 0; }, error, debug);
    local.add_for_inspection(id(1));
    local.gather_all();
    if (service->local_calls != 2 || service->foreign_calls != 2 || local.state()->gathered.size() != 2)
        throw std::runtime_error("Native HG Unicode whitespace local service fallback");
    remote.asset_server_url({});
    if (remote.fetch_asset(id(0)) || service->foreign_calls != 3 || service->urls.back() ||
        remote.core().asset_get_count != 2 || remote.state()->gathered.size() != 2)
        throw std::runtime_error(
            "Direct null-URL zero fetch must remain foreign and leave core state unchanged");
}
struct Probe {
    int level = 0, mode = 0, point = 0, nth = 1, seen = 0, mut_url = 0, mut_level = 0, depth = 0;
    std::vector<std::string> trace;
    HGUuidGatherer* hg = nullptr;
    void log(const std::string& s) {
        trace.push_back(s);
    }
    void hook(int p) {
        if (p == point && ++seen == nth)
            throw std::runtime_error("fault");
    }
    void mutate() {
        if (mut_url == 1)
            hg->asset_server_url({});
        else if (mut_url == 2)
            hg->asset_server_url("changed");
        else if (mut_url == 3)
            hg->asset_server_url(" \t");
        if (mut_level)
            level = mut_level - 2;
    }
    UuidGatherer::Asset get(framework::Uuid key, bool foreign, const std::optional<std::string>& url = {},
                            bool local = false) {
        int n = number(key);
        log((foreign ? "F:" : "L:") + std::to_string(n) +
            (foreign ? ":" + show_url(url) + ":" + (local ? "1" : "0") : ""));
        hook(foreign ? 2 : 1);
        mutate();
        if (foreign)
            log("ARG:" + show_url(url));
        if (mode == 2 || (mode == 6 && n == 2))
            throw std::runtime_error("fault");
        if (mode == 1 || (mode == 5 && n == 2))
            return {};
        auto a = std::make_shared<framework::AssetBase>(key, "asset", std::int8_t(10), "");
        if (mode != 3)
            a->data =
                std::make_shared<std::vector<std::uint8_t>>(mode == 4 || (mode == 7 && n == 2) ? 0 : 36);
        return a;
    }
    std::shared_ptr<UuidGathererBackend> backend() {
        auto b = std::make_shared<UuidGathererBackend>();
        b->get_asset = [this](auto key) { return get(key, false); };
        b->error = [this](auto& s) {
            log("E:" + s);
            hook(5);
        };
        b->debug = [this](auto& s) {
            log("B:" + s);
            hook(6);
        };
        auto inspect = [this](auto& g, auto& a) {
            int n = number(a->full_id());
            log("I:" + std::to_string(n));
            hook(7);
            if (n > 0 && n < depth)
                g.add_for_inspection(id(n + 1));
            else if (depth > 1)
                g.add_for_inspection(id(1));
        };
        b->wearable = b->gesture = b->notecard = b->embedded = b->material = b->object = inspect;
        return b;
    }
    std::string snapshot(const UuidGatherer& g) {
        std::vector<std::string> values;
        for (auto& [key, type] : g.state()->gathered)
            values.push_back(std::to_string(number(key)) + ":" + std::to_string(type));
        std::sort(values.begin(), values.end());
        auto join = [](const auto& list) {
            std::string s;
            for (auto& item : list) {
                if (!s.empty())
                    s += ',';
                s += item;
            }
            return s;
        };
        std::vector<std::string> failed, uncertain, pending;
        for (auto key : g.state()->failed.values())
            failed.push_back(std::to_string(number(key)));
        for (auto key : g.state()->uncertain.values())
            uncertain.push_back(std::to_string(number(key)));
        for (auto key : g.pending())
            pending.push_back(std::to_string(number(key)));
        return join(values) + "|" + join(failed) + "|" + join(uncertain) + "|" +
               std::to_string(g.error_count) + ":" + std::to_string(g.asset_get_count) + ":" +
               std::to_string(g.possible_not_asset_count) + ":" + (g.verbose() ? "1" : "0") + "|" +
               join(pending);
    }
    std::string run(const std::string& line) {
        std::istringstream in(line);
        std::string url;
        int op, collector, target;
        in >> url >> op >> level >> mode >> point >> nth >> mut_url >> mut_level >> collector >> target >>
            depth;
        auto state = std::make_shared<UuidGathererState>();
        if (collector == 1)
            state->gathered[id(1)] = 7;
        if (collector == 2)
            state->failed.add(id(1));
        if (collector == 3)
            state->uncertain.add(id(1));
        if (collector == 4)
            state->gathered[id(2)] = 7;
        auto make = [&] {
            return std::make_unique<HGUuidGatherer>(
                backend(),
                [this](const auto& key, const auto& foreign, bool local) {
                    return get(framework::Uuid(key), true, foreign, local);
                },
                unhex(url),
                [this] {
                    log("D:" + std::to_string(level));
                    hook(3);
                    return level;
                },
                [this](auto& s) {
                    log("H:" + s);
                    hook(4);
                },
                state);
        };
        auto first = make();
        hg = first.get();
        std::string result;
        std::unique_ptr<HGUuidGatherer> second;
        try {
            if (op == 0 || op == 4) {
                auto a = first->fetch_asset(id(target));
                result = a ? "A:" + std::to_string(number(a->full_id())) + ":" + std::to_string(a->type()) +
                                 ":" + (a->data ? std::to_string(a->data->size()) : "N")
                           : "-";
            }
            if (op != 0) {
                first->add_for_inspection(id(target));
                bool gathered = first->gather_all(op == 2);
                if (op != 4)
                    result = gathered ? "1" : "0";
                if (op == 3) {
                    second = make();
                    hg = second.get();
                    second->add_for_inspection(id(target));
                    result += second->gather_all() ? ":1" : ":0";
                }
            }
        } catch (...) {
            result = "EX";
        }
        std::string events;
        for (auto& event : trace) {
            if (!events.empty())
                events += ';';
            events += event;
        }
        return result + "|" + hex(events) + "|" + show_url(first->asset_server_url()) + "|" +
               std::to_string(level) + "|" + snapshot(first->core()) +
               (second ? "|S:" + snapshot(second->core()) : "");
    }
};
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string line;
            while (std::getline(std::cin, line)) {
                Probe p;
                std::cout << p.run(line) << '\n';
            }
            return 0;
        }
        integration();
        Probe p;
        auto out = p.run("687474703a2f2f6173736574732f 1 1 0 0 1 0 0 0 1 3");
        if (!out.starts_with("1|"))
            throw std::runtime_error("HG recursive fetch");
        Probe direct;
        if (!direct.run("- 0 1 0 0 1 0 0 0 0 0").starts_with("A:0:10:36|"))
            throw std::runtime_error("Public direct fetch must bypass zero/URL fallback");
        std::cout << "PASS: Hypergrid direct and recursive retrieval\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
