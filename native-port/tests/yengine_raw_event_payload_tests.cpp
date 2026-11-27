// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include <iostream>
#include <memory>
using namespace utf16_test;

namespace {
std::span<const std::uint8_t> bytes(const std::string &s) {
    return {reinterpret_cast<const std::uint8_t *>(s.data()), s.size()};
}

LslValue raw(const network::Value &n) {
    const auto type = n.get("type").text;
    if (type == "raw_single")
        return LslRawSingle{
            std::bit_cast<float>(static_cast<std::uint32_t>(std::stoul(n.get("bits").text, nullptr, 16)))};
    if (type == "raw_boolean")
        return LslRawBoolean{n.get("value").boolean};
    check(type == "raw_uuid", "Unmeasured raw event carrier");
    return LslRawUuid{framework::Uuid(n.get("value").text)};
}

void value(const LslValue &a, const network::Value &e, const std::string &w) {
    const auto t = e.get("type").text;
    if (t.starts_with("raw_")) {
        auto expected = raw(e);
        check(a.index() == expected.index(), w + ": raw CLR identity differs");
        if (t == "raw_single")
            check(std::bit_cast<std::uint32_t>(std::get<LslRawSingle>(a).value) ==
                      std::bit_cast<std::uint32_t>(std::get<LslRawSingle>(expected).value),
                  w + ": Single bits differ");
        else if (t == "raw_boolean")
            check(std::get<LslRawBoolean>(a).value == std::get<LslRawBoolean>(expected).value, w + ": Boolean differs");
        else
            check(std::get<LslRawUuid>(a).value == std::get<LslRawUuid>(expected).value, w + ": UUID differs");
    } else if (t == "list") {
        auto p = std::get_if<std::shared_ptr<const LslList>>(&a);
        check(p && *p && (*p)->items.size() == e.get("value").array.size(), w + ": list differs");
        for (std::size_t i = 0; i < (*p)->items.size(); ++i)
            value((*p)->items[i], e.get("value").array[i], w + "[" + std::to_string(i) + "]");
    } else
        compare_value(a, e, w);
}

void globals(const LslCheckpoint &c, const std::string &s, const network::Value &e) {
    auto names = global_names(s);
    check(names.size() == c.globals.size() && names.size() == e.map.size(), "Global count differs");
    for (std::size_t i = 0; i < names.size(); ++i)
        value(c.globals[i], e.get(names[i]), names[i]);
}

void queue(const LslCheckpoint &c, const network::Value &e) {
    check(c.queued_events.size() == e.array.size(), "Queue count differs");
    for (std::size_t i = 0; i < e.array.size(); ++i) {
        auto &q = c.queued_events[i];
        auto &x = e.array[i];
        check(q.name == x.get("name").text && q.arguments.size() == x.get("arguments").array.size(),
              "Queue shape differs");
        for (std::size_t j = 0; j < q.arguments.size(); ++j)
            value(q.arguments[j], x.get("arguments").array[j], "queued payload");
    }
}

void fixture(const std::filesystem::path &root, const network::Value &o) {
    auto folder = root / o.get("name").text;
    auto source = read(folder / "source.lsl");
    auto compiled = compile_lsl(source);
    if (!o.get("accepted").boolean) {
        check(!compiled, "Original rejected handler signature accepted");
        return;
    }
    check(bool(compiled), "Original accepted source rejected");
    for (auto name :
         {"source.lsl", "original.yobj", "original.il.txt", "snapshot.bin", "event-queue.xml", "detect-array.xml"})
        check(framework::sha256_hex(read(folder / name)) == o.get("sha256").get(name).text,
              std::string("Artifact hash differs: ") + name);
    check(yengine_source_matches(source, o.get("source_hash").text), "Original token source identity differs");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int ms) {
        check(ms == 100, "Unexpected sleep duration");
        ++sleeps;
    };
    LslContext context;
    context.item = framework::Uuid::random();
    context.owner = framework::Uuid::random();
    auto native = std::make_unique<LslInstance>(compiled.program, context, host);
    const auto &input = o.get("initial").get("queue").array;
    check(input.size() == 1, "Original raw queue inventory differs");
    check(native->post_event({"touch_start", {raw(input[0].get("arguments").array[0])}, {}}),
          "Raw event admission rejected");
    bool sleeping = false;
    for (unsigned i = 0; i < 1000; ++i) {
        auto r = native->run(1);
        check(r.status != LslRunStatus::Faulted, "Initial raw event source faulted: " + r.error);
        auto c = native->checkpoint();
        check(bool(c), "Initial state missing");
        auto d = decode_lsl_checkpoint(encode_lsl_checkpoint(*c));
        auto fresh = std::make_unique<LslInstance>(compiled.program, context, host);
        check(d && fresh->restore_checkpoint(*d), "Initial budget1 continuation rejected");
        native = std::move(fresh);
        if (r.status == LslRunStatus::Sleeping) {
            sleeping = true;
            break;
        }
    }
    check(sleeping && sleeps == 1, "Original sleep boundary not reached exactly once");
    globals(*native->checkpoint(), source, o.get("initial").get("globals"));
    queue(*native->checkpoint(), o.get("initial").get("queue"));
    std::vector<std::unique_ptr<LslInstance>> paths;
    paths.push_back(std::move(native));
    auto p = paths[0]->persistent_checkpoint();
    check(bool(p), "Persistent state missing");
    auto d = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(*p));
    auto external = std::make_unique<LslInstance>(compiled.program, context, host);
    check(d && external->restore_checkpoint(*d), "Persistent raw state rejected");
    queue(*external->checkpoint(), o.get("after_load").get("queue"));
    paths.push_back(std::move(external));
    auto snapshot = read(folder / "snapshot.bin");
    auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + context.item.str() + "\" SourceHash=\"" +
               o.get("source_hash").text + "\"><Running>True</Running><Snapshot>" +
               network::binary_base64(network::osd_from_binary(
                   std::make_shared<std::vector<std::uint8_t>>(snapshot.begin(), snapshot.end()))) +
               "</Snapshot>" + read(folder / "detect-array.xml") + read(folder / "event-queue.xml") + "</ScriptState>";
    auto original = scene::read_yengine_state_xml(bytes(xml), context.item);
    auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
    auto imported = std::make_unique<LslInstance>(compiled.program, context, host);
    check(original && original->snapshot && object &&
              imported->restore_yengine_snapshot(*original->snapshot, original->source_hash, source, *object),
          "Original sleeping entry import rejected");
    imported->restore_queued_events(original->events);
    queue(*imported->checkpoint(), o.get("after_load").get("queue"));
    paths.push_back(std::move(imported));
    clock += 10000001;
    for (std::size_t path = 0; path < paths.size(); ++path) {
        auto &vm = paths[path];
        auto last = vm->checkpoint();
        const auto &exception = o.get(path == 0 ? "live_resume_exception" : "resume_exception");
        const auto &expected = o.get(path == 0 ? "live_resume" : "final");
        bool done = false;
        for (unsigned step = 0; step < 2000; ++step) {
            auto r = vm->run(1);
            if (r.status == LslRunStatus::Faulted) {
                check(exception.kind == network::Value::Kind::Map, "Successful raw case faulted: " + r.error);
                auto type = exception.get("type").text;
                check(type.starts_with("System."), "Unmeasured fault type");
                check(r.error == type.substr(7) + ": " + exception.get("message").text,
                      "Fault category/text differs: " + r.error);
                globals(*last, source, expected.get("globals"));
                done = true;
                break;
            }
            last = vm->checkpoint();
            check(bool(last), "Continuation state missing");
            auto wire = encode_lsl_checkpoint(*last);
            auto decoded = decode_lsl_checkpoint(wire);
            check(decoded && encode_lsl_checkpoint(*decoded) == wire, "Raw budget checkpoint changed bytes");
            auto fresh = std::make_unique<LslInstance>(compiled.program, context, host);
            check(fresh->restore_checkpoint(*decoded), "Raw budget fresh VM rejected");
            vm = std::move(fresh);
            if (r.status == LslRunStatus::Idle) {
                check(exception.kind != network::Value::Kind::Map, "Original raw fault became success");
                globals(*last, source, expected.get("globals"));
                check(vm->run(1).status == LslRunStatus::Idle && sleeps == 1, "Completed raw entry replayed");
                done = true;
                break;
            }
            check(r.status == LslRunStatus::BudgetExhausted, "Raw continuation slept/replayed API");
        }
        check(done && sleeps == 1, "Raw continuation bound/effect count differs");
    }
}

void binary_fixture(const std::filesystem::path &root, const network::Value &o) {
    const auto folder = root / o.get("name").text;
    const auto source = read(folder / "source.lsl");
    for (auto name :
         {"source.lsl", "original.yobj", "original.il.txt", "snapshot.bin", "event-queue.xml", "detect-array.xml"})
        check(framework::sha256_hex(read(folder / name)) == o.get("sha256").get(name).text,
              std::string("Binary proof hash differs: ") + name);
    check(o.get("accepted").boolean && o.get("migrated").boolean &&
              yengine_source_matches(source, o.get("source_hash").text),
          "Binary proof source/outcome differs");
    auto compiled = compile_lsl(source);
    check(bool(compiled), "Binary proof source rejected");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int ms) {
        check(ms == 100, "Binary proof sleep differs");
        ++sleeps;
    };
    LslContext context;
    context.item = framework::Uuid::random();
    auto inspect = [&](const LslCheckpoint &c) {
        globals(c, source, o.get("initial").get("globals"));
        auto result = std::get<std::shared_ptr<const LslList>>(c.globals[0]);
        check(result == std::get<std::shared_ptr<const LslList>>(c.globals[1]), "Binary global list alias lost");
        check(std::get<std::shared_ptr<const LslList>>(result->items[2]) ==
                  std::get<std::shared_ptr<const LslList>>(result->items[3]),
              "Binary nested alias lost");
        check(c.locals.size() >= 2, "Binary suspended hold arguments/locals missing");
        const auto &slots = o.get("initial").get("frames").array.back().get("slot_values").array;
        value(c.locals[0], slots[0], "hold argument");
        value(c.locals[1], slots[1], "hold local");
        check(std::get<std::shared_ptr<const LslList>>(c.locals[0]) == result &&
                  std::get<std::shared_ptr<const LslList>>(c.locals[1]) == result,
              "Binary global/frame aliases lost");
    };
    auto native = std::make_unique<LslInstance>(compiled.program, context, host);
    const auto &items = o.get("initial").get("globals").get("result").get("value").array;
    auto nested = std::make_shared<const LslList>(LslList{{raw(items[1]), LslBoxedFloat{2.5}}});
    check(native->post_event({"touch_start", {raw(items[1]), nested, nested}, {}}), "Binary source raw event rejected");
    bool stopped = false;
    for (unsigned i = 0; i < 2000; ++i) {
        auto r = native->run(1);
        check(r.status != LslRunStatus::Faulted, "Binary source fault: " + r.error);
        auto c = native->checkpoint();
        check(bool(c), "Binary native checkpoint missing");
        auto d = decode_lsl_checkpoint(encode_lsl_checkpoint(*c));
        auto fresh = std::make_unique<LslInstance>(compiled.program, context, host);
        check(d && fresh->restore_checkpoint(*d), "Binary native budget continuation rejected");
        native = std::move(fresh);
        if (r.status == LslRunStatus::Sleeping) {
            stopped = true;
            break;
        }
    }
    check(stopped && sleeps == 1, "Binary native source did not sleep once");
    inspect(*native->checkpoint());
    std::vector<std::unique_ptr<LslInstance>> paths;
    paths.push_back(std::move(native));
    auto projected = paths[0]->persistent_checkpoint();
    check(bool(projected), "Binary persistent projection missing");
    auto d = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(*projected));
    auto persistent = std::make_unique<LslInstance>(compiled.program, context, host);
    check(d && persistent->restore_checkpoint(*d), "Binary persistent restore rejected");
    inspect(*persistent->checkpoint());
    paths.push_back(std::move(persistent));
    auto snapshot = read(folder / "snapshot.bin");
    auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + context.item.str() + "\" SourceHash=\"" +
               o.get("source_hash").text + "\"><Running>True</Running><Snapshot>" +
               network::binary_base64(network::osd_from_binary(
                   std::make_shared<std::vector<std::uint8_t>>(snapshot.begin(), snapshot.end()))) +
               "</Snapshot>" + read(folder / "detect-array.xml") + read(folder / "event-queue.xml") + "</ScriptState>";
    auto original = scene::read_yengine_state_xml(bytes(xml), context.item);
    auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
    auto imported = std::make_unique<LslInstance>(compiled.program, context, host);
    check(original && original->snapshot && object &&
              imported->restore_yengine_snapshot(*original->snapshot, original->source_hash, source, *object),
          "Binary Single original import rejected");
    imported->restore_queued_events(original->events);
    inspect(*imported->checkpoint());
    paths.push_back(std::move(imported));
    clock += 10000001;
    for (auto &vm : paths) {
        bool done = false;
        for (unsigned i = 0; i < 2000; ++i) {
            auto r = vm->run(1);
            check(r.status != LslRunStatus::Faulted, "Binary Single resume fault: " + r.error);
            auto c = vm->checkpoint();
            check(bool(c), "Binary resume state missing");
            auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(*c));
            auto fresh = std::make_unique<LslInstance>(compiled.program, context, host);
            check(decoded && fresh->restore_checkpoint(*decoded), "Binary resume budget continuation rejected");
            vm = std::move(fresh);
            if (r.status == LslRunStatus::Idle) {
                globals(*c, source, o.get("final").get("globals"));
                check(vm->run(1).status == LslRunStatus::Idle && sleeps == 1, "Binary source effect replayed");
                done = true;
                break;
            }
            check(r.status == LslRunStatus::BudgetExhausted, "Binary import repeated sleep");
        }
        check(done, "Binary resume bound exceeded");
    }
}

} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Raw30 and binary Single3 fixture directories required");
        std::filesystem::path root = argv[1];
        auto rows = network::parse_osd_json(read(root / "metadata.json"));
        check(rows.array.size() == 30, "Raw30 inventory changed");
        unsigned accepted = 0, faults = 0;
        for (auto &o : rows.array) {
            accepted += o.get("accepted").boolean;
            faults += o.get("live_resume_exception").kind == network::Value::Kind::Map;
            try {
                fixture(root, o);
            } catch (const std::exception &e) {
                throw std::runtime_error(o.get("name").text + ": " + e.what());
            }
        }
        check(accepted == 24 && faults == 6, "Raw30 outcome counts differ");
        const std::filesystem::path binary_root = argv[2];
        const auto binary_rows = network::parse_osd_json(read(binary_root / "metadata.json"));
        check(binary_rows.array.size() == 3, "Binary Single proof count differs");
        for (const auto &row : binary_rows.array) {
            try {
                binary_fixture(binary_root, row);
            } catch (const std::exception &e) {
                throw std::runtime_error(row.get("name").text + ": " + e.what());
            }
        }
        std::cout
            << "PASS raw30:24 accepted/6 rejected,6 measured faults; live, persistent and original XML queue "
               "imports with budget1 checkpoints. Binary Single3 globals/frame aliases and exact IEEE bits verified.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
