// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_object_test_support.hpp"
#include <set>

using namespace object_test;

namespace {
void large_lists() {
    for (const std::size_t count : {65537u, 100000u}) {
        auto list = std::make_shared<LslList>();
        list->items.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
            list->items.emplace_back(std::int32_t(index == count - 1 ? -7 : 19));
        const auto value = std::shared_ptr<const LslList>(list);
        const auto expected = integer_list_cells_hash(value);
        LslCheckpoint checkpoint;
        checkpoint.program_hash = "collection-capacity-list";
        checkpoint.state = "default";
        checkpoint.globals = {value, value};
        for (const bool persistent : {false, true}) {
            const auto wire =
                persistent ? encode_persistent_lsl_checkpoint(checkpoint) : encode_lsl_checkpoint(checkpoint);
            const auto restored = decode_lsl_checkpoint(wire);
            check(restored && restored->globals.size() == 2, "Large List checkpoint rejected");
            const auto first = std::get<std::shared_ptr<const LslList>>(restored->globals[0]);
            check(first == std::get<std::shared_ptr<const LslList>>(restored->globals[1]) &&
                      first->items.size() == count,
                  "Large List checkpoint lost count or alias");
            check(integer_list_cells_hash(first) == expected, "Large List checkpoint changed cells");
            if (count == 65537) {
                auto malformed = wire;
                const std::array<std::uint8_t, 5> header{5, 1, 0, 1, 0};
                const auto at = std::search(malformed.begin(), malformed.end(), header.begin(), header.end());
                check(at != malformed.end(), "Large List length header missing");
                std::fill(at + 1, at + 5, 255);
                check(!decode_lsl_checkpoint(malformed), "Impossible List count accepted");
            }
        }
    }
}

void many_string_references() {
    constexpr std::size_t count = 100000;
    const auto value = make_lsl_fixed_array(LslFixedArrayKind::Object, count);
    const auto array = std::get<std::shared_ptr<const LslFixedArray>>(value);
    for (std::size_t index = 0; index < count; ++index)
        lsl_fixed_array_set(array, index, make_lsl_reference_string(framework::utf16(std::to_string(index))));
    LslCheckpoint checkpoint;
    checkpoint.program_hash = "collection-capacity-references";
    checkpoint.state = "default";
    checkpoint.globals = {value, value};
    const auto expected = fixed_array_cells_hash(array);
    for (const bool persistent : {false, true}) {
        const auto wire = persistent ? encode_persistent_lsl_checkpoint(checkpoint) : encode_lsl_checkpoint(checkpoint);
        const auto restored = decode_lsl_checkpoint(wire);
        check(restored && restored->globals.size() == 2, "Many-reference checkpoint rejected");
        const auto first = std::get<std::shared_ptr<const LslFixedArray>>(restored->globals[0]);
        check(first == std::get<std::shared_ptr<const LslFixedArray>>(restored->globals[1]) &&
                  lsl_fixed_array_size(first) == count && fixed_array_cells_hash(first) == expected,
              "Many-reference checkpoint lost array alias or full cells");
        std::set<const LslStringReference *> identities;
        for (std::size_t index = 0; index < count; ++index) {
            const auto cell = lsl_fixed_array_get(first, index);
            const auto text = std::get<std::shared_ptr<const LslStringReference>>(cell);
            check(text && text->units == framework::utf16(std::to_string(index)), "Many-reference String changed");
            identities.insert(text.get());
        }
        check(identities.size() == count, "Distinct String references were merged");
        auto truncated = wire;
        truncated.resize(truncated.size() / 2);
        check(!decode_lsl_checkpoint(truncated), "Truncated many-reference checkpoint accepted");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected collection-capacity fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "a3db0f2615f85e5519074463cc4fa06f02ed232bb6f493ffff00edca142bf381",
              "Collection-capacity original proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        for (const auto &[name, digest] : proof.get("files").map)
            check(framework::sha256_hex(read(root / name)) == digest.text,
                  "Original capacity artifact changed: " + name);
        large_lists();
        many_string_references();
        unsigned cases = 0;
        for (const auto kind : {"list", "reference"})
            for (const auto platform : {"windows", "linux"}) {
                const auto folder = root / kind / platform;
                const auto rows = network::parse_osd_json(read(folder / "metadata.json"));
                check(rows.array.size() == 2, "Original collection-capacity case inventory changed");
                for (const auto &row : rows.array) {
                    check(row.get("accepted").boolean && row.get("suspended").boolean && row.get("migrated").boolean,
                          "Original collection-capacity source did not compile/capture/migrate");
                    try {
                        fixture(folder / row.get("name").text, row, 100000000, 100000);
                    } catch (const std::exception &error) {
                        throw std::runtime_error(std::string(kind) + "/" + platform + "/" + row.get("name").text +
                                                 ": " + error.what());
                    }
                    ++cases;
                }
            }
        check(cases == 8, "Missing original collection-capacity comparisons");
        std::cout << "PASS 8 original collection/reference capacity cases, full cells, "
                     "native/internal/persistent/original imports and malformed lengths\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
