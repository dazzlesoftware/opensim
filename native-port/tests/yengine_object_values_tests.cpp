// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_object_test_support.hpp"

using namespace object_test;

int main(int argc, char **argv) {
    try {
        check(argc == 4, "Expected Object value, conversion-identity and Object queue fixture directories");
        const std::filesystem::path root = argv[1];
        check(framework::sha256_hex(read(root / "verification.json")) ==
                  "be8c4fea3f5488a751e23787166967066363d6c45916112f1e796effa2331b5a",
              "Original Object verification changed");
        check(framework::sha256_hex(read(root.parent_path() / "yengine_object_value_reference.cs")) ==
                  "e9711a2ec5669dfb39b3be7b9cd8968163891779e821922757dc98f610e4d9cf",
              "Original Object generator changed");
        std::vector<std::string> failures;
        for (const auto &[process, digest] : std::array<std::pair<const char *, const char *>, 2>{
                 {{"process_a", "cd4e5c4711b7f49cc45f205891b66bc98b2b347e2c07f22b87c1e88c3fbedde9"},
                  {"linux_process", "f995450c98a79f78cfb073fb87dd51fc7409d1fad3d0d09240a87c01ea902f48"}}}) {
            const auto metadata = read(root / process / "metadata.json");
            check(framework::sha256_hex(metadata) == digest, "Original Object process metadata changed");
            const auto rows = network::parse_osd_json(metadata);
            check(rows.array.size() == 216, "Original Object source inventory changed");
            unsigned accepted = 0, faults = 0, captures = 0;
            for (const auto &row : rows.array) {
                accepted += row.get("accepted").boolean;
                faults += row.get("initial_exception").kind == network::Value::Kind::Map;
                captures += row.get("suspended").boolean;
                try {
                    fixture(root / process / row.get("name").text, row);
                } catch (const std::exception &error) {
                    failures.push_back(std::string(process) + "/" + row.get("name").text + ": " + error.what());
                }
            }
            check(accepted == 202 && faults == 62 && captures == 8, "Original Object outcome inventory changed");
        }

        struct ExtraCorpus {
            const char *generator;
            const char *generator_hash;
            const char *verification_hash;
            std::array<const char *, 2> metadata_hashes;
            unsigned count, captures;
        };

        const std::array<ExtraCorpus, 2> extras{{{"yengine_object_string_conversion_reference.cs",
                                                  "d9c1e6737fee58cb92e97915e5a0fb6db18a731db5110312ff6e026111e7fcf4",
                                                  "f76023d7b8349d990a08d17a54328d57a2b3dde3731bb1c564f01a8d51a10542",
                                                  {"7927f83151d67704c53b29d2f416908c3b9d2ec8acf86e37e468a814696a5ab5",
                                                   "aad503e83bdb3435f371fd98779c244a43e75c5cf61eb60743bbb3ffa4faa363"},
                                                  80,
                                                  7},
                                                 {"yengine_object_event_queue_reference.cs",
                                                  "94fbaf9c939eac122e2fb36c948fba365aa33eff54e2e5e8030920a42106d725",
                                                  "f240ee50ed2aac460a8140e72b7bbefd85c59e4adbdec0455ff1f4aefacfba4c",
                                                  {"a3474d27816896fb428d49ba75330b1f3a4d85e653432c944d36489d1a317cea",
                                                   "bc3be191c476e51ea3557add43d1a158d19167a196ed749e50ead8bbab66485d"},
                                                  8,
                                                  8}}};
        for (std::size_t index = 0; index < extras.size(); ++index) {
            const auto &extra = extras[index];
            const std::filesystem::path folder = argv[index + 2];
            check(framework::sha256_hex(read(folder / "verification.json")) == extra.verification_hash,
                  "Original Object supplementary verification changed");
            check(framework::sha256_hex(read(folder.parent_path() / extra.generator)) == extra.generator_hash,
                  "Original Object supplementary generator changed");
            for (std::size_t process = 0; process < 2; ++process) {
                const auto process_folder = folder / (process ? "linux_process" : "process_a");
                const auto text = read(process_folder / "metadata.json");
                check(framework::sha256_hex(text) == extra.metadata_hashes[process],
                      "Original Object supplementary metadata changed");
                const auto rows = network::parse_osd_json(text);
                check(rows.array.size() == extra.count, "Original Object supplementary inventory changed");
                unsigned captures = 0;
                for (const auto &row : rows.array) {
                    check(row.get("accepted").boolean && row.get("initial_exception").kind != network::Value::Kind::Map,
                          "Original Object supplementary source failed");
                    captures += row.get("suspended").boolean;
                    try {
                        fixture(process_folder / row.get("name").text, row);
                    } catch (const std::exception &error) {
                        failures.push_back(std::string(extra.generator) + "/" + process_folder.filename().string() +
                                           "/" + row.get("name").text + ": " + error.what());
                    }
                }
                check(captures == extra.captures, "Original Object supplementary capture count differs");
            }
        }
        for (const auto &failure : failures)
            std::cerr << failure << '\n';
        check(failures.empty(), std::to_string(failures.size()) + " Object source outcomes differ");
        std::cout << "PASS 216 Object +80 conversion +8 queue controls per original Windows/Linux corpus; "
                     "budget1/live/persistent/import paths\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
