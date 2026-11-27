// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_object_test_support.hpp"

using namespace object_test;

namespace {
void invalid_lengths() {
    for (const auto kind : {LslFixedArrayKind::Character, LslFixedArrayKind::Float, LslFixedArrayKind::Integer,
                            LslFixedArrayKind::Object}) {
        LslCheckpoint checkpoint;
        checkpoint.program_hash = "large-array-length-check";
        checkpoint.state = "default";
        checkpoint.globals = {make_lsl_fixed_array(kind, 1)};
        auto wire = encode_lsl_checkpoint(checkpoint);
        const std::array<std::uint8_t, 8> header{25, 0, static_cast<std::uint8_t>(kind), 0, 1, 0, 0, 0};
        const auto at = std::search(wire.begin(), wire.end(), header.begin(), header.end());
        check(at != wire.end(), "Array length control header missing");
        std::fill(at + 4, at + 8, 255);
        check(!decode_lsl_checkpoint(wire), "Impossible array length accepted");
    }
    LslCheckpoint checkpoint;
    checkpoint.program_hash = "large-array-byte-limit";
    checkpoint.state = "default";
    checkpoint.globals = {make_lsl_fixed_array(LslFixedArrayKind::Float, 4 * 1024 * 1024 / 8)};
    const auto wire = encode_lsl_checkpoint(checkpoint);
    check(wire.size() > 4 * 1024 * 1024, "Byte capacity control did not cross the former envelope");
    const auto restored = decode_lsl_checkpoint(wire);
    check(restored && restored->globals.size() == 1, "Former byte capacity boundary did not roundtrip");
    const auto array = std::get<std::shared_ptr<const LslFixedArray>>(restored->globals.front());
    check(lsl_fixed_array_size(array) == 4 * 1024 * 1024 / 8, "Former byte boundary changed array length");
}

void invalid_original_length(const std::filesystem::path &root) {
    const auto original = read(root / "windows/large_I_100000/snapshot.bin");
    std::vector<std::uint8_t> wire(original.begin(), original.end());
    const std::array<std::uint8_t, 8> header{9, 2, 'i', 'n', 0xa0, 0x86, 1, 0};
    const auto at = std::search(wire.begin(), wire.end(), header.begin(), header.end());
    check(at != wire.end(), "Original large-array length control missing");
    std::fill(at + 4, at + 8, 0x7f);
    check(!decode_yengine_snapshot(wire), "Impossible original array length accepted");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original large-array fixture directory");
        invalid_lengths();
        const std::filesystem::path root = argv[1];
        invalid_original_length(root);
        check(framework::sha256_hex(read(root / "verification.json")) ==
                  "e5b6faa96cac4cca33f4c0f24e80c7b4475d0d5369b3a501cf326a208f0aef1c",
              "Original large-array proof changed");
        check(framework::sha256_hex(read(root.parent_path() / "yengine_large_fixed_array_reference.cs")) ==
                  "39eaba6199a4c4c95df3ed10843fcdd0057014430df69ef6427376dd80f63549",
              "Original large-array generator changed");
        std::vector<std::string> failures;
        for (const auto &[name, digest] : std::array<std::pair<const char *, const char *>, 2>{{
                 {"windows", "f127fd3acf292bb0c5e6c79621d43faaeff2fd6e32e45a37973878b50f681e14"},
                 {"linux", "b5cf438be1a64b286ac4ed98b112b005d5b465f37a7a36c82e511f761f7f8b40"},
             }}) {
            const auto metadata = read(root / name / "metadata.json");
            check(framework::sha256_hex(metadata) == digest, "Original large-array metadata changed");
            const auto rows = network::parse_osd_json(metadata);
            check(rows.array.size() == 8, "Original large-array control inventory changed");
            for (const auto &row : rows.array) {
                check(row.get("accepted").boolean && row.get("suspended").boolean && row.get("migrated").boolean,
                      "Original large-array compile/capture/migration did not succeed");
                try {
                    fixture(root / name / row.get("name").text, row, 100000000);
                } catch (const std::exception &error) {
                    failures.push_back(std::string(name) + "/" + row.get("name").text + ": " + error.what());
                }
            }
        }
        for (const auto &failure : failures)
            std::cerr << failure << '\n';
        check(failures.empty(), std::to_string(failures.size()) + " original large-array outcomes differ");
        std::cout << "PASS 16 original large-array cases: full-cell hashes, internal preemption, live/persistent/"
                     "original import resumes at 65537 and 100000 elements\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
