// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>

using namespace opensim;
using namespace framework;
using namespace script;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Missing original heap/compare fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

auto bytes(const std::string &value) {
    return std::span(reinterpret_cast<const std::uint8_t *>(value.data()), value.size());
}

auto compile(const std::string &source) {
    const auto result = compile_lsl(source);
    std::string errors;
    for (const auto &error : result.diagnostics)
        errors += " (" + std::to_string(error.line) + "," + std::to_string(error.column) + ") " + error.message;
    check(bool(result), "Original heap/compare source did not compile:" + errors);
    return result.program;
}

std::vector<std::string> global_names(std::string source) {
    // Ignore function/event bodies and quoted text rather than treating a
    // function's local marker as an additional global declaration.
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (char &ch : source) {
        const char original = ch;
        if (quoted) {
            ch = ' ';
            if (escaped)
                escaped = false;
            else if (original == '\\')
                escaped = true;
            else if (original == '"')
                quoted = false;
            continue;
        }
        if (original == '"') {
            quoted = true;
            ch = ' ';
        } else if (original == '{') {
            ++depth;
            ch = ' ';
        } else if (original == '}') {
            --depth;
            ch = ' ';
        } else if (depth)
            ch = ' ';
    }
    const std::regex declaration(
        R"(\b(?:integer|float|string|key|vector|rotation|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> result;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        result.push_back((*it)[1].str());
    return result;
}

void compare_value(const LslValue &actual, const network::Value &expected) {
    const auto value = unbox_lsl_value(actual);
    const auto type = expected.get("type").text;
    const auto &oracle = expected.get("value");
    const auto clr = expected.get("clr").text;
    if (clr.ends_with("+LSLInteger"))
        check(std::holds_alternative<LslBoxedInteger>(actual), "Original heap/compare lost boxed integer identity");
    if (clr.ends_with("+LSLString"))
        check(std::holds_alternative<LslBoxedString>(actual), "Original heap/compare lost boxed string identity");
    if (clr.ends_with("+LSLFloat"))
        check(std::holds_alternative<LslBoxedFloat>(actual), "Original heap/compare lost boxed float identity");
    if (clr.ends_with("+key"))
        check(std::holds_alternative<LslBoxedKey>(actual), "Original heap/compare lost true CLR key identity");
    if (clr == "System.Int32")
        check(std::holds_alternative<std::int32_t>(actual), "Original raw integer was reboxed");
    if (clr == "System.Double")
        check(std::holds_alternative<double>(actual), "Original raw double was reboxed");
    if (clr == "System.String")
        check(std::holds_alternative<std::string>(actual), "Original raw string was reboxed");
    if (type == "integer")
        check(std::get<std::int32_t>(value) == network::osd_integer(oracle),
              "Heap/compare integer differs from original: actual=" + std::to_string(std::get<std::int32_t>(value)) +
                  " expected=" + std::to_string(network::osd_integer(oracle)));
    else if (type == "string" || type == "key")
        check(std::get<std::string>(value) == oracle.text, "Heap/compare string differs from original");
    else if (type == "list") {
        const auto &list = std::get<std::shared_ptr<const LslList>>(value);
        check(list && list->items.size() == oracle.array.size(),
              "Heap/compare list length differs from original: actual=" +
                  std::to_string(list ? list->items.size() : 0) + " expected=" + std::to_string(oracle.array.size()));
        for (std::size_t i = 0; i < list->items.size(); ++i)
            compare_value(list->items[i], oracle.array[i]);
    } else if (type == "vector") {
        const auto v = std::get<Vector3d>(value);
        check(v.x == network::osd_real(oracle.array.at(0)) && v.y == network::osd_real(oracle.array.at(1)) &&
                  v.z == network::osd_real(oracle.array.at(2)),
              "Vector differs from original");
    } else if (type == "rotation") {
        const auto v = std::get<LslRotation>(value);
        check(v.x == network::osd_real(oracle.array.at(0)) && v.y == network::osd_real(oracle.array.at(1)) &&
                  v.z == network::osd_real(oracle.array.at(2)) && v.w == network::osd_real(oracle.array.at(3)),
              "Rotation differs from original");
    } else if (type == "float")
        check(std::get<double>(value) == network::osd_real(oracle), "Float differs from original");
    else
        throw std::runtime_error("Unsupported heap/compare oracle type: " + type);
}

LslCheckpoint snapshot(LslInstance &instance) {
    const auto cp = instance.checkpoint();
    check(cp.has_value(), "Heap/compare checkpoint unavailable");
    return *cp;
}

std::int32_t integer_global(const LslCheckpoint &cp, const std::vector<std::string> &names, std::string_view name) {
    const auto found = std::ranges::find(names, name);
    check(found != names.end(), "Heap source omitted " + std::string(name));
    const auto *value = std::get_if<std::int32_t>(&cp.globals.at(static_cast<std::size_t>(found - names.begin())));
    check(value != nullptr, "Heap observation is not plain Int32");
    return *value;
}

void compare_globals(const LslCheckpoint &cp, const std::string &source, const network::Value &oracle, bool heap,
                     std::int32_t limit) {
    const auto names = global_names(source);
    check(names.size() == cp.globals.size() && names.size() == oracle.map.size(),
          "Original source global scope changed");
    for (std::size_t i = 0; i < names.size(); ++i) {
        // These four original byte observations depend on CLR allocation and
        // liveness. The acceptance contract is the aliases' same-state identity,
        // not an unsupported absolute native-versus-CLR byte-count comparison.
        if (heap && (names[i] == "aliasLeft" || names[i] == "intrinsicLeft" || names[i] == "aliasUsed" ||
                     names[i] == "intrinsicUsed"))
            continue;
        const auto found = oracle.map.find(names[i]);
        check(found != oracle.map.end(), "Original globals omitted " + names[i]);
        try {
            compare_value(cp.globals[i], found->second);
        } catch (const std::exception &error) {
            throw std::runtime_error(names[i] + ": " + error.what());
        }
    }
    if (heap) {
        const auto left = integer_global(cp, names, "aliasLeft"), used = integer_global(cp, names, "aliasUsed");
        check(left == integer_global(cp, names, "intrinsicLeft"),
              "xmrHeapLeft differs from llGetFreeMemory at same source state");
        check(used == integer_global(cp, names, "intrinsicUsed"),
              "xmrHeapUsed differs from llGetUsedMemory at same source state");
        check(static_cast<std::int64_t>(left) + used == limit, "Heap aliases do not sum to the configured limit");
    }
}

LslRunResult drive(LslInstance &instance, std::int64_t &clock, bool stop_sleep,
                   const std::function<void(LslInstance &)> &roundtrip) {
    for (unsigned n = 0; n < 20000; ++n) {
        const auto result = instance.run(1);
        check(result.status != LslRunStatus::Faulted, "Heap/compare source faulted: " + result.error);
        if (result.status == LslRunStatus::Idle)
            return result;
        if (result.status == LslRunStatus::Sleeping) {
            if (stop_sleep)
                return result;
            const auto cp = snapshot(instance);
            check(!cp.event_wait, "Finite original heap source unexpectedly waits for a new event");
            clock += cp.sleep_remaining + 1;
        } else if (result.status == LslRunStatus::BudgetExhausted)
            roundtrip(instance);
    }
    throw std::runtime_error("Heap/compare source exceeded instruction bound");
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto name = oracle.get("name").text;
    const auto source = read(root / name / "source.lsl");
    check(oracle.get("accepted").boolean, "Original heap/compare source did not compile");
    check(yengine_source_matches(source, oracle.get("source_hash").text),
          "Original heap/compare source provenance changed");
    check(oracle.get("production_culture").text.empty(), "Original comparison did not use invariant culture");
    const auto program = compile(source);
    const bool heap = oracle.get("kind").text == "heap";
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.heap_limit_bytes = network::osd_integer(oracle.get("configured_heap_limit"));
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    check(host.heap_limit_bytes == 65536, "Original configured heap-limit scope changed");
    LslContext context;
    context.object = Uuid::random();
    context.item = Uuid::random();
    const auto roundtrip = [&](LslInstance &instance) {
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(snapshot(instance)));
        LslInstance restored(program, context, host);
        check(decoded && restored.restore_checkpoint(*decoded), "Internal budget checkpoint did not restore");
        instance = std::move(restored);
    };
    LslInstance instance(program, context, host);
    const bool captured = oracle.get("suspended").boolean;
    const auto boundary = drive(instance, clock, captured, roundtrip);
    if (captured) {
        check(boundary.status == LslRunStatus::Sleeping && oracle.get("migrated").boolean,
              "Original heap sleep capture did not suspend");
        compare_globals(snapshot(instance), source, oracle.get("initial").get("globals"), heap, host.heap_limit_bytes);
        const auto wire = encode_lsl_checkpoint(snapshot(instance));
        const auto decoded = decode_lsl_checkpoint(wire);
        LslInstance fresh(program, context, host);
        check(decoded && fresh.restore_checkpoint(*decoded), "Active heap context failed fresh-VM restore");
        const auto requests_before = sleeps;
        for (auto *vm : {&instance, &fresh}) {
            drive(*vm, clock, false, roundtrip);
            compare_globals(snapshot(*vm), source, oracle.get("final").get("globals"), heap, host.heap_limit_bytes);
        }
        check(sleeps == requests_before, "Heap continuation replayed a completed sleep API");
    } else {
        check(boundary.status == LslRunStatus::Idle, "Pure original comparison/heap source suspended");
        compare_globals(snapshot(instance), source, oracle.get("final").get("globals"), heap, host.heap_limit_bytes);
        check(sleeps == 0, "Pure comparison/heap source requested sleep");
    }
    if (oracle.get("kind").text == "compare") {
        const auto names = global_names(source);
        const auto result = integer_global(snapshot(instance), names, "result");
        check(result == network::osd_integer(oracle.get("direct_comparison").get("value")),
              "Exact signed original string.Compare return differs");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original heap/compare fixture directory required");
        const std::filesystem::path root = argv[1];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 43, "Original heap/compare inventory changed");
        unsigned comparisons = 0, order = 0, heap = 0, captured = 0;
        for (const auto &oracle : cases.array) {
            try {
                fixture(root, oracle);
            } catch (const std::exception &error) {
                throw std::runtime_error(oracle.get("name").text + ": " + error.what());
            }
            const auto kind = oracle.get("kind").text;
            if (kind == "compare")
                ++comparisons;
            else if (kind == "heap")
                ++heap;
            else {
                check(kind == "source", "Unreviewed original oracle kind");
                ++order;
            }
            if (oracle.get("suspended").boolean)
                ++captured;
        }
        check(comparisons == 34 && order == 1 && heap == 8 && captured == 4, "Original heap/compare coverage changed");
        std::cout << "PASS: 34 exact invariant comparisons, left-to-right argument effects, 8 heap-alias stages (4 "
                     "resumed contexts); absolute CLR byte counts excluded\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
