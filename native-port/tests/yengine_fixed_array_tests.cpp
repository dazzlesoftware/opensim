// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_object_test_support.hpp"
#include "opensim/script/yengine_reference_projection.hpp"

using namespace object_test;

namespace {
std::filesystem::path relative_path(std::string path) {
    std::ranges::replace(path, '\\', '/');
    return std::filesystem::path(path);
}

void raw_cycle(const std::filesystem::path &folder) {
    std::weak_ptr<const YEngineManagedValue> raw_lifetime;
    std::weak_ptr<const LslFixedArray> projected_lifetime;
    {
        auto original = decode_yengine_snapshot(bytes(read(folder / "snapshot.bin")));
        check(original.has_value(), "Actual original cyclic array binary rejected");
        const auto &globals = std::get<YEngineManagedArray>(original->managed_globals[5]->value);
        check(globals.size() == 1, "Original cyclic Object global inventory changed");
        const auto input = globals.front();
        const auto &items = std::get<YEngineManagedArray>(input->value);
        check(input->kind == YEngineManagedValue::Kind::Array && input->element_type == "ob" && items.size() == 1 &&
                  items.front() == input,
              "Original binary early array registration lost self-reference");
        raw_lifetime = input;
        {
            YEngineReferenceProjection projection;
            const auto value = projection.project(input, YEngineReferenceType::Object);
            check(value.has_value(), "Actual cyclic array reference projection rejected");
            const auto array = std::get<std::shared_ptr<const LslFixedArray>>(*value);
            check(std::get<std::shared_ptr<const LslFixedArray>>(lsl_fixed_array_get(array, 0)) == array,
                  "Actual cyclic array projection lost self-reference");
            projected_lifetime = array;
        }
        check(projected_lifetime.expired(), "Cyclic projected array leaked after its final external root");
    }
    check(raw_lifetime.expired(), "Cyclic raw original graph leaked after snapshot destruction");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 5, "Expected fixed-array, heap-ledger, Object-element and temporary-owner fixture directories");
        const std::filesystem::path root = argv[1], heap = argv[2], elements = argv[3], temporary = argv[4];
        check(framework::sha256_hex(read(root / "verification.json")) ==
                  "2b2c4ed212b4d5eea82180ab5ff3d2ad4164c8c310b33ae3220829ff6b44f0ea",
              "Original fixed-array verification changed");
        check(framework::sha256_hex(read(heap / "verification.json")) ==
                  "62193af7ba59f48c366ecfc188f9c1a0501d33099e6297c6374b02f077b26521",
              "Original fixed-array heap verification changed");
        check(framework::sha256_hex(read(elements / "verification.json")) ==
                  "808e92de40f1599e067b04342371bcae9a19976ac897714f9eaf5ae853248251",
              "Original Object-array element verification changed");
        check(framework::sha256_hex(read(temporary / "verification.json")) ==
                  "c93a199de8d4bf44f4f7ec7d359f0ea3a30ec288ca9231bcf9e393d266844cf5",
              "Original temporary array-owner verification changed");
        for (const auto &[name, digest] : std::array<std::pair<const char *, const char *>, 6>{{
                 {"yengine_fixed_array_reference.cs",
                  "ef43710f20f66f5f32fcb8f53c97b95ec056e57c5d55f37fb1bf3db256d6edb2"},
                 {"yengine_fixed_array_values_reference.cs",
                  "3f0f7e553ec3448acd2323dda3bcdd02e1f5ea4b438390b1bb41763118392791"},
                 {"yengine_fixed_array_cycle_reference.cs",
                  "d51cd0d04e3c359cec32a889b002b1b423652611cedd80177fc956e14a1a179d"},
                 {"yengine_fixed_array_heap_reference.cs",
                  "155a5aa2320fad1594aed3dea4de83c33a1a5dda58f1b73ae2f2f17b9198292c"},
                 {"yengine_fixed_array_elements_reference.cs",
                  "e9254299cb43cc6fa6122be9ff9da3407e5596003a5ceeb1bb2caba8b942b0d3"},
                 {"yengine_fixed_array_temporary_reference.cs",
                  "e171c0c4677529a15310f3b31595528625a162f97625918545bcbcc546fb4489"},
             }})
            check(framework::sha256_hex(read(root.parent_path() / name)) == digest,
                  std::string("Original fixed-array generator changed: ") + name);
        const auto pins = network::parse_osd_json(read(root / "verification.json"));
        for (const auto &[path, digest] : pins.get("files_sha256").map)
            check(framework::sha256_hex(read(root / relative_path(path))) == digest.text,
                  "Original fixed-array artifact changed: " + path);

        struct Corpus {
            std::filesystem::path folder;
            const char *digest;
            std::size_t cases, accepted, captures;
        };

        const std::array<Corpus, 10> corpora{{
            {root / "windows", "da95dc36daf0b2c4d2114db96731db93eb88267645d59795cbb7b0131b31e357", 103, 101, 15},
            {root / "linux", "422177ccd22a5aa4be5598bc2d7b4e3c97ce2147b491d8e3b44490fb01878f9f", 103, 101, 15},
            {root / "values_windows", "77bd806989f168a7e3734a170c42430527ef384ae8da47b944a5c855346e6e34", 13, 13, 9},
            {root / "values_linux", "f1cb871c3c3dd0fcdafbabb28ee4e80132191f843ce0f2858af8437f59435a65", 13, 13, 9},
            {heap / "windows", "4e056d26d4993515382ab44d1aa3ac2616ce637591aad067c50fa156851207b0", 15, 15, 3},
            {heap / "linux", "73d824a18a125759c8698df5fc647b94af70a68c227f3b804d0142b98f27fe6e", 15, 15, 3},
            {elements / "windows", "115f81465958b871607315cb1db548bdac7a9c3137d26ded2ec8755583c0bbb8", 11, 11, 11},
            {elements / "linux", "d02d26402afcfc57e7694cf01ce0d489c2e935bbcfd170218bb944c7a3418d08", 11, 11, 11},
            {temporary / "windows", "545d217451911d42016d28b34e183cfbaf73169d93b7dce1d13fa2f7b4980b75", 14, 14, 5},
            {temporary / "linux", "441773a0dd22559c53dac8484d59d23ad0a0a8b2ccb925cd2a05ab8638e90f3f", 14, 14, 5},
        }};
        std::vector<std::string> failures;
        for (const auto &corpus : corpora) {
            const auto metadata = read(corpus.folder / "metadata.json");
            check(framework::sha256_hex(metadata) == corpus.digest, "Original fixed-array corpus metadata changed");
            const auto rows = network::parse_osd_json(metadata);
            check(rows.array.size() == corpus.cases, "Original fixed-array source inventory changed");
            std::size_t accepted = 0, captures = 0;
            for (const auto &row : rows.array) {
                accepted += row.get("accepted").boolean;
                captures += row.get("suspended").boolean;
                try {
                    fixture(corpus.folder / row.get("name").text, row, 100000000);
                } catch (const std::exception &error) {
                    failures.push_back(corpus.folder.filename().string() + "/" + row.get("name").text + ": " +
                                       error.what());
                }
            }
            check(accepted == corpus.accepted && captures == corpus.captures,
                  "Original fixed-array acceptance/capture inventory changed");
        }
        for (const auto *platform : {"cycles_windows", "cycles_linux"})
            raw_cycle(root / platform / "cycle_self_sleep" / "cycle_self_sleep");
        for (const auto &failure : failures)
            std::cerr << failure << '\n';
        check(failures.empty(), std::to_string(failures.size()) + " fixed-array original outcomes differ");
        std::cout << "PASS 103 array +13 value/identity +15 heap +11 Object-element +14 temporary-owner cases per "
                     "original OS; "
                     "internal/persistent/original imports and raw-cycle ownership\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
