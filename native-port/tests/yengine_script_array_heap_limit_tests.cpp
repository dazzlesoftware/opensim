// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include <array>
#include <iostream>
using namespace utf16_test;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

void constructor_source(const std::string &source, const network::Value &row) {
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Original heap-limit source rejected");
    LslHost host;
    const auto &state = row.get("state");
    host.heap_limit_bytes = network::osd_integer(state.get("heapLimit"));
    std::int64_t clock = 100000000;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    LslInstance instance(compiled.program, {}, host);
    const auto result = instance.run(10000);
    const auto &error = row.get("error");
    if (error.kind == network::Value::Kind::Map) {
        const auto message = error.get("message").text;
        check(error.get("type").text == "OpenSim.Region.ScriptEngine.Yengine.OutOfHeapException" &&
                  message.starts_with("(OWNER)"),
              "Original constructor fault changed");
        check(result.status == LslRunStatus::Faulted && result.error == "OutOfHeap: " + message.substr(7),
              "Constructor heap boundary differs: " + result.error);
        // Native faulted instances deliberately expose no checkpoint. The
        // original post-fault null globals are not asserted through that API.
        return;
    }
    check(result.status == LslRunStatus::Sleeping && result.error.empty(),
          "Two independent wrappers incorrectly aggregated against heap limit: " + result.error);
    const auto checkpoint = instance.checkpoint();
    check(checkpoint && checkpoint->globals.size() == 3 && checkpoint->arrays_heap_usage &&
              *checkpoint->arrays_heap_usage == network::osd_integer(state.get("globalUsage")) &&
              checkpoint->locals_heap_usage && *checkpoint->locals_heap_usage == 0,
          "Constructor global/local heap ledger differs");
    auto first = std::get<Class>(checkpoint->globals[0]);
    auto second = std::get<Class>(checkpoint->globals[1]);
    check(first && second && first != second && state.get("classesDistinct").boolean,
          "Distinct generated constructors merged wrappers");
    check(first->arrays_heap_usage == network::osd_integer(state.get("classes").array[0].get("ownUsage")) &&
              second->arrays_heap_usage == network::osd_integer(state.get("classes").array[1].get("ownUsage")),
          "Independent class own ledgers differ");
}

void replacement_backend(const std::vector<network::Value> &rows) {
    // These three comparisons exercise the native carrier/store helper with
    // the measured original UpdateArraysHeapUse rule. They do not claim the
    // source runtime can change its copied host limit while suspended.
    const std::array dimensions{std::int32_t{3}};
    auto descriptor = make_lsl_script_array_descriptor("heap-limit", "integer", 1);
    auto value = std::get<Class>(make_lsl_script_array(descriptor, dimensions));
    auto second = std::get<Class>(make_lsl_script_array(descriptor, dimensions));
    for (std::size_t i = 4; i < rows.size(); ++i) {
        const auto &row = rows[i];
        const auto &expected = row.get("state");
        const auto global = network::osd_integer(expected.get("globalUsage"));
        const auto limit = network::osd_integer(expected.get("heapLimit"));
        const auto length = i == 4 ? 5u : i == 5 ? 4u : 0u;
        auto next = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Integer, length));
        auto old = std::get<Array>(lsl_script_class_field(value, 5, 0));
        const auto old_usage = value->arrays_heap_usage;
        LslScriptClassHeapHooks hooks;
        hooks.check_delta = [=](std::int32_t delta) {
            if (global + delta > limit)
                throw std::runtime_error("(OWNER)oldtotal=" + std::to_string(global) + ", newtotal=" +
                                         std::to_string(global + delta) + ", limit=" + std::to_string(limit));
        };
        std::string error;
        try {
            lsl_script_class_store_field(value, 5, 0, next, hooks);
        } catch (const std::exception &failure) {
            error = failure.what();
        }
        const auto &original_error = row.get("error");
        check(error == original_error.get("message").text, "Backend replacement heap error differs");
        auto stored = std::get<Array>(lsl_script_class_field(value, 5, 0));
        check((stored == old) == row.get("oldPreserved").boolean && (stored == next) == row.get("newStored").boolean,
              "Backend rejection/acceptance field mutation differs");
        check(value->arrays_heap_usage == network::osd_integer(expected.get("classes").array[0].get("ownUsage")) &&
                  second->arrays_heap_usage == network::osd_integer(expected.get("classes").array[1].get("ownUsage")),
              "Backend class own ledger differs");
        if (!error.empty())
            check(value->arrays_heap_usage == old_usage, "Rejected backend store mutated ledger");
    }
}

void fixture(const std::filesystem::path &root) {
    const auto proof_text = read(root / "verification.json");
    check(framework::sha256_hex(proof_text) == "1e71eb37ca71eae1f2293062dc89ba5c6e3a5beb1fff5e9ab6cb0520518f665f",
          "Original heap-limit proof changed");
    const auto proof = network::parse_osd_json(proof_text);
    for (const auto platform : {"windows", "linux"}) {
        const auto folder = root / platform;
        const auto &expected = proof.get("platforms").get(platform);
        const auto source = read(folder / "source.lsl");
        const auto observations = read(folder / "heap-limit-observations.json");
        check(framework::sha256_hex(source) == expected.get("sourceSha256").text &&
                  framework::sha256_hex(observations) == expected.get("observationsSha256").text &&
                  framework::sha256_hex(read(folder / "metadata.json")) == expected.get("metadataSha256").text,
              "Original heap-limit artifacts changed");
        const auto rows = network::parse_osd_json(observations).array;
        check(rows.size() == 7, "Original heap-limit case count changed");
        for (unsigned i = 0; i < 4; ++i)
            constructor_source(source, rows[i]);
        replacement_backend(rows);
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original heap-limit fixture directory");
        fixture(argv[1]);
        collect_lsl_fixed_arrays();
        std::cout
            << "Matched four source constructor and three backend replacement heap-limit cases per original OS.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
