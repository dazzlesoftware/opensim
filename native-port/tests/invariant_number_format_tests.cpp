// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/invariant_number_format.hpp"
#include "opensim/network/osd_json.hpp"
#include <bit>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace opensim;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Missing original numeric formatter fixture: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

double original_double(const network::Value &input) {
    const auto &text = input.get("bits").text;
    check(text.size() == 16, "Original Double bits missing");
    std::uint64_t bits{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), bits, 16);
    check(result.ec == std::errc{} && result.ptr == text.data() + text.size(), "Invalid original Double bits");
    return std::bit_cast<double>(bits);
}

std::string format(const network::Value &row) {
    const auto &kind = row.get("kind").text;
    const auto &input = row.get("direct_input");
    const auto &specifier = row.get("format").text;
    if (kind == "Integer")
        return framework::format_invariant_integer(network::osd_integer(input.get("value")), specifier);
    if (kind == "Float")
        return framework::format_invariant_double(original_double(input), specifier);
    check(kind == "Vector" || kind == "Rotation", "Unknown original formatter kind: " + kind);
    const auto &components = input.get("components").array;
    check(components.size() == (kind == "Vector" ? 3 : 4), "Original vector/rotation component count differs");
    // This mirrors the original thin wrapper contract; runtime bindings are
    // checked separately by the VM source oracle target.
    std::string result = "<";
    for (std::size_t index = 0; index < components.size(); ++index) {
        if (index)
            result += ',';
        result += framework::format_invariant_double(original_double(components[index]), specifier);
    }
    return result + '>';
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc >= 2 && argc <= 4, "Expected baseline and optional extended/zero-section fixture directories");
        for (int fixture = 1; fixture < argc; ++fixture) {
            const auto rows = network::parse_osd_json(read(std::filesystem::path(argv[fixture]) / "metadata.json"));
            const std::size_t expected_rows = fixture == 1 ? 489 : fixture == 2 ? 15 : 5;
            check(rows.array.size() == expected_rows,
                  "Original formatter corpus count changed; review coverage explicitly");
            std::map<std::string, std::size_t> kinds;
            std::size_t returned = 0, faults = 0, source_only = 0;
            for (const auto &row : rows.array) {
                const auto &name = row.get("name").text;
                const auto &kind = row.get("kind").text;
                ++kinds[kind];
                if (kind == "Source") {
                    check(fixture == 1 && name == "argument_order", "Unexpected source-only formatter oracle");
                    ++source_only;
                    continue;
                }
                try {
                    const auto actual = format(row);
                    check(row.get("direct_exception").kind != network::Value::Kind::Map,
                          name + ": expected original FormatException but formatter returned");
                    const auto &expected = row.get("direct_result").get("value").text;
                    check(actual == expected, name + ": exact UTF-8 formatter output differs; actual=[" + actual +
                                                  "] original=[" + expected + "]");
                    ++returned;
                } catch (const std::runtime_error &error) {
                    const std::string message = error.what();
                    if (!message.starts_with("FormatException: "))
                        throw;
                    check(row.get("direct_exception").get("type").text == "System.FormatException",
                          name + ": unexpected native FormatException: " + message);
                    ++faults;
                }
            }
            if (fixture == 1)
                check(kinds ==
                              std::map<std::string, std::size_t>{
                                  {"Integer", 141}, {"Float", 287}, {"Vector", 30}, {"Rotation", 30}, {"Source", 1}} &&
                          returned == 471 && faults == 17 && source_only == 1,
                      "Original formatter coverage accounting differs");
            else
                check(returned == expected_rows && faults == 0 && source_only == 0,
                      "Extended original formatter coverage accounting differs");
        }
        std::cout << "PASS: 488 original direct format cases (471 exact UTF-8 returns, 17 FormatException); "
                     "one argument-order source case belongs to the separate VM oracle target; supplied 15/5-case "
                     "extensions verified separately\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
