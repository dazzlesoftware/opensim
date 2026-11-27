// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include <iostream>
#include <memory>
using namespace utf16_test;

namespace {
LslValue original_value(const network::Value &node) {
    const auto type = node.get("type").text, clr = node.get("clr").text;
    const auto &value = node.get("value");
    if (type == "integer")
        return clr.ends_with("+LSLInteger") ? LslValue(LslBoxedInteger{network::osd_integer(value)})
                                            : LslValue(network::osd_integer(value));
    if (type == "float")
        return clr.ends_with("+LSLFloat") ? LslValue(LslBoxedFloat{network::osd_real(value)})
                                          : LslValue(network::osd_real(value));
    if (type == "string") {
        check(node.get("utf16").kind == network::Value::Kind::Array, "Original name/UUID units missing");
        std::u16string units;
        for (const auto &unit : node.get("utf16").array)
            units.push_back(static_cast<char16_t>(network::osd_integer(unit)));
        return clr.ends_with("+LSLString") ? LslValue(LslBoxedUtf16String{units}) : LslValue(LslUtf16String{units});
    }
    if (type == "vector")
        return framework::Vector3d{network::osd_real(value.array.at(0)), network::osd_real(value.array.at(1)),
                                   network::osd_real(value.array.at(2))};
    if (type == "rotation")
        return LslRotation{network::osd_real(value.array.at(0)), network::osd_real(value.array.at(1)),
                           network::osd_real(value.array.at(2)), network::osd_real(value.array.at(3))};
    throw std::runtime_error("Unreviewed genuine detector schema value " + type);
}

std::u16string detector_name(const LslDetector &detector) {
    if (detector.sdk_name)
        return *detector.sdk_name;
    const auto units = lsl_string_code_units(LslValue(detector.name));
    check(units.has_value(), "Detector SDK name missing");
    return *units;
}

void fixture(const std::filesystem::path &root, const network::Value &oracle) {
    const auto folder = root / oracle.get("name").text;
    const auto source = read(folder / "source.lsl");
    check(oracle.get("accepted").boolean && oracle.get("initial_exception").kind != network::Value::Kind::Map &&
              oracle.get("live_resume_exception").kind != network::Value::Kind::Map,
          "Original live detector source did not succeed");
    check(framework::sha256_hex(source) == oracle.get("sha256").get("source.lsl").text &&
              yengine_source_matches(source, oracle.get("source_hash").text),
          "Original detector source identity differs");
    for (const auto *name : {"original.yobj", "original.il.txt"})
        check(framework::sha256_hex(read(folder / name)) == oracle.get("sha256").get(name).text,
              "Original detector compiler artifact differs");
    // Raw binary success is distinct from complete DetectArray XML transfer.
    // The genuine original explicitly fails XML serialization for lone names.
    const bool xml_failure =
        oracle.get("name").text == "high" || oracle.get("name").text == "low" || oracle.get("name").text == "mixed";
    check(oracle.get("migrated").boolean && oracle.get("production_write_exception").kind != network::Value::Kind::Map,
          "Original raw string snapshot control changed");
    check(oracle.get("detect_xml_context_restored").boolean == !xml_failure,
          "Original full DetectArray transfer control changed");
    check(xml_failure ? oracle.get("detect_xml_write_exception").get("type").text == "System.ArgumentException"
                      : oracle.get("detect_xml_write_exception").kind != network::Value::Kind::Map,
          "Original DetectArray XML boundary category changed");
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Actual detector source rejected");
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    auto instance = std::make_unique<LslInstance>(compiled.program, LslContext{}, host, false);
    // The actual original source starts with one rich detection. Its observed
    // saved schema is a fixed point of LoadDets except for the source-replaced
    // name, so seed this genuine schema through the public conversion/event APIs.
    std::vector<LslValue> original_schema;
    for (const auto &value : oracle.get("initial").get("globals").get("saved").get("value").array)
        original_schema.push_back(original_value(value));
    auto detectors = load_lsl_detector_values(original_schema);
    check(detectors.size() == 1, "Original source did not provide exactly one rich detector");
    instance->running(false);
    check(instance->post_event({"state_entry", {}, std::move(detectors)}),
          "Original state-entry detection was not admitted");
    check(instance->queued_events() == 1, "Original state-entry detection was ignored rather than queued");
    instance->running(true);
    bool slept = false, saw15 = false;
    for (unsigned step = 0; step < 3000; ++step) {
        const auto result = instance->run(1);
        check(result.status != LslRunStatus::Faulted, "Live detector source faulted: " + result.error);
        const auto current = instance->checkpoint();
        check(current.has_value(), "Detector checkpoint unavailable");
        const auto wire = encode_lsl_checkpoint(*current);
        saw15 |= wire.size() > 3 && wire[3] == 15;
        const auto decoded = decode_lsl_checkpoint(wire);
        check(decoded.has_value(), "Detector checkpoint codec failed");
        for (std::size_t i = 0; i < current->globals.size(); ++i)
            compare_checkpoint_strings(current->globals[i], decoded->globals.at(i));
        if (current->active_event) {
            check(decoded->active_event &&
                      current->active_event->detectors.size() == decoded->active_event->detectors.size(),
                  "Detector event context changed");
            for (std::size_t i = 0; i < current->active_event->detectors.size(); ++i)
                check(detector_name(current->active_event->detectors[i]) ==
                          detector_name(decoded->active_event->detectors[i]),
                      "Internal codec changed exact detector name units");
        }
        auto fresh = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh VM rejected live detector checkpoint");
        instance = std::move(fresh);
        if (result.status == LslRunStatus::Sleeping) {
            check(!slept, "Live detector API sleep replayed");
            slept = true;
            compare_globals(*current, global_names(source), oracle.get("initial").get("globals"));
            clock += current->sleep_remaining + 1;
        }
        if (result.status == LslRunStatus::Idle) {
            check(slept && sleeps == 1, "Live detector sleep count differs: slept=" + std::to_string(slept) +
                                            " requests=" + std::to_string(sleeps));
            check(!xml_failure || saw15, "Unpaired detector context never used lossless checkpoint15");
            compare_globals(*current, global_names(source), oracle.get("live_resume").get("globals"));
            check(instance->run(1).status == LslRunStatus::Idle, "Completed detector source replayed");
            return;
        }
    }
    throw std::runtime_error("Live detector source exceeded bounded execution");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Original UTF16 detector fixture directory required");
        const std::filesystem::path root = argv[1];
        const auto cases = network::parse_osd_json(read(root / "metadata.json"));
        check(cases.array.size() == 5, "Original detector inventory changed");
        std::vector<std::string> failures;
        for (const auto &oracle : cases.array)
            try {
                fixture(root, oracle);
            } catch (const std::exception &error) {
                failures.push_back(oracle.get("name").text + ": " + error.what());
            }
        for (const auto &failure : failures)
            std::cerr << failure << '\n';
        check(failures.empty(), "Original detector live source checks failed");
        std::cout << "Five actual detector sources preserve live units; XML failure controls remain separate.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
