// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/framework/dotnet_identity_hash.hpp"
#include <iostream>

using namespace utf16_test;

namespace {
constexpr auto generator_hash = "74d354ae10f1ec16536e8f2b3dfa8ae851ea3a065a25dc880d6e4f820820e71f";

std::uint32_t number(const network::Value &v) {
    const auto n = network::osd_real(v);
    check(n >= 0 && static_cast<std::uint64_t>(n) <= UINT32_MAX, "Original UInt32 out of range");
    return static_cast<std::uint32_t>(n);
}

void thread(const network::Value &row) {
    const auto id = number(row.get("managed_thread_id"));
    const auto &hashes = row.get("hashes").array;
    check(hashes.size() == 64 && row.get("unique_recovery").boolean, "Original thread evidence incomplete");
    check(number(row.get("multiplier")) == id * 4u + 5u, "Original multiplier differs");
    std::vector<std::uint32_t> candidates;
    for (std::uint32_t low = 0; low < 64; ++low) {
        auto state = (number(hashes[0]) << 6) | low;
        const auto first_state = state;
        bool matches = true;
        for (std::size_t i = 1; i < hashes.size(); ++i)
            matches &= framework::dotnet_identity_hash(state, id) == number(hashes[i]);
        if (matches)
            candidates.push_back(first_state);
    }
    check(candidates.size() == 1, "Actual 63 following hashes do not uniquely recover initial state");
    const auto &original_candidates = row.get("recovered_after_first_candidates").array;
    check(original_candidates.size() == 1 && candidates[0] == number(original_candidates[0]),
          "Recovered state differs from original proof");
    auto state = candidates[0];
    const auto &predicted = row.get("predicted_remaining_63").array;
    check(predicted.size() == 63, "Original predicted sequence incomplete");
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        const auto actual = number(hashes[i]);
        check(actual && actual <= 0x3ffffffu, "Original hash exceeds nonzero 26-bit contract");
        if (i)
            check(framework::dotnet_identity_hash(state, id) == actual && number(predicted[i - 1]) == actual,
                  "Explicit generator differs from actual CLR output");
    }
    const auto &array = row.get("array_relations");
    check(array.get("same_reference").boolean && array.get("stable").boolean, "Array identity evidence missing");
    const auto array_hash = number(array.get("original_hash"));
    for (auto key : {"alias_hash", "content_mutation_hash", "runtime_helpers_hash"})
        check(number(array.get(key)) == array_hash, "Original array identity changed");
    const auto &list = row.get("sdk_list_relations");
    check(list.get("first_backing_same").boolean && list.get("alias_backing_same").boolean &&
              list.get("stable").boolean,
          "Original SDK List backing evidence missing");
    const auto list_hash = number(list.get("data_hash"));
    for (auto key : {"list_hash", "alias_hash", "after_mutation_hash"})
        check(number(list.get(key)) == list_hash, "Original SDK List backing identity changed");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected identity fixture directory");
        const std::filesystem::path root(argv[1]);
        const auto verification = read(root / "verification.json");
        check(framework::sha256_hex(verification) == "04f4b21432cbdb4e441365babb999e81cb766e13266c71f359dfaeaa4c7b7a72",
              "Original verification hash differs");
        check(framework::sha256_hex(read(root.parent_path() / "dotnet_identity_hash_reference.cs")) == generator_hash,
              "Original reference generator changed");
        const auto manifest = network::parse_osd_json(verification);
        check(manifest.get("passed").boolean && manifest.get("processes").array.size() == 3,
              "Original processes incomplete");
        for (const auto &process : manifest.get("processes").array) {
            const auto bytes = read(root / process.get("directory").text / "metadata.json");
            check(framework::sha256_hex(bytes) == process.get("metadata_sha256").text,
                  "Original metadata hash differs");
            const auto data = network::parse_osd_json(bytes);
            check(data.get("runtime").text == ".NET 8.0.34" && data.get("architecture").text == "X64" &&
                      data.get("generator_sha256").text == generator_hash && data.get("threads").array.size() == 4,
                  "Original runtime/provenance differs");
            for (const auto &row : data.get("threads").array)
                thread(row);
        }
        for (unsigned i = 0; i < 64; ++i) {
            const auto hash = framework::dotnet_identity_hash();
            check(hash && hash <= 0x3ffffffu, "Native TLS generator produced invalid hash");
        }
        std::cout << "Identity hash: 768 original hashes verified; native/CLR process IDs are not equated\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
