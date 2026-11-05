// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/uuid_gatherer.hpp"
#include "opensim/scene/live_asset_gather.hpp"
#include "opensim/services/flotsam_memory_cache.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/texture_entry.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/services/flotsam_cache_cleanup.hpp"
#include <fstream>
#include <array>
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim;
using namespace scene;
namespace {
framework::Uuid id(int n) {
    if (n == -1)
        return framework::Uuid("968cbad0-4dad-d64e-71b5-72bf13ad051a");
    if (n == -2)
        return framework::Uuid("11111111-1111-0000-0000-000100bba000");
    if (n == -3)
        return framework::Uuid("ffffffff-ffff-ffff-ffff-ffffffffffff");
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::hex << std::setfill('0') << std::setw(12) << n;
    return framework::Uuid(s.str());
}
int number(framework::Uuid value) {
    for (int n : {-1, -2, -3})
        if (value == id(n))
            return n;
    auto n = std::stoi(value.str().substr(24), nullptr, 16);
    if (value != id(n))
        throw std::runtime_error("Full UUID");
    return n;
}
std::string label(framework::Uuid value) {
    try {
        return std::to_string(number(value));
    } catch (...) {
        return value.str();
    }
}
std::string hex(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::vector<std::uint8_t> bytes(const std::string& text) {
    std::vector<std::uint8_t> out;
    if (text == "-" || text == ".")
        return out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoi(text.substr(i, 2), nullptr, 16)));
    return out;
}
#include "live_asset_gather_fixture.hpp"
#include "scene_asset_gather_fixture.hpp"
struct Edge {
    int child;
    std::int8_t type;
    int mode;
};
struct Profile {
    int mode = 0;
    std::int8_t type = 0;
    int length = 0;
    std::vector<Edge> edges;
    framework::AssetBytes raw;
};
struct Probe {
    std::map<int, Profile> assets;
    std::shared_ptr<UuidGathererState> state;
    std::array<std::unique_ptr<UuidGatherer>, 2> gatherers;
    std::vector<std::string> trace;
    int point = 0, nth = 1, seen = 0, selected = 0;
    bool formats = false;
    std::shared_ptr<LiveFixture> live;
    std::shared_ptr<UuidGathererBackend> backend;
    std::shared_ptr<SceneFixture> scene;
    void log(const std::string& s) {
        trace.push_back(s);
    }
    void hook(int p) {
        if (point == p && ++seen == nth)
            throw std::runtime_error("fault");
    }
    void inspect(UuidGatherer& g, const std::shared_ptr<framework::AssetBase>& a, const std::string& name) {
        if (formats) {
            if (name == "embedded")
                g.inspect_embedded(*a->data);
            else if (name == "wearable")
                g.inspect_wearable(a);
            else if (name == "gesture")
                g.inspect_gesture(a);
            else if (name == "material")
                g.inspect_material(a);
            else if (name == "notecard")
                g.inspect_notecard(a);
            else
                g.inspect_object(a);
            return;
        }
        auto n = number(a->full_id());
        log("I:" + name + ":" + std::to_string(n));
        hook(2);
        for (auto edge : assets.at(n).edges) {
            auto key = id(edge.child);
            switch (edge.mode) {
            case 0:
                g.add_for_inspection(key);
                break;
            case 1:
                g.add_for_inspection(key, edge.type);
                break;
            case 2:
                g.add_gathered(key, edge.type);
                break;
            case 3:
                g.state()->uncertain.add(key);
                g.add_for_inspection(key);
                break;
            case 4:
                throw std::runtime_error("fault");
            }
        }
    }
    void init() {
        scene.reset();
        live.reset();
        assets.clear();
        formats = false;
        point = seen = selected = 0;
        nth = 1;
        state = std::make_shared<UuidGathererState>();
        auto b = std::make_shared<UuidGathererBackend>();
        b->get_asset = [this](auto key) {
            log("G:" + label(key));
            if (scene) {
                int tag = 0;
                try {
                    tag = number(key);
                } catch (...) {
                }
                scene->hit(50, tag);
            }
            hook(1);
            int n;
            try {
                n = number(key);
            } catch (...) {
                return UuidGathererBackend::Asset{};
            }
            auto it = assets.find(n);
            if (it == assets.end() || it->second.mode == 0)
                return UuidGathererBackend::Asset{};
            auto& p = it->second;
            if (p.mode == 1)
                throw std::runtime_error("fault");
            auto a = std::make_shared<framework::AssetBase>(key, "asset", p.type, "");
            if (p.mode != 2)
                a->data =
                    p.raw ? p.raw
                          : std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(p.length));
            return a;
        };
        b->error = [this](auto& s) {
            log("E:" + s);
            if (live)
                live->hit(25, 0);
            hook(3);
        };
        b->debug = [this](auto& s) {
            log("D:" + s);
            hook(4);
        };
        b->wearable = [this](auto& g, auto& a) { inspect(g, a, "wearable"); };
        b->gesture = [this](auto& g, auto& a) { inspect(g, a, "gesture"); };
        b->notecard = [this](auto& g, auto& a) { inspect(g, a, "notecard"); };
        b->embedded = [this](auto& g, auto& a) { inspect(g, a, "embedded"); };
        b->material = [this](auto& g, auto& a) { inspect(g, a, "material"); };
        b->object = [this](auto& g, auto& a) { inspect(g, a, "object"); };
        backend = b;
        for (auto& g : gatherers)
            g = std::make_unique<UuidGatherer>(b, state);
    }
    std::string execute(const std::string& line) {
        std::istringstream in(line);
        std::string op, word;
        in >> op;
        int n = 0;
        if (op != "scan" && op != "chars" && op != "skip" && op != "index")
            in >> n;
        auto& g = *gatherers[selected];
        if (op == "init") {
            init();
            return "init";
        }
        if (op == "select") {
            selected = n;
            return "select";
        }
        if (op == "fault") {
            point = n;
            in >> nth;
            seen = 0;
            return "fault";
        }
        if (op == "asset") {
            int mode, type, length;
            in >> mode >> type >> length;
            assets[n] = Profile{mode, static_cast<std::int8_t>(type), length, {}};
            return "asset";
        }
        if (op == "formats") {
            formats = n != 0;
            return "formats";
        }
        if (op == "raw") {
            in >> word;
            auto& p = assets.at(n);
            p.raw = word == "-" ? nullptr : std::make_shared<std::vector<std::uint8_t>>(bytes(word));
            p.length = p.raw ? static_cast<int>(p.raw->size()) : 0;
            return "raw";
        }
        if (op == "edge") {
            int child, type, mode;
            in >> child >> type >> mode;
            assets.at(n).edges.push_back({child, static_cast<std::int8_t>(type), mode});
            return "edge";
        }
        if (op == "add")
            return g.add_for_inspection(id(n)) ? "1" : "0";
        if (op == "known" || op == "typed") {
            int type;
            in >> type;
            if (op == "known")
                return g.add_gathered(id(n), static_cast<std::int8_t>(type)) ? "1" : "0";
            g.add_for_inspection(id(n), static_cast<std::int8_t>(type));
            return "typed";
        }
        if (op == "failed") {
            state->failed.add(id(n));
            return "failed";
        }
        if (op == "uncertain") {
            state->uncertain.add(id(n));
            return "uncertain";
        }
        if (op == "unfail") {
            state->failed.erase(id(n));
            return "unfail";
        }
        if (op == "uncollect") {
            state->gathered.erase(id(n));
            return "uncollect";
        }
        if (op == "clear-failed") {
            state->failed.clear();
            return "clear-failed";
        }
        if (op == "next")
            return g.gather_next() ? "1" : "0";
        if (op == "all")
            return g.gather_all(n != 0) ? "1" : "0";
        if (op == "skip") {
            in >> word;
            return UuidGatherer::skipped(framework::Uuid(word)) ? "1" : "0";
        }
        if (op == "peek")
            return g.next() ? label(*g.next()) : "-";
        if (op == "count") {
            g.error_count = n;
            in >> g.asset_get_count >> g.possible_not_asset_count;
            return "count";
        }
        if (op == "order") {
            std::string result;
            for (auto& [key, value] : state->gathered) {
                if (!result.empty())
                    result += ',';
                result += label(key);
            }
            return result;
        }
        if (op == "scene") {
            int cancel, nth;
            in >> cancel >> nth;
            auto fixture = std::make_shared<SceneFixture>(
                n, cancel, nth, [this](auto& s) { log(s); }, [this](int p) { hook(p); });
            scene = fixture;
            live = fixture->live;
            try {
                auto result = fixture->call(backend);
                live.reset();
                scene.reset();
                return result;
            } catch (...) {
                live.reset();
                scene.reset();
                throw;
            }
        }
        if (op == "live") {
            int sides, mutation, nth;
            in >> sides >> mutation >> nth;
            auto fixture = std::make_shared<LiveFixture>(
                n, sides, mutation, nth, [this](auto& s) { log(s); }, [this](int p) { hook(p); });
            live = fixture;
            try {
                g.add_for_inspection(fixture->group, [this](auto& text) {
                    log("W:" + text);
                    live->hit(24, 0);
                });
                live.reset();
                return fixture->disposed ? "live:1" : "live:0";
            } catch (...) {
                live.reset();
                throw;
            }
        }
        if (op == "index") {
            std::string source, pattern;
            in >> source >> pattern;
            auto decode = [](const std::string& value) {
                auto b = bytes(value);
                std::u16string text;
                for (std::size_t i = 0; i + 1 < b.size(); i += 2)
                    text += static_cast<char16_t>(b[i] | (std::uint16_t(b[i + 1]) << 8));
                return text;
            };
            return std::to_string(framework::invariant_index_of(decode(source), decode(pattern)));
        }
        if (op == "format") {
            in >> word;
            auto a = std::make_shared<framework::AssetBase>();
            if (word != "-")
                a->data = std::make_shared<std::vector<std::uint8_t>>(bytes(word));
            switch (n) {
            case 0:
                g.inspect_wearable(a);
                break;
            case 1:
                g.inspect_gesture(a);
                break;
            case 2:
                g.inspect_material(a);
                break;
            case 3:
                g.inspect_notecard(a);
                break;
            case 4:
                g.inspect_object(a);
                break;
            }
            return "format";
        }
        if (op == "scan" || op == "chars") {
            in >> word;
            auto data = bytes(word);
            if (op == "scan")
                g.inspect_embedded(data);
            else {
                std::u16string text;
                for (std::size_t i = 0; i + 1 < data.size(); i += 2)
                    text += static_cast<char16_t>(data[i] | (std::uint16_t(data[i + 1]) << 8));
                g.inspect_embedded(text);
            }
            return op;
        }
        throw std::runtime_error("command");
    }
    std::string snapshot() {
        auto& g = *gatherers[selected];
        std::string out;
        bool first = true;
        std::map<framework::Uuid, std::int8_t> sorted;
        for (auto& [key, type] : state->gathered)
            sorted[key] = type;
        for (auto& [key, type] : sorted) {
            if (!first)
                out += ',';
            first = false;
            out += label(key) + ":" + std::to_string(type);
        }
        out += '|';
        for (auto* set : {&state->failed, &state->uncertain}) {
            first = true;
            for (auto key : set->values()) {
                if (!first)
                    out += ',';
                first = false;
                out += label(key);
            }
            out += '|';
        }
        out += std::to_string(g.error_count) + ":" + std::to_string(g.asset_get_count) + ":" +
               std::to_string(g.possible_not_asset_count) + ":" + (g.verbose() ? "1" : "0") + "|";
        first = true;
        for (auto key : g.pending()) {
            if (!first)
                out += ',';
            first = false;
            out += label(key);
        }
        return out;
    }
};
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
void native_service() {
    auto cache = std::make_shared<services::FlotsamMemoryCache>(
        services::FlotsamMemoryCache::Options{true, true, 3600, 120000, false, false, 0}, [] { return 0.; },
        false);
    auto put = [&](int n, int type, const std::string& text) {
        auto a = std::make_shared<framework::AssetBase>(id(n), "fixture", static_cast<std::int8_t>(type), "");
        a->data = std::make_shared<std::vector<std::uint8_t>>(text.begin(), text.end());
        cache->cache(a, false);
    };
    put(1, 10,
        "script " + id(2).str() + "  " + id(3).str() + "  " + id(7).str() + "  " + id(1).str() + " end");
    put(2, 5,
        "LLWearable version 22\nname\ndescription\n{\nparameters 1\n0 0.5\ntextures 1\n0 " + id(4).str() +
            "\n");
    put(3, 21,
        "2\n0\n0\ntrigger\nreplace\n2\n0\nanim\n" + id(5).str() + "\n0\n1\nsound\n" + id(6).str() + "\n0\n");
    put(7, 7,
        "Linden text version 2\n{\nLLEmbeddedItems version 1\n{\ncount 0\n}\nText length 99\n" + id(8).str() +
            " end\n}");
    put(8, -2, "<llsd><map><key>NormMap</key><uuid>" + id(9).str() + "</uuid></map></llsd>");
    int object_calls = 0;
    std::vector<std::string> errors;
    UuidGatherer g(native_uuid_gatherer_backend(
        cache,
        [&](auto&, auto&) {
            ++object_calls;
            throw std::runtime_error("Unexpected object asset");
        },
        [&](auto& text) { errors.push_back(text); }, [](auto&) {}));
    g.add_for_inspection(id(1));
    require(g.gather_all(), "native service graph gather");
    auto state = g.state();
    require(
        state->gathered.size() == 9 && g.asset_get_count == 5 && g.error_count == 0 &&
            g.possible_not_asset_count == 0 && object_calls == 0 && errors.empty(),
        "native text references traverse actual cache/service without fetching leaf textures/sounds/animations");
    require(state->gathered.at(id(4)) == 0 && state->gathered.at(id(5)) == 20 &&
                state->gathered.at(id(6)) == 1 && state->gathered.at(id(9)) == 0 &&
                state->uncertain.size() == 1 && state->uncertain.contains(id(1)),
            "native typed leaf references preserve source self-reference uncertainty");
    auto xml = [](const std::string& name, const std::string& body) {
        return "<" + name + ">" + body + "</" + name + ">";
    };
    auto uuid_xml = [&](int n) { return xml("UUID", id(n).str()); };
    auto binary_xml = [&](const std::string& name, const std::vector<std::uint8_t>& data) {
        return xml(name, network::binary_base64(
                             network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(data))));
    };
    framework::TextureEntry entry(id(13));
    entry.default_texture->material_id(id(8));
    entry.create_face(0)->texture_id(id(14));
    std::vector<std::uint8_t> extra{2, 0x40, 0, 28, 0, 0, 0};
    auto projection = id(15).bytes();
    extra.insert(extra.end(), projection.begin(), projection.end());
    extra.insert(extra.end(), 12, 0);
    extra.insert(extra.end(), {0x80, 0, 18, 0, 0, 0, 1, 0});
    auto material = id(8).bytes();
    extra.insert(extra.end(), material.begin(), material.end());
    std::vector<std::uint8_t> particles(86);
    auto particle = id(16).bytes();
    std::copy(particle.begin(), particle.end(), particles.begin() + 36);
    auto override_text = id(8).str() + "x";
    put(10, 6,
        xml("SceneObjectGroup",
            xml("SoundID", uuid_xml(11)) + xml("SculptTexture", uuid_xml(12)) +
                binary_xml("TextureEntry", entry.bytes()) + binary_xml("ExtraParams", extra) +
                binary_xml("ParticleSystem", particles) +
                xml("TaskInventory",
                    xml("Item", xml("AssetID", uuid_xml(1))) + xml("Item", xml("AssetID", uuid_xml(17)))) +
                binary_xml("MatOvrd",
                           std::vector<std::uint8_t>(override_text.begin(), override_text.end()))));
    put(17, 6,
        xml("SceneObjectGroup",
            xml("SoundID", uuid_xml(18)) + xml("TaskInventory", xml("Item", xml("AssetID", uuid_xml(10))))));
    UuidGatherer objects(
        native_uuid_gatherer_backend(cache, [&](auto& text) { errors.push_back(text); }, [](auto&) {}));
    objects.add_for_inspection(id(10));
    require(objects.gather_all(), "native serialized object graph");
    require(
        objects.state()->gathered.size() == 18 && objects.asset_get_count == 7 && objects.error_count == 0 &&
            errors.empty(),
        "native serialized objects traverse textures, materials, particles, nested inventory and cyclic objects through actual cache/service");
    require(objects.state()->gathered.at(id(15)) == 0 && objects.state()->gathered.at(id(16)) == 0 &&
                objects.state()->gathered.at(id(18)) == 1,
            "native projection, particle and nested sound references");
    auto live_backend =
        native_uuid_gatherer_backend(cache, [&](auto& text) { errors.push_back(text); }, [](auto&) {});
    SceneFixture scan(0, 0, 1, [](auto&) {}, [](int) {});
    scan.call(live_backend);
    require(scan.last && scan.last->failed.size() == 0 && scan.last->uncertain.size() == 0,
            "scene sweep clears source failure/uncertainty sets");
    require(scan.last->gathered.contains(id(1)) && scan.last->gathered.contains(id(13)) &&
                scan.last->gathered.contains(id(20)) && scan.last->gathered.contains(id(30)) &&
                scan.last->gathered.contains(id(88)),
            "actual native cache/service and live object/avatar/terrain/environment provider graph");
    auto root =
        std::filesystem::temp_directory_path() / ("opensim-live-gather-" + framework::Uuid::random().str());
    std::filesystem::create_directories(root / "a");
    auto protected_file = root / "a" / id(13).str(), stale_file = root / "a" / id(900).str();
    {
        std::ofstream(protected_file) << "protected";
        std::ofstream(stale_file) << "stale";
    }
    auto life = std::make_shared<services::FlotsamCacheLifecycle>();
    life->cleanup_running = true;
    auto shared = std::make_shared<services::FlotsamCacheSharedState>();
    auto cleanup = services::FlotsamCacheCleaner::native_backend(
        cache,
        [&] {
            scan.call(live_backend);
            services::FlotsamProtectedAssets ids;
            for (auto& [key, type] : scan.last->gathered)
                ids.insert(key);
            return ids;
        },
        [] { return std::int64_t(0); }, [](auto&, auto&) {});
    services::FlotsamCacheCleaner cleaner({root, 0, 30000}, life, shared, cleanup);
    cleaner.clean_all(framework::AssetDateTime(3155378975999999999, 2));
    require(std::filesystem::exists(protected_file) && !std::filesystem::exists(stale_file),
            "native scene gather protects a live asset during actual filesystem cleanup");
    std::filesystem::remove(protected_file);
    std::filesystem::remove(root / "a");
    std::filesystem::remove(root);
}
void unit() {
    native_service();
    Probe p;
    p.init();
    p.execute("asset 1 3 6 36");
    p.execute("asset 2 3 10 36");
    p.execute("edge 1 2 10 0");
    p.execute("edge 2 1 6 0");
    p.execute("add 1");
    require(p.execute("all 0") == "1", "inspect recursive graph");
    require(p.state->gathered.size() == 2 && p.gatherers[0]->asset_get_count == 2, "cycle is fetched once");
    p.execute("select 1");
    require(p.execute("add 2") == "0", "caller-owned collector shared");
    p.execute("init");
    p.execute("uncertain 3");
    p.execute("add 3");
    p.execute("all 1");
    require(p.gatherers[0]->possible_not_asset_count == 1 && p.gatherers[0]->error_count == 0 &&
                !p.gatherers[0]->verbose(),
            "uncertain absence accounting");
    bool rejected = false;
    try {
        UuidGatherer incomplete(std::make_shared<UuidGathererBackend>());
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "missing inspectors rejected");
    std::cout
        << "PASS: recursive shared collector, cycle prevention, uncertain failure and mandatory inspector dependencies\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            Probe p;
            p.init();
            std::string line;
            while (std::getline(std::cin, line)) {
                p.trace.clear();
                std::string result;
                try {
                    result = p.execute(line);
                } catch (...) {
                    result = "EX";
                }
                std::string trace;
                for (auto& item : p.trace) {
                    if (!trace.empty())
                        trace += ';';
                    trace += item;
                }
                std::cout << result << '|' << hex(trace) << '|' << p.snapshot() << '\n';
            }
            return 0;
        }
        unit();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
