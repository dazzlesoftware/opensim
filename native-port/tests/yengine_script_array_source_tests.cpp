// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include <iostream>
#include <map>
#include <memory>

using namespace utf16_test;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

std::vector<std::string> declarations(std::string source) {
    source = global_declaration_source(std::move(source));
    const std::regex declaration(
        R"(\b(?:integer\s*\[\s*\]|integer|object|string)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::vector<std::string> names;
    for (auto it = std::sregex_iterator(source.begin(), source.end(), declaration); it != std::sregex_iterator(); ++it)
        names.push_back((*it)[1].str());
    return names;
}

struct GraphComparison {
    std::map<std::int32_t, const void *> references;
    std::map<const void *, std::int32_t> reverse;

    void reference(std::int32_t id, const void *actual, const std::string &where) {
        check(id > 0 && actual, where + ": invalid reference");
        const auto [forward, inserted] = references.emplace(id, actual);
        check(inserted || forward->second == actual, where + ": original aliases split");
        const auto [backward, new_identity] = reverse.emplace(actual, id);
        check(new_identity || backward->second == id, where + ": distinct original references merged");
    }

    void value(const LslValue &actual, const network::Value &expected, const std::string &where) {
        const auto type = expected.get("type").text;
        if (type == "null") {
            check(std::holds_alternative<LslNullObject>(actual), where + ": null Object differs");
        } else if (type == "integer") {
            const auto *integer = std::get_if<std::int32_t>(&actual);
            check(integer && *integer == network::osd_integer(expected.get("value")), where + ": Int32 differs");
        } else if (type == "string") {
            const auto units = lsl_string_code_units(actual);
            const auto &expected_units = expected.get("utf16").array;
            check(units && units->size() == expected_units.size(), where + ": string length differs");
            for (std::size_t i = 0; i < expected_units.size(); ++i)
                check((*units)[i] == network::osd_integer(expected_units[i]), where + ": UTF16 unit differs");
        } else if (type == "script_class" || type == "script_class_reference") {
            const auto *object = std::get_if<Class>(&actual);
            check(object && *object, where + ": generated class reference missing");
            reference(network::osd_integer(expected.get("reference_id")), object->get(), where);
            if (type == "script_class_reference")
                return;
            const auto &instance = **object;
            check(instance.descriptor && instance.descriptor->canonical_name == expected.get("script_type").text &&
                      instance.descriptor->element_type == expected.get("element_type").text &&
                      instance.descriptor->array_rank == network::osd_integer(expected.get("rank")),
                  where + ": generated type descriptor differs");
            const auto &dimensions = expected.get("integers").array;
            check(instance.instance_arrays[3].size() == dimensions.size(), where + ": dimension count differs");
            for (std::size_t i = 0; i < dimensions.size(); ++i)
                check(std::get<std::int32_t>(instance.instance_arrays[3][i]) == network::osd_integer(dimensions[i]),
                      where + ": public dimension differs");
            const auto &objects = expected.get("objects").array;
            check(instance.instance_arrays[5].size() == objects.size(), where + ": Object field count differs");
            for (std::size_t i = 0; i < objects.size(); ++i)
                value(instance.instance_arrays[5][i], objects[i], where + ".obj");
        } else if (type == "clr_array" || type == "array_reference") {
            const auto *array = std::get_if<Array>(&actual);
            check(array && *array, where + ": raw backing missing");
            reference(network::osd_integer(expected.get("reference_id")), array->get(), where);
            if (type == "array_reference")
                return;
            check((*array)->kind == LslFixedArrayKind::Integer, where + ": raw backing type differs");
            const auto &items = expected.get("items").array;
            check(lsl_fixed_array_size(*array) == items.size(), where + ": backing length differs");
            for (std::size_t i = 0; i < items.size(); ++i)
                value(lsl_fixed_array_get(*array, i), items[i], where + "[" + std::to_string(i) + "]");
        } else
            throw std::runtime_error(where + ": unreviewed original observation " + type);
    }
};

void compare(const LslCheckpoint &checkpoint, const std::vector<std::string> &names, const network::Value &expected,
             const std::string &where) {
    const auto &globals = expected.get("globals");
    check(globals.kind == network::Value::Kind::Map && names.size() == checkpoint.globals.size() &&
              globals.map.size() == names.size(),
          where + ": global inventory differs");
    GraphComparison graph;
    for (std::size_t i = 0; i < names.size(); ++i)
        graph.value(checkpoint.globals[i], globals.get(names[i]), where + "." + names[i]);
}

std::unique_ptr<LslInstance> restore(std::shared_ptr<const LslProgram> program, const LslHost &host,
                                     const LslCheckpoint &checkpoint, bool persistent) {
    const auto encoded = persistent ? encode_persistent_lsl_checkpoint(checkpoint) : encode_lsl_checkpoint(checkpoint);
    const auto decoded = decode_lsl_checkpoint(encoded);
    auto instance = std::make_unique<LslInstance>(std::move(program), LslContext{}, host);
    check(decoded && instance->restore_checkpoint(*decoded), "Native array checkpoint restore rejected");
    return instance;
}

void source_case(const std::filesystem::path &folder, const network::Value &proof_row) {
    const auto source = read(folder / "source.lsl");
    const auto metadata_text = read(folder / "metadata.json");
    check(framework::sha256_hex(source) == proof_row.get("sourceSha256").text &&
              framework::sha256_hex(metadata_text) == proof_row.get("metadataSha256").text,
          "Original array source/metadata artifact changed: " + folder.string());
    const auto metadata = network::parse_osd_json(metadata_text);
    check(metadata.get("accepted").boolean && metadata.get("suspended").boolean,
          "Original array source control was not accepted and suspended");
    const auto names = declarations(source);
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &diagnostic : compiled.diagnostics)
        diagnostics += " " + diagnostic.message;
    check(bool(compiled), folder.filename().string() + ": native source rejected:" + diagnostics);
    std::int64_t clock = 100000000;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    auto live = std::make_unique<LslInstance>(compiled.program, LslContext{}, host);
    bool suspended = false;
    for (unsigned step = 0; step < 10000; ++step) {
        const auto result = live->run(1);
        check(result.error.empty(), "Array source faulted before capture: " + result.error);
        if (result.status == LslRunStatus::Sleeping) {
            suspended = true;
            break;
        }
        check(result.status != LslRunStatus::Idle, "Array source completed before expected Sleep");
    }
    check(suspended && sleeps == 1, "Array source did not suspend exactly once");
    const auto captured = live->checkpoint();
    check(captured && captured->sleep_remaining > 0, "Array Sleep checkpoint missing");
    compare(*captured, names, metadata.get("initial"), folder.filename().string() + " native capture");
    auto internal = restore(compiled.program, host, *captured, false);
    compare(*internal->checkpoint(), names, metadata.get("initial"), "internal array reload");
    const auto persistent = live->persistent_checkpoint();
    check(persistent.has_value(), "Array persistent checkpoint unavailable");
    auto external = restore(compiled.program, host, *persistent, true);
    compare(*external->checkpoint(), names, metadata.get("after_load"), "persistent array reload");
    std::array<std::unique_ptr<LslInstance>, 3> paths{std::move(live), std::move(internal), std::move(external)};
    for (std::size_t path = 0; path < paths.size(); ++path) {
        auto &instance = paths[path];
        clock += instance->checkpoint()->sleep_remaining + 1;
        const auto &expected = metadata.get(path == 2 ? "final" : "live_resume");
        const auto &expected_fault = metadata.get(path == 2 ? "resume_exception" : "live_resume_exception");
        auto previous = instance->checkpoint();
        bool finished = false;
        for (unsigned step = 0; step < 10000; ++step) {
            const auto result = instance->run(1);
            if (result.status == LslRunStatus::Faulted) {
                check(expected_fault.get("type").text == "System.IndexOutOfRangeException",
                      "Unexpected native array fault: " + result.error);
                check(result.error == "IndexOutOfRangeException: Index was outside the bounds of the array.",
                      "Native array bounds exception category/text differs: " + result.error);
                check(previous.has_value(), "Array pre-fault checkpoint missing");
                compare(*previous, names, expected, "array state before expected bounds fault");
                finished = true;
                break;
            }
            check(result.error.empty() && result.status != LslRunStatus::Sleeping,
                  "Array resume replayed Sleep or faulted: " + result.error);
            const auto current = instance->checkpoint();
            check(current.has_value(), "Array resume checkpoint missing");
            if (result.status == LslRunStatus::Idle) {
                check(expected_fault.kind != network::Value::Kind::Map, "Original array fault became success");
                compare(*current, names, expected, "native array resumed output");
                check(instance->run(1).status == LslRunStatus::Idle && sleeps == 1, "Array resume replayed effects");
                finished = true;
                break;
            }
            previous = current;
        }
        check(finished, "Array resume exceeded execution bound");
    }
}

// Native type-safety controls supplement the measured fixtures; these are not
// additional original-runtime oracle cases.
void typed_controls() {
    const auto valid = compile_lsl(R"(YOption allowall;
integer[] output; integer done;
integer[] Pass(integer[] value) { return value; }
integer Choose(integer[] value) { return 7; }
integer Choose(object value) { return 9; }
default { state_entry() {
    integer[] local = new integer[](1);
    local[0] = 37;
    output = Pass(local);
    done = (output == local) && (Choose(output) == 7) && (output[0] == 37);
} })");
    check(bool(valid), "Typed array parameters/return/local/overload control rejected");
    LslHost host;
    LslInstance instance(valid.program, {}, host);
    const auto result = instance.run(10000);
    check(result.status == LslRunStatus::Idle && result.error.empty(), "Typed array function control faulted");
    const auto checkpoint = instance.checkpoint();
    check(checkpoint && checkpoint->globals.size() == 2 && std::get<std::int32_t>(checkpoint->globals[1]) == 1,
          "Typed array function descriptors or overload resolution lost");
    const std::array invalid{
        "YOption allowall; integer[] input; default{state_entry(){input=1;}}",
        "YOption allowall; integer[] Pass(integer[] input){return input;} default{state_entry(){Pass(1);}}",
        "YOption allowall; integer[] Pass(){return 1;} default{state_entry(){}}",
        "YOption allowall; integer[] input; default{state_entry(){input=(integer[)1;}}",
        "YOption allowall; integer[] input; default{state_entry(){input.Get(0)=7;}}",
        "YOption allowall; integer[] input; default{state_entry(){input.Get(0)++;}}",
    };
    for (const auto source : invalid)
        check(!compile_lsl(source), "Unsupported or incompatible array declaration/cast accepted");
    // The original float and rectangular-copy oracles demonstrate these paths.
    check(bool(compile_lsl("YOption allowall; float[] input; "
                           "default{state_entry(){input=new float[](1);input.Set(0,1.25);}}")),
          "Measured float-array constructor/Set source control rejected");
    check(bool(compile_lsl("YOption allowall; integer[,] input; "
                           "default{state_entry(){input=new integer[,](1,2);input[0,1]=22;}}")),
          "Measured rectangular-array source control rejected");
    const auto cast = compile_lsl(R"(YOption allowall;
object boxed; integer[] input; integer after;
default { state_entry() { boxed = 37; input = (integer[])boxed; after = 1; } })");
    check(bool(cast), "Explicit Object-to-array cast control rejected at compile time");
    LslInstance wrong(cast.program, {}, host);
    const auto fault = wrong.run(10000);
    check(fault.status == LslRunStatus::Faulted && fault.error.starts_with("InvalidCastException:"),
          "Wrong Object payload passed a typed array cast");
}

Class incompatible_class(const Class &original, unsigned variant, bool preserve_owner = false) {
    check(original && original->descriptor, "Typed checkpoint control lacks class descriptor");
    auto source = original->descriptor->source_hash;
    auto element = original->descriptor->element_type;
    auto rank = original->descriptor->array_rank;
    if (variant == 0)
        source = source == std::string(source.size(), 'a') ? std::string(source.size(), 'b')
                                                           : std::string(source.size(), 'a');
    else if (variant == 1)
        element = "float";
    else
        rank = 2;
    auto descriptor = std::make_shared<LslScriptClassDescriptor>(
        *make_lsl_script_array_descriptor(std::move(source), std::move(element), rank));
    if (preserve_owner) {
        check(original->descriptor->class_index == 0 && !original->descriptor->type_name_owner.is_zero(),
              "Known-index corruption control lacks its compiled owner");
        descriptor->class_index = original->descriptor->class_index;
        descriptor->type_name_owner = original->descriptor->type_name_owner;
    }
    std::vector<std::int32_t> dimensions(rank, 1);
    return std::get<Class>(make_lsl_script_array(descriptor, dimensions));
}

LslCheckpoint codec_roundtrip(const LslCheckpoint &checkpoint) {
    const auto encoded = encode_lsl_checkpoint(checkpoint);
    const auto decoded = decode_lsl_checkpoint(encoded);
    check(decoded.has_value(), "Structurally valid class checkpoint was rejected by native codec");
    return *decoded;
}

void checkpoint_admission() {
    const auto compiled = compile_lsl(R"(YOption allowall;
integer[] input; object opaque; integer after;
default { state_entry() {
    integer[] local = new integer[](1);
    local[0] = 19; input = local; opaque = local;
    llSleep(0.1); after = local[0];
} })");
    check(bool(compiled), "Typed checkpoint control did not compile");
    std::int64_t clock = 100000000;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    LslInstance instance(compiled.program, {}, host);
    check(instance.run(10000).status == LslRunStatus::Sleeping, "Typed checkpoint control did not suspend");
    const auto saved = instance.checkpoint();
    check(saved && saved->globals.size() == 3 && saved->locals.size() == 1,
          "Typed global/local checkpoint roots missing");
    const auto original = std::get<Class>(saved->globals[0]);
    {
        check(original->descriptor && original->descriptor->class_index == 0 &&
                  !original->descriptor->type_name_owner.is_zero(),
              "Object descriptor-index control lacks its compiled owner");
        auto descriptor = std::make_shared<LslScriptClassDescriptor>(*original->descriptor);
        descriptor->class_index = 1;
        const std::array dimensions{std::int32_t{1}};
        auto candidate = *saved;
        // Keep the typed global and local intact: the conflicting descriptor
        // enters solely through an otherwise valid generic Object root.
        candidate.globals[1] = make_lsl_script_array(descriptor, dimensions);
        LslInstance target(compiled.program, {}, host);
        const auto before = target.checkpoint();
        check(before.has_value(), "Object descriptor-index target checkpoint missing");
        const auto original_state = encode_lsl_checkpoint(*before);
        check(!target.restore_checkpoint(codec_roundtrip(candidate)),
              "Generic Object root rebound a conflicting known index to local metadata");
        const auto after = target.checkpoint();
        check(after && encode_lsl_checkpoint(*after) == original_state,
              "Rejected local descriptor-index conflict changed the receiving VM");
    }
    for (unsigned variant = 0; variant < 3; ++variant) {
        for (const bool preserve_owner : {false, true}) {
            const auto incompatible = incompatible_class(original, variant, preserve_owner);
            for (const bool local : {false, true}) {
                auto candidate = *saved;
                (local ? candidate.locals[0] : candidate.globals[0]) = incompatible;
                LslInstance target(compiled.program, {}, host);
                check(!target.restore_checkpoint(codec_roundtrip(candidate)),
                      local ? "Typed local accepted mismatched class descriptor"
                            : "Typed global accepted mismatched class descriptor");
            }
        }
        // A generic Object can retain any structurally valid class. Admission
        // must inspect the declared slot rather than prohibit foreign carriers.
        auto object = *saved;
        const auto incompatible = incompatible_class(original, variant);
        object.globals[1] = incompatible;
        LslInstance generic(compiled.program, {}, host);
        check(generic.restore_checkpoint(codec_roundtrip(object)),
              "Generic Object slot rejected a legitimate class carrier");
        const auto accepted = generic.checkpoint();
        check(accepted && same_lsl_script_class_type(*std::get<Class>(accepted->globals[1])->descriptor,
                                                     *incompatible->descriptor),
              "Generic Object descriptor was changed during checkpoint admission");
    }

    const auto evaluation = compile_lsl(R"(YOption allowall;
integer[] input; integer after;
integer Index() { llSleep(0.1); return 0; }
default { state_entry() { input = new integer[](1); input[0] = 23; after = input[Index()]; } })");
    check(bool(evaluation), "Typed evaluation checkpoint control did not compile");
    LslInstance evaluating(evaluation.program, {}, host);
    check(evaluating.run(10000).status == LslRunStatus::Sleeping,
          "Typed evaluation checkpoint control did not suspend");
    const auto journal = evaluating.checkpoint();
    check(journal.has_value(), "Typed evaluation checkpoint missing");
    auto replace_evaluation = [](LslCheckpoint &checkpoint, const Class &replacement) {
        auto replace = [&](std::vector<LslEvaluationCheckpoint> &values) {
            for (auto &entry : values)
                if (std::holds_alternative<Class>(entry.value)) {
                    entry.value = replacement;
                    return true;
                }
            return false;
        };
        if (replace(checkpoint.evaluations))
            return true;
        for (auto &frame : checkpoint.call_stack)
            if (replace(frame.evaluations))
                return true;
        return false;
    };
    const auto receiver = std::get<Class>(journal->globals[0]);
    for (unsigned variant = 0; variant < 3; ++variant) {
        for (const bool preserve_owner : {false, true}) {
            auto candidate = *journal;
            check(replace_evaluation(candidate, incompatible_class(receiver, variant, preserve_owner)),
                  "Suspended array receiver did not retain a typed evaluation root");
            LslInstance target(evaluation.program, {}, host);
            check(!target.restore_checkpoint(codec_roundtrip(candidate)),
                  "Typed evaluation accepted mismatched class descriptor");
        }
    }
}

void fixture(const std::filesystem::path &root, std::string_view expected_hash, std::string_view suffix,
             unsigned expected_cases) {
    const auto proof_text = read(root / "verification.json");
    check(framework::sha256_hex(proof_text) == expected_hash, "Original source-array proof changed");
    const auto proof = network::parse_osd_json(proof_text);
    check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
          "Original array proof scope changed");
    unsigned cases = 0;
    for (const auto platform : {"windows", "linux"}) {
        const auto &rows = proof.get("platforms").get(platform).get("cases").array;
        check(rows.size() == expected_cases, "Original source-array case inventory differs");
        auto platform_root = root / platform;
        if (!std::filesystem::exists(platform_root))
            platform_root = root / (std::string(platform) + std::string(suffix));
        for (const auto &row : rows) {
            source_case(platform_root / row.get("name").text, row);
            ++cases;
        }
    }
    check(cases == expected_cases * 2, "Original source-array platform coverage incomplete");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 4, "Expected base-array, lvalue and receiver fixture directories");
        fixture(argv[1], "d8277e63982ff02c77e878ab7a61e45b22c5a97b9c97bc482766720dc4143311", "", 4);
        fixture(argv[2], "1db57d484e53fcb2db5a15eeaa9d46f9478622df96aa3cbe2a7ac092be80778d", "-frozen-observations", 4);
        fixture(argv[3], "6218c0c087c12eb9610a34b36233f2f7f16b21678493cab0d2316dffd202a732", "-observed", 5);
        typed_controls();
        checkpoint_admission();
        collect_lsl_fixed_arrays();
        std::cout << "Matched 26 original source controls through native execution, internal checkpoints and "
                     "persistent reload.\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
