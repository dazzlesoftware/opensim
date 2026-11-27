// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/dotnet_string_hash.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/script/yengine_object_code.hpp"
#include <array>
#include <iostream>
#include <memory>

using namespace utf16_test;

namespace {
std::span<const std::uint8_t> bytes(const std::string &value) {
    return {reinterpret_cast<const std::uint8_t *>(value.data()), value.size()};
}

std::u16string units(const network::Value &value) {
    check(value.get("type").text == "string", "Original hash input is not a String");
    std::u16string result;
    for (const auto &unit : value.get("utf16").array)
        result.push_back(static_cast<char16_t>(network::osd_integer(unit)));
    return result;
}

std::int32_t hash(std::u16string_view value) {
    return std::bit_cast<std::int32_t>(framework::dotnet_string_hash(value));
}

network::Value expected_globals(const network::Value &original, std::int32_t expected_hash, bool before_hash) {
    auto result = original;
    result.map.at("result").map.at("value").integer = expected_hash;
    if (before_hash)
        result.map.at("before_hash").map.at("value").integer = expected_hash;
    return result;
}

void fixture(const std::filesystem::path &root, const network::Value &row) {
    const auto folder = root / row.get("name").text;
    // The separate original import process has already completed ResumeEx at
    // its initial observation. Its input capture belongs to process_a; it did
    // not produce a second sleeping capture or an initial result of zero.
    const auto capture_folder =
        row.get("imported_capture").boolean ? root.parent_path() / "process_a" / row.get("name").text : folder;
    auto capture = row;
    if (row.get("imported_capture").boolean) {
        const auto metadata = read(root.parent_path() / "process_a" / "metadata.json");
        check(framework::sha256_hex(metadata) == "092254ba0e387353a3c4cea2448dad4298cea585ebd4156e188b08657d8e4939",
              "Original input capture metadata changed");
        const auto rows = network::parse_osd_json(metadata);
        bool found = false;
        for (const auto &candidate : rows.array)
            if (candidate.get("name").text == row.get("name").text) {
                capture = candidate;
                found = true;
                break;
            }
        check(found, "Original input capture identity missing");
        for (const auto &[name, digest] : capture.get("sha256").map) {
            const auto artifact = capture_folder / name;
            if (std::filesystem::is_regular_file(artifact))
                check(framework::sha256_hex(read(artifact)) == digest.text, "Original input capture artifact changed");
        }
    }
    const auto source = read(folder / "source.lsl");
    for (const auto &[name, digest] : row.get("sha256").map) {
        const auto artifact = folder / name;
        if (std::filesystem::is_regular_file(artifact))
            check(framework::sha256_hex(read(artifact)) == digest.text, "Original hash artifact changed");
    }
    auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &error : compiled.diagnostics)
        diagnostics += " " + error.message;
    check(bool(compiled) == row.get("accepted").boolean, "Original hash acceptance differs:" + diagnostics);
    if (!compiled) {
        check(row.get("name").text == "char_ambiguous", "Unexpected rejected hash source");
        return;
    }
    check(yengine_source_matches(source, row.get("source_hash").text), "Original hash source identity differs");
    const auto &calls = row.get("hash_call_lines").array;
    check(!calls.empty(), "Original hash overload proof missing");
    for (const auto &call : calls)
        check(call.text.find("xmrHashCode(String)") != std::string::npos,
              "Original source did not bind the String hash overload");
    const bool null = row.get("direct_string_input").get("type").text == "null";
    const auto text = null ? std::u16string{} : units(row.get("direct_string_input"));
    if (!null) {
        const auto seed = std::stoull(row.get("marvin_seed").text, nullptr, 16);
        const auto expected = network::osd_integer(row.get("direct_string_hash"));
        check(std::bit_cast<std::int32_t>(framework::dotnet_string_hash(text, seed)) == expected,
              "Native UTF16 Marvin hash differs from actual original process seed");
        check(framework::config_http_cookie_marvin_hash(text, seed) == framework::dotnet_string_hash(text, seed),
              "Cookie explicit-seed compatibility changed");
    }
    const auto names = global_names(source);
    const auto strings = declared_string_globals(source);
    std::int64_t clock = 638000000000000000LL;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int) { ++sleeps; };
    auto native = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    auto last = native->checkpoint();
    bool stopped = false;
    const bool sleeping = row.get("name").text == "sleep_restart";
    for (unsigned step = 0; step < 4000; ++step) {
        const auto run = native->run(1);
        if (run.status == LslRunStatus::Faulted) {
            const auto &failure = row.get("initial_exception");
            check(null && failure.get("type").text == "System.NullReferenceException" &&
                      run.error == "NullReferenceException: " + failure.get("message").text,
                  "Original String hash fault category/text differs: " + run.error);
            check(last.has_value(), "Pre-fault String hash checkpoint missing");
            compare_globals(*last, names, row.get("initial").get("globals"), strings);
            stopped = true;
            break;
        }
        last = native->checkpoint();
        check(last.has_value(), "String hash checkpoint missing");
        if (run.status == LslRunStatus::Sleeping) {
            check(sleeping && sleeps == 1 && last->sleep_remaining > 0, "Unexpected String hash sleep");
            stopped = true;
            break;
        }
        if (run.status == LslRunStatus::Idle) {
            check(!sleeping && !null, "Original String hash outcome differs");
            compare_globals(*last, names, expected_globals(row.get("final").get("globals"), hash(text), false),
                            strings);
            check(native->run(1).status == LslRunStatus::Idle, "String hash entry replayed");
            stopped = true;
            break;
        }
        const auto wire = encode_lsl_checkpoint(*last);
        auto decoded = decode_lsl_checkpoint(wire);
        check(decoded && encode_lsl_checkpoint(*decoded) == wire, "Hash budget checkpoint changed bytes");
        auto fresh = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
        check(fresh->restore_checkpoint(*decoded), "Fresh String hash VM rejected budget checkpoint");
        native = std::move(fresh);
    }
    check(stopped, "String hash execution exceeded bound");
    if (!sleeping)
        return;
    auto initial = capture.get("initial").get("globals");
    initial.map.at("before_hash").map.at("value").integer = hash(text);
    compare_globals(*last, names, initial, strings);
    std::vector<std::unique_ptr<LslInstance>> paths;
    paths.push_back(std::move(native));
    const auto persistent = paths.front()->persistent_checkpoint();
    check(persistent.has_value(), "Persistent String hash state missing");
    const auto decoded = decode_lsl_checkpoint(encode_persistent_lsl_checkpoint(*persistent));
    auto transferred = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    check(decoded && transferred->restore_checkpoint(*decoded), "Persistent String hash state rejected");
    paths.push_back(std::move(transferred));
    const auto snapshot = read(capture_folder / "snapshot.bin");
    const auto encoded = network::binary_base64(
        network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(snapshot.begin(), snapshot.end())));
    const auto xml = "<ScriptState Engine=\"YEngine\" SourceHash=\"" + row.get("source_hash").text +
                     "\"><Running>True</Running><Snapshot>" + encoded + "</Snapshot>" +
                     read(capture_folder / "detect-array.xml") + read(capture_folder / "event-queue.xml") +
                     "</ScriptState>";
    const auto original = scene::read_yengine_state_xml(bytes(xml), framework::Uuid{});
    const auto object = decode_yengine_object_code(bytes(read(capture_folder / "original.yobj")));
    auto imported = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    check(original && original->snapshot && object &&
              imported->restore_yengine_snapshot(*original->snapshot, original->source_hash, source, *object),
          "Actual original String hash sleep capture rejected");
    compare_globals(*imported->checkpoint(), names, capture.get("initial").get("globals"), strings);
    paths.push_back(std::move(imported));
    for (std::size_t path = 0; path < paths.size(); ++path) {
        auto &vm = paths[path];
        clock += vm->checkpoint()->sleep_remaining + 1;
        bool done = false;
        for (unsigned step = 0; step < 4000; ++step) {
            const auto run = vm->run(1);
            check(run.status != LslRunStatus::Faulted && run.status != LslRunStatus::Sleeping,
                  "String hash resume faulted or replayed sleep: " + run.error);
            auto current = vm->checkpoint();
            check(current.has_value(), "Resumed String hash state missing");
            if (run.status == LslRunStatus::Idle) {
                compare_globals(*current, names,
                                expected_globals(row.get("final").get("globals"), hash(text), path < 2), strings);
                check(vm->run(1).status == LslRunStatus::Idle && sleeps == 1, "String hash resume replayed effects");
                done = true;
                break;
            }
            const auto decoded_step = decode_lsl_checkpoint(encode_lsl_checkpoint(*current));
            auto fresh = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
            check(decoded_step && fresh->restore_checkpoint(*decoded_step), "Hash resume budget restore failed");
            vm = std::move(fresh);
        }
        check(done, "String hash resume exceeded bound");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original String hash fixture directory");
        check(framework::native_config_http_cookie_hash_seed() == framework::dotnet_string_hash_seed(),
              "String hash callers do not share one process seed");
        const std::filesystem::path root = argv[1];
        check(framework::sha256_hex(read(root / "verification.json")) ==
                  "672fc9207d9e94ab3211587c45fe212200a68a911af9cfddd0a30431388d49c6",
              "Original String hash process verification changed");
        check(framework::sha256_hex(read(root.parent_path() / "yengine_string_hash_reference.cs")) ==
                  "772ee85ae4121a540b253b7920332a7436213b8c2ff8cd0b50392ed2527cfdab",
              "Original String hash generator changed");
        std::set<std::string> seeds;
        for (const auto &[process, digest] : std::array<std::pair<const char *, const char *>, 4>{
                 {{"process_a", "092254ba0e387353a3c4cea2448dad4298cea585ebd4156e188b08657d8e4939"},
                  {"process_b", "1546038a8cdd17155d6faaea9434ed47598084aa86120ce11b8fd320c6a740ca"},
                  {"process_import", "9a806a577f582ab95a59d78f5a0d52066b78e3de6f4e1c607338979e2215c0c1"},
                  {"linux_process", "1059f29983fc34c71993edda4414b3b89efe447ffb8c366be8e6169f8553f549"}}}) {
            const auto metadata = read(root / process / "metadata.json");
            check(framework::sha256_hex(metadata) == digest, "Original String hash process metadata changed");
            const auto rows = network::parse_osd_json(metadata);
            check(rows.array.size() == 14, "Original String hash case inventory changed");
            for (const auto &row : rows.array) {
                if (row.get("accepted").boolean)
                    seeds.insert(row.get("marvin_seed").text);
                try {
                    fixture(root / process, row);
                } catch (const std::exception &error) {
                    throw std::runtime_error(std::string(process) + "/" + row.get("name").text + ": " + error.what());
                }
            }
        }
        check(seeds.size() == 4, "Four distinct original process seeds were not measured");
        std::cout << "PASS String hash:56 original controls,4 process seeds,UTF16/null/key/order and sleep import\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
