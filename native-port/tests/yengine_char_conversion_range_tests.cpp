// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include <iostream>
using namespace utf16_test;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

void units(std::u16string_view actual, const network::Value &expected, const std::string &label) {
    check(expected.kind == network::Value::Kind::Array && actual.size() == expected.array.size(),
          label + ": UTF16 length differs");
    for (std::size_t i = 0; i < actual.size(); ++i)
        check(actual[i] == network::osd_integer(expected.array[i]), label + ": UTF16 unit differs");
}

void cells(const Class &wrapper, const network::Value &expected, const std::string &label) {
    const auto &value = wrapper->instance_arrays[5].at(0);
    const auto name = expected.get("backingClr").text;
    if (name.empty()) {
        check(std::holds_alternative<LslNullObject>(value) && expected.get("utf16").kind != network::Value::Kind::Array,
              label + ": null backing differs");
        return;
    }
    const auto array = std::get<Array>(value);
    check(array && lsl_fixed_array_size(array) == 2, label + ": backing shape differs");
    if (name == "System.Char[]") {
        check(array->kind == LslFixedArrayKind::Character, label + ": Char backing type differs");
        std::u16string characters;
        for (std::size_t i = 0; i < 2; ++i)
            characters.push_back(std::get<LslCharacter>(lsl_fixed_array_get(array, i)).value);
        units(characters, expected.get("utf16"), label + " partial destination");
    } else {
        check(name == "System.Int32[]" && array->kind == LslFixedArrayKind::Integer &&
                  expected.get("integers").array.size() == 2,
              label + ": wrong-type backing changed");
        for (std::size_t i = 0; i < 2; ++i)
            check(std::get<std::int32_t>(lsl_fixed_array_get(array, i)) ==
                      network::osd_integer(expected.get("integers").array[i]),
                  label + ": wrong backing cell changed");
    }
}

void matrix(const std::filesystem::path &folder, const network::Value &proof) {
    const auto source = read(folder / "source.lsl");
    const auto matrix_text = read(folder / "range-matrix.json");
    check(framework::sha256_hex(source) == proof.get("sourceSha256").text &&
              framework::sha256_hex(matrix_text) == proof.get("matrixSha256").text &&
              framework::sha256_hex(read(folder / "metadata.json")) == proof.get("metadataSha256").text &&
              framework::sha256_hex(read(folder / "original.yobj")) == proof.get("objectSha256").text &&
              framework::sha256_hex(read(folder / "snapshot.bin")) == proof.get("snapshotSha256").text,
          "Original conversion matrix artifacts changed");
    const auto compiled = compile_lsl(source);
    check(bool(compiled), "Native source-created Char[] control rejected");
    std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    LslInstance owner(compiled.program, {}, host);
    bool paused = false;
    for (unsigned i = 0; i < 10000; ++i) {
        const auto result = owner.run(1);
        check(result.error.empty(), "Char[] source preparation faulted: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            paused = true;
            break;
        }
    }
    const auto captured = owner.checkpoint();
    check(paused && captured && captured->globals.size() == 2, "Char[] source did not pause");
    const auto wrapper = std::get<Class>(captured->globals[0]);
    const auto original = std::get<Array>(wrapper->instance_arrays[5].at(0));
    check(original && original->kind == LslFixedArrayKind::Character && lsl_fixed_array_size(original) == 2,
          "Source-generated Char[] backing shape differs");
    const auto rows = network::parse_osd_json(matrix_text).array;
    check(rows.size() == 5684, "Original conversion row inventory differs");
    unsigned checked = 0;
    for (const auto &row : rows) {
        const auto label = std::string("matrix row ") + std::to_string(checked);
        lsl_fixed_array_set(original, 0, LslCharacter{65});
        lsl_fixed_array_set(original, 1, LslCharacter{90});
        const auto backing = row.get("backing").text;
        LslValue object = original;
        if (backing == "null_backing")
            object = LslNullObject{};
        if (backing == "wrong_int") {
            auto integers = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Integer, 2));
            lsl_fixed_array_set(integers, 0, std::int32_t{7});
            lsl_fixed_array_set(integers, 1, std::int32_t{8});
            object = integers;
        }
        lsl_script_class_store_field(wrapper, 5, 0, object);
        const LslValue argument = backing == "null_wrapper" ? LslValue{LslNullObject{}} : LslValue{wrapper};
        std::optional<LslValue> result;
        std::string error;
        try {
            if (row.get("operation").text == "chars_to_string") {
                result = lsl_chars_to_string(argument, network::osd_integer(row.get("start")),
                                             network::osd_integer(row.get("count")));
            } else {
                const auto source_name = row.get("sourceName").text;
                const LslValue text = source_name == "null" ? LslValue{LslNullString{}}
                                                            : make_lsl_string(source_name == "empty" ? u""
                                                                              : source_name == "AZ"  ? u"AZ"
                                                                                                     : u"\U0001f600");
                lsl_string_to_chars(text, network::osd_integer(row.get("sourceStart")), argument,
                                    network::osd_integer(row.get("destinationStart")),
                                    network::osd_integer(row.get("count")));
            }
        } catch (const std::exception &failure) {
            error = failure.what();
        }
        const auto &expected_error = row.get("error");
        if (expected_error.kind == network::Value::Kind::Map) {
            const auto type = expected_error.get("type").text;
            check(type.starts_with("System.") && error == type.substr(7) + ": " + expected_error.get("message").text,
                  label + ": exact conversion exception differs: " + error);
        } else {
            check(error.empty(), label + ": unexpected conversion exception: " + error);
            if (row.get("operation").text == "chars_to_string") {
                check(result.has_value(), label + ": converted String missing");
                const auto converted = lsl_string_code_units(*result);
                check(converted.has_value(), label + ": result is not UTF16 String");
                units(*converted, row.get("resultUtf16"), label + " result");
            }
        }
        cells(wrapper, row.get("cells"), label);
        ++checked;
    }
    check(checked == 5684, "Native conversion matrix incomplete");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original character conversion range fixture directory");
        const std::filesystem::path root = argv[1];
        const auto proof_text = read(root / "verification.json");
        check(framework::sha256_hex(proof_text) == "485c9e20115357d71e2204b302cd69f604c3e20e7fa3677b3da4dc2c03a37e60",
              "Original character conversion matrix proof changed");
        const auto proof = network::parse_osd_json(proof_text);
        check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
              "Original conversion matrix proof scope changed");
        // Range exception text uses the host runtime's newline convention.
        // Both original datasets are hash-checked by the standalone verifier;
        // each native platform compares its own original exact text here.
#ifdef _WIN32
        constexpr auto platform = "windows";
#else
        constexpr auto platform = "linux";
#endif
        matrix(root / platform / "conversion_matrix", proof.get("platforms").get(platform));
        collect_lsl_fixed_arrays();
        std::cout << "Matched 5684 original direct-backend conversion range/null/cast/partial-write controls.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
