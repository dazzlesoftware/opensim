// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/yengine_continuation.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace opensim::script;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::vector<std::uint8_t> read(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    check(bool(input), "Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void all_rejected(const YEngineContinuationMetadata &metadata) {
    check(!metadata.methods.empty(), "Missing analyzed methods");
    for (const auto &[name, method] : metadata.methods)
        check(!method.supported && !method.error.empty(), "Invalid class context admitted " + name);
}

void mutations(const YEngineObjectCode &original, std::int32_t index) {
    const std::array allowed{index};
    all_rejected(analyze_yengine_continuations(original));
    const std::array duplicate{index, index};
    all_rejected(analyze_yengine_continuations(original, duplicate));
    const std::array absent{std::int32_t{999999}};
    all_rejected(analyze_yengine_continuations(original, absent));
    const std::array negative{std::int32_t{-1}};
    all_rejected(analyze_yengine_continuations(original, negative));
    for (unsigned mutation = 0; mutation < 11; ++mutation) {
        auto bad = original;
        auto &type = bad.types.at("integer[]");
        switch (mutation) {
        case 0:
            type.kind = 2;
            break;
        case 1:
            type.array_rank = 2;
            break;
        case 2:
            type.element_type = "float";
            break;
        case 3:
            type.instance_sizes[3] = 2;
            break;
        case 4:
            type.instance_sizes[5] = 0;
            break;
        case 5:
            type.base_type = "Other";
            break;
        case 6:
            type.virtual_methods = 1;
            break;
        case 7:
            type.bindings.push_back({0, "Other.Get()", "integer()"});
            break;
        case 8:
            type.interfaces.push_back({"Other", {}});
            break;
        case 9: {
            auto duplicate_type = type;
            duplicate_type.kind = 1;
            bad.types.emplace("duplicate-delegate", duplicate_type);
            break;
        }
        case 10:
            bad.source_hash = "invalid";
            break;
        }
        all_rejected(analyze_yengine_continuations(bad, allowed));
    }
    auto renamed = original;
    auto node = renamed.types.extract("integer[]");
    node.key() = "OrdinaryClass";
    renamed.types.insert(std::move(node));
    all_rejected(analyze_yengine_continuations(renamed, allowed));
}

void delegate_control() {
    YEngineObjectCode code{};
    code.compiled_version = -11;
    code.source_hash = std::string(32, '0');
    YEngineObjectType delegate{};
    delegate.kind = 1;
    delegate.index = 0;
    code.types.emplace("$anonymous", delegate);
    YEngineObjectMethod body;
    body.has_body = true;
    code.methods.emplace("default state_entry", body);
    check(analyze_yengine_continuations(code).methods.at("default state_entry").supported,
          "Ordinary delegate continuation regressed");
    const std::array wrong{std::int32_t{0}};
    all_rejected(analyze_yengine_continuations(code, wrong));
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc > 1, "Supply original generated-array fixture directories");
        unsigned files = 0, class_slots = 0, source_functions = 0, helpers = 0;
        bool mutation_checked = false;
        for (int argument = 1; argument < argc; ++argument)
            for (const auto &entry : std::filesystem::recursive_directory_iterator(argv[argument])) {
                if (entry.path().filename() != "original.yobj")
                    continue;
                const auto decoded = decode_yengine_object_code(read(entry.path()));
                check(bool(decoded), "Original code rejected");
                const auto type = decoded->types.find("integer[]");
                check(type != decoded->types.end(), "Generated integer[] type missing");
                const std::array allowed{type->second.index};
                const auto metadata = analyze_yengine_continuations(*decoded, allowed);
                const auto event = metadata.methods.find("default state_entry");
                check(event != metadata.methods.end() && event->second.supported,
                      "Source event continuation rejected: " +
                          (event == metadata.methods.end() ? std::string("absent") : event->second.error));
                for (const auto &[name, method] : metadata.methods) {
                    if (name.starts_with("integer[].")) {
                        check(!method.supported, "Generated helper active frame admitted");
                        ++helpers;
                        continue;
                    }
                    check(method.supported, "Ordinary source frame rejected " + name + ": " + method.error);
                    if (name != "default state_entry" && name != "$globalvarinit()")
                        ++source_functions;
                    for (const auto &slot : method.slots)
                        if (slot.type == "sdtclobj")
                            ++class_slots;
                }
                for (const auto &[name, method] : decoded->methods) {
                    if (name.starts_with("integer[]."))
                        continue;
                    for (std::size_t record = 0; record < method.records.size(); ++record) {
                        const auto &conversion = method.records[record];
                        if (conversion.kind != YEngineObjectRecordKind::EmitType || !conversion.opcode ||
                            static_cast<std::uint16_t>(*conversion.opcode) != 0x74 ||
                            conversion.strings != std::vector<std::string>{"sdtclobj"})
                            continue;
                        auto bad = *decoded;
                        bad.methods.at(name).records[record].strings = {"int"};
                        check(!analyze_yengine_continuations(bad, allowed).methods.at(name).supported,
                              "Mismatched class restore type accepted");
                        bad = *decoded;
                        bad.methods.at(name).records[record].opcode = std::int16_t{0xa5};
                        check(!analyze_yengine_continuations(bad, allowed).methods.at(name).supported,
                              "Boxed class restore conversion accepted");
                    }
                }
                if (!mutation_checked) {
                    mutations(*decoded, type->second.index);
                    mutation_checked = true;
                }
                ++files;
            }
        delegate_control();
        check(files > 0 && helpers > 0 && class_slots > 0, "No real class-reference continuation storage exercised");
        std::cout << "PASS " << files << " original objects, " << class_slots << " managed class slots, "
                  << source_functions << " source functions; generated helpers rejected\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
    return 0;
}
