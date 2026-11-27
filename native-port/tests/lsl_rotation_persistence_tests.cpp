// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include <array>
#include <algorithm>
#include <bit>
#include <functional>
#include <iostream>
using namespace utf16_test;

namespace {
using Bits = std::array<std::uint64_t, 4>;
std::vector<std::pair<Bits, LslRotation>> queue_witnesses;

Bits bits(const LslRotation &r) {
    return {std::bit_cast<std::uint64_t>(r.x), std::bit_cast<std::uint64_t>(r.y), std::bit_cast<std::uint64_t>(r.z),
            std::bit_cast<std::uint64_t>(r.w)};
}

LslRotation rotation(const network::Value &node) {
    check(node.get("type").text == "rotation" && node.get("value").array.size() == 4,
          "Original Rotation witness type differs");
    std::array<double, 4> values;
    for (unsigned i = 0; i < 4; ++i)
        values[i] = std::bit_cast<double>(std::stoull(node.get("value").array[i].get("bits").text, nullptr, 16));
    return {values[0], values[1], values[2], values[3]};
}

struct Scenario {
    LslCheckpoint checkpoint;
    std::function<LslValue(const LslCheckpoint &)> get;
};

Scenario scenario(unsigned root, LslRotation value) {
    Scenario s;
    auto &c = s.checkpoint;
    c.program_hash = "rotation-persistence";
    c.state = "default";
    c.globals = {std::int32_t{1}};
    c.initialized_globals = 1;
    c.global_initialization_pending = false;
    c.active_event = LslEvent{"state_entry", {}, {}};
    LslCallFrameCheckpoint f;
    f.function = "caller";
    LslEventContextCheckpoint e;
    e.active_event = {"touch_start", {}, {}};
    e.event_wait = LslEventWaitCheckpoint{9, 0, 0, 0, 1, 0};
    LslCapturePointCheckpoint p;
    p.evaluations = {{1, value}};
    LslResumeFrameCheckpoint r;
    r.method = "worker";
    r.complete_restore = true;
    switch (root) {
    case 0:
        c.globals = {value};
        s.get = [](const auto &x) { return x.globals.at(0); };
        break;
    case 1:
        c.locals = {value};
        s.get = [](const auto &x) { return x.locals.at(0); };
        break;
    case 2: {
        auto child = std::make_shared<const LslList>(LslList{{value}});
        auto parent = std::make_shared<const LslList>(LslList{{child, child}});
        c.globals = {parent, parent};
        s.get = [](const auto &x) {
            auto a = std::get<std::shared_ptr<const LslList>>(x.globals.at(0));
            auto b = std::get<std::shared_ptr<const LslList>>(x.globals.at(1));
            check(a == b, "Outer DAG alias lost");
            auto u = std::get<std::shared_ptr<const LslList>>(a->items.at(0));
            auto v = std::get<std::shared_ptr<const LslList>>(a->items.at(1));
            check(u == v, "Inner DAG alias lost");
            return u->items.at(0);
        };
        break;
    }
    case 3:
        c.active_event->arguments = {value};
        s.get = [](const auto &x) { return x.active_event->arguments.at(0); };
        break;
    case 4:
        c.queued_events = {{"changed", {value}, {}}};
        s.get = [](const auto &x) { return x.queued_events.at(0).arguments.at(0); };
        break;
    case 5:
        f.locals = {value};
        c.call_stack = {f};
        s.get = [](const auto &x) { return x.call_stack.at(0).locals.at(0); };
        break;
    case 6:
        f.evaluations = {{1, value}};
        c.call_stack = {f};
        s.get = [](const auto &x) { return x.call_stack.at(0).evaluations.at(0).value; };
        break;
    case 7:
        c.evaluations = {{1, value}};
        s.get = [](const auto &x) { return x.evaluations.at(0).value; };
        break;
    case 8:
        c.capture_point = p;
        s.get = [](const auto &x) { return x.capture_point->evaluations.at(0).value; };
        break;
    case 9:
        e.locals = {value};
        c.event_contexts = {e};
        s.get = [](const auto &x) { return x.event_contexts.at(0).locals.at(0); };
        break;
    case 10:
        e.active_event.arguments = {value};
        c.event_contexts = {e};
        s.get = [](const auto &x) { return x.event_contexts.at(0).active_event.arguments.at(0); };
        break;
    case 11:
        e.evaluations = {{1, value}};
        c.event_contexts = {e};
        s.get = [](const auto &x) { return x.event_contexts.at(0).evaluations.at(0).value; };
        break;
    case 12:
        f.locals = {value};
        e.call_stack = {f};
        c.event_contexts = {e};
        s.get = [](const auto &x) { return x.event_contexts.at(0).call_stack.at(0).locals.at(0); };
        break;
    case 13:
        e.capture_point = p;
        c.event_contexts = {e};
        s.get = [](const auto &x) { return x.event_contexts.at(0).capture_point->evaluations.at(0).value; };
        break;
    case 14:
        r.locals = {value};
        c.resume_frames = {r};
        s.get = [](const auto &x) { return x.resume_frames.at(0).locals.at(0); };
        break;
    case 15:
        r.evaluations = {{1, value}};
        c.resume_frames = {r};
        s.get = [](const auto &x) { return x.resume_frames.at(0).evaluations.at(0).value; };
        break;
    case 16:
        r.method = "xmrEventDequeue";
        r.complete_restore = false;
        r.event_wait = e.event_wait;
        r.background_event = LslEvent{"touch_end", {value}, {}};
        c.resume_frames = {r};
        s.get = [](const auto &x) { return x.resume_frames.at(0).background_event->arguments.at(0); };
        break;
    case 17:
        e.kind = LslEventContextKind::DirectHandler;
        e.event_wait.reset();
        e.direct_call_expression = 1;
        e.invoked_event = LslEvent{"touch_end", {value}, {}};
        c.event_contexts = {e};
        s.get = [](const auto &x) { return x.event_contexts.at(0).invoked_event->arguments.at(0); };
        break;
    default:
        throw std::runtime_error("Unknown Rotation root");
    }
    LslDetector detector;
    detector.rotation = value;
    c.active_event->detectors = {detector};
    return s;
}

void verify(LslRotation original, LslRotation expected, std::optional<LslRotation> detector_expected = {}) {
    for (unsigned root = 0; root < 18; ++root) {
        auto s = scenario(root, original);
        const auto before = encode_lsl_checkpoint(s.checkpoint);
        auto plain = decode_lsl_checkpoint(before);
        check(plain && bits(std::get<LslRotation>(s.get(*plain))) == bits(original), "Internal Rotation bits changed");
        auto wire = encode_persistent_lsl_checkpoint(s.checkpoint);
        auto saved = decode_lsl_checkpoint(wire);
        auto expected_value = expected;
        if (root == 4) {
            // Queued arguments use Quaternion.ToString + XML restore, separately
            // from the binary value constructor's all-zero normalization.
            const auto found = std::find_if(queue_witnesses.begin(), queue_witnesses.end(),
                                            [&](const auto &entry) { return entry.first == bits(original); });
            check(found != queue_witnesses.end(), "Missing exact original queued Rotation witness");
            expected_value = found->second;
        }
        check(saved && bits(std::get<LslRotation>(s.get(*saved))) == bits(expected_value),
              "Persistent Rotation bits differ");
        // A raw instruction-budget checkpoint with nested contexts is not a
        // canonical source capture. Only the VM persistent_checkpoint() API can
        // assign its active detector's original XML/binary role. The codec
        // leaves those context caches untouched, while canonical root captures
        // pass the active detector through the original XML boundary.
        const auto expected_detector =
            s.checkpoint.event_contexts.empty() ? detector_expected.value_or(original) : original;
        check(bits(saved->active_event->detectors.at(0).rotation) == bits(expected_detector),
              "Detector Rotation normalized at the wrong API boundary");
        check(encode_lsl_checkpoint(s.checkpoint) == before, "Persistent writer mutated input");
        check(encode_persistent_lsl_checkpoint(*saved) == wire, "Persistent Rotation encoding is not stable");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 3, "Expected genuine signed-zero and XML numeric fixture directories");
        auto root = std::filesystem::path(argv[1]);
        auto load_queue_witnesses = [&](const std::filesystem::path &queue_root, const std::filesystem::path &reference,
                                        const char *expected_sha, std::size_t expected_count) {
            const auto queue_text = read(queue_root / "metadata.json");
            check(framework::sha256_hex(queue_text) == expected_sha, "Original queued Rotation metadata changed");
            const auto queue_proof = network::parse_osd_json(queue_text);
            check(queue_proof.get("cases").array.size() == expected_count,
                  "Original queued Rotation inventory differs");
            check(framework::sha256_hex(read(reference)) == queue_proof.get("generatorSha256").text,
                  "Original queued Rotation generator changed");
            for (const auto &row : queue_proof.get("cases").array) {
                for (auto stage :
                     {"append_exception", "outer_xml_exception", "load_xml_exception", "restore_event_queue_exception"})
                    check((row.get(stage).kind == network::Value::Kind::Undefined ||
                           row.get(stage).kind == network::Value::Kind::NullReference),
                          "Original queued Rotation stage faulted");
                check(framework::sha256_hex(read(queue_root / row.get("name").text / "serialized.xml")) ==
                          row.get("sha256").get("serialized.xml").text,
                      "Original queued Rotation XML changed");
                auto decode_bits = [](const network::Value &node) {
                    check(node.get("bits").array.size() == 4, "Original queued Rotation component count differs");
                    std::array<double, 4> v;
                    for (unsigned i = 0; i < 4; ++i)
                        v[i] = std::bit_cast<double>(std::stoull(node.get("bits").array[i].text, nullptr, 16));
                    return LslRotation{v[0], v[1], v[2], v[3]};
                };
                queue_witnesses.emplace_back(bits(decode_bits(row.get("input"))), decode_bits(row.get("restored")));
            }
        };
        load_queue_witnesses(root.parent_path() / "yengine_event_queue_rotation_bits_fixture",
                             root.parent_path() / "yengine_event_queue_rotation_bits_reference.cs",
                             "bb3410b2416fa17b42cae6fc0730ab133a74f164f75774020b7b2bfeff1a4d36", 5);
        load_queue_witnesses(root.parent_path() / "yengine_event_queue_local_rotation_bits_fixture",
                             root.parent_path() / "yengine_event_queue_local_rotation_bits_reference.cs",
                             "a7046a2129f1e08e026ddbdc7da542beda188d04a5dfd151635dd51082783bab", 1);
        auto rows = network::parse_osd_json(read(root / "metadata.json"));
        check(rows.array.size() == 3, "Original signed-zero case count differs");
        for (const auto &row : rows.array) {
            check(row.get("accepted").boolean && row.get("migrated").boolean, "Original migration failed");
            auto folder = root / row.get("name").text;
            for (const auto *name : {"source.lsl", "original.yobj", "original.il.txt", "snapshot.bin"})
                check(framework::sha256_hex(read(folder / name)) == row.get("sha256").get(name).text,
                      "Original migration artifact changed");
        }
        const auto &witness = rows.array.at(0);
        verify(rotation(witness.get("initial").get("globals").get("r")),
               rotation(witness.get("after_load").get("globals").get("r")));
        // The captured local reaches copied only after the source resumes. Its
        // live result is the actual pre-migration local, not the initial global.
        const auto &local = rows.array.at(1);
        verify(rotation(local.get("live_resume").get("globals").get("copied")),
               rotation(local.get("final").get("globals").get("copied")));
        const auto &control = rows.array.at(2);
        verify(rotation(control.get("initial").get("globals").get("r")),
               rotation(control.get("after_load").get("globals").get("r")));
        LslRotation nan{std::bit_cast<double>(std::uint64_t{0x7ff8000000000042}), 0., -0., -0.};
        const auto numeric_text = read(std::filesystem::path(argv[2]) / "metadata.json");
        check(framework::sha256_hex(numeric_text) == "7822d425db6f2e4a3a2538725cefda2e09e61329a40f0e624a3d316c4599bf77",
              "Original XML numeric metadata changed");
        const auto numeric = network::parse_osd_json(numeric_text);
        const auto &nan_case = numeric.array.at(5);
        check(nan_case.get("name").text == "nan_payload", "Original NaN witness differs");
        const auto &components = nan_case.get("after_load").get("detectors").array.at(0).get("rotation").array;
        auto detector_nan = nan;
        detector_nan.x = std::bit_cast<double>(std::stoull(components.at(0).get("bits").text, nullptr, 16));
        verify(nan, nan, detector_nan);
        std::cout << "Rotation persistent witness/control coverage passed across18 roots\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
