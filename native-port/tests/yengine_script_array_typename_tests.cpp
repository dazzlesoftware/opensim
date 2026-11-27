// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/framework/dotnet_string_hash.hpp"
#include <array>
#include <iostream>

using namespace utf16_test;

namespace {
using String = std::shared_ptr<const LslStringReference>;
using Class = std::shared_ptr<const LslScriptClass>;
constexpr std::array<std::string_view, 17> names{
    "input",        "other",      "first",       "second",     "third",       "literal",
    "pre_same",     "pre_cross",  "pre_literal", "post_first", "post_second", "post_cross",
    "post_literal", "hash_first", "hash_second", "hash_fresh", "after"};

std::size_t slot(std::string_view name) {
    const auto found = std::find(names.begin(), names.end(), name);
    check(found != names.end(), "Unreviewed original type-name global");
    return static_cast<std::size_t>(found - names.begin());
}

std::int32_t integer(const LslCheckpoint &checkpoint, std::string_view name) {
    return std::get<std::int32_t>(checkpoint.globals.at(slot(name)));
}

String string(const LslCheckpoint &checkpoint, std::string_view name) {
    const auto *value = std::get_if<String>(&checkpoint.globals.at(slot(name)));
    check(value && *value && (*value)->units == u"integer[]", "Type-name String reference/content differs");
    return *value;
}

void compare(const LslCheckpoint &checkpoint, const network::Value &expected, const std::string &where) {
    check(checkpoint.globals.size() == names.size(), where + ": global inventory differs");
    const auto &globals = expected.get("globals");
    check(globals.map.size() == names.size(), where + ": original global inventory differs");
    for (std::size_t i = 6; i < names.size(); ++i) {
        if (names[i].starts_with("hash_"))
            continue;
        check(integer(checkpoint, names[i]) == network::osd_integer(globals.get(std::string(names[i])).get("value")),
              where + ": source result differs: " + std::string(names[i]));
    }
    for (const auto name : {"first", "second", "third", "literal"})
        (void)string(checkpoint, name);
    const auto &pairs = expected.get("object_reference_pairs").array;
    check(pairs.size() == 6, where + ": original String alias inventory differs");
    for (const auto &pair : pairs) {
        const auto left = string(checkpoint, pair.get("left").text);
        const auto right = string(checkpoint, pair.get("right").text);
        check((left == right) == pair.get("same_reference").boolean,
              where + ": original String reference relation differs");
        check(pair.get("object_equals").boolean && left->units == right->units,
              where + ": original String content relation differs");
    }
    const auto input = std::get<Class>(checkpoint.globals[0]);
    const auto other = std::get<Class>(checkpoint.globals[1]);
    check(input && other && input != other && input->descriptor && other->descriptor &&
              same_lsl_script_class_type(*input->descriptor, *other->descriptor) &&
              lsl_script_array_length(input) == 1 && lsl_script_array_length(other) == 2,
          where + ": cross-wrapper class identity/shape differs");
    check(integer(checkpoint, "hash_first") == integer(checkpoint, "hash_second"),
          where + ": equal descriptor name content produced unequal hashes");
}

struct Oracle {
    std::string source;
    network::Value row;
};

Oracle oracle(const std::filesystem::path &root, std::string_view platform) {
    const auto proof_text = read(root / "verification.json");
    check(framework::sha256_hex(proof_text) == "583b3c478b506c813764e656d85243a05514bebc0df08084723dfdefe40602f8",
          "Original type-name identity proof changed");
    const auto proof = network::parse_osd_json(proof_text);
    check(proof.get("passed").boolean && !proof.get("nativeParityVerified").boolean,
          "Original type-name proof scope differs");
    check(framework::sha256_hex(read(root / "Reference.cs")) == proof.get("generatorSha256").text &&
              framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text,
          "Original type-name generator/verifier changed");
    const auto &platform_proof = proof.get("platforms").get(std::string(platform));
    const auto metadata = read(root / platform / "metadata.json");
    check(framework::sha256_hex(metadata) == platform_proof.get("metadataSha256").text,
          "Original type-name metadata changed");
    const auto rows = network::parse_osd_json(metadata);
    check(rows.array.size() == 1, "Original type-name case inventory differs");
    const auto source = read(root / platform / "typename_identity" / "source.lsl");
    check(framework::sha256_hex(source) == platform_proof.get("sourceSha256").text,
          "Original type-name source changed");
    return {source, rows.array[0]};
}

std::shared_ptr<const LslProgram> compile(const Oracle &original) {
    const auto compiled = compile_lsl(original.source);
    check(bool(compiled), "Original type-name identity source rejected");
    return compiled.program;
}

std::unique_ptr<LslInstance> suspend(std::shared_ptr<const LslProgram> program, const LslHost &host) {
    auto instance = std::make_unique<LslInstance>(std::move(program), LslContext{}, host);
    const auto result = instance->run(10000);
    check(result.status == LslRunStatus::Sleeping && result.error.empty(), "Type-name identity source did not Sleep");
    return instance;
}

LslCheckpoint checkpoint(const LslInstance &instance) {
    const auto saved = instance.checkpoint();
    check(saved.has_value(), "Type-name checkpoint missing");
    return *saved;
}

std::unique_ptr<LslInstance> restore(std::shared_ptr<const LslProgram> program, const LslHost &host,
                                     const LslCheckpoint &saved, bool persistent) {
    const auto bytes = persistent ? encode_persistent_lsl_checkpoint(saved) : encode_lsl_checkpoint(saved);
    const auto decoded = decode_lsl_checkpoint(bytes);
    auto instance = std::make_unique<LslInstance>(std::move(program), LslContext{}, host);
    check(decoded && instance->restore_checkpoint(*decoded), "Type-name checkpoint restore rejected");
    return instance;
}

void resume(LslInstance &instance, std::int64_t &clock, const network::Value &expected, std::int32_t saved_hash,
            std::int32_t process_hash, const std::string &where) {
    clock += checkpoint(instance).sleep_remaining + 1;
    const auto result = instance.run(10000);
    check(result.status == LslRunStatus::Idle && result.error.empty(), where + ": resume failed: " + result.error);
    const auto output = checkpoint(instance);
    compare(output, expected, where);
    check(integer(output, "hash_first") == saved_hash && integer(output, "hash_second") == saved_hash,
          where + ": captured scalar hash changed");
    check(integer(output, "hash_fresh") == process_hash, where + ": fresh type-name hash ignored current process");
    check(instance.run(10000).status == LslRunStatus::Idle, where + ": source entry replayed");
}

void local_paths(const std::filesystem::path &root, std::string_view platform) {
    const auto original = oracle(root, platform);
    const auto program = compile(original);
    std::int64_t clock = 100000000;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    auto live = suspend(program, host);
    const auto captured = checkpoint(*live);
    compare(captured, original.row.get("initial"), "native capture");
    const auto saved_hash = integer(captured, "hash_first");
    check(saved_hash == std::bit_cast<std::int32_t>(framework::dotnet_string_hash(u"integer[]")),
          "Descriptor name hash differs from current-process Marvin content hash");
    auto internal = restore(program, host, captured, false);
    compare(checkpoint(*internal), original.row.get("initial"), "internal origin preservation");
    const auto persistent = live->persistent_checkpoint();
    check(persistent.has_value(), "Persistent type-name checkpoint missing");
    auto external = restore(program, host, *persistent, true);
    compare(checkpoint(*external), original.row.get("after_load"), "persistent origin stripping");
    resume(*live, clock, original.row.get("live_resume"), saved_hash, saved_hash, "live type-name resume");
    resume(*internal, clock, original.row.get("live_resume"), saved_hash, saved_hash, "internal type-name resume");
    resume(*external, clock, original.row.get("final"), saved_hash, saved_hash, "persistent type-name resume");
    check(sleeps == 1, "Checkpoint reload replayed type-name prefix effects");
}

void compiled_program_ownership(const std::filesystem::path &root) {
    const auto original = oracle(root, "windows");
    const auto shared_program = compile(original);
    const auto independent_program = compile(original);
    auto whitespace = original;
    whitespace.source = "\n\n" + whitespace.source + "\n";
    const auto whitespace_program = compile(whitespace);
    std::int64_t clock = 100000000;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    const auto first = suspend(shared_program, host);
    const auto second = suspend(shared_program, host);
    const auto independent = suspend(independent_program, host);
    const auto spaced = suspend(whitespace_program, host);
    const auto a = checkpoint(*first);
    const auto b = checkpoint(*second);
    const auto c = checkpoint(*independent);
    const auto d = checkpoint(*spaced);
    const auto shared_name = string(a, "first");
    check(shared_name == string(b, "first"), "Two VMs using one compiled program split descriptor-name identity");
    check(shared_name != string(c, "first") && shared_name != string(d, "first") &&
              string(c, "first") != string(d, "first"),
          "Independent compiled programs merged descriptor-name identity by source/name content");
    check(integer(a, "hash_first") == integer(b, "hash_first") &&
              integer(a, "hash_first") == integer(c, "hash_first") &&
              integer(a, "hash_first") == integer(d, "hash_first"),
          "Equal descriptor-name content hash differed within one process");
}

void foreign_object_ownership(bool whitespace_foreign) {
    const std::string source = R"(YOption allowall;
object wrapper; object name; object foreign; object foreign_name;
integer local_match; integer foreign_match; integer names_distinct; integer cast_after;
object foreign_box; object foreign_list; object foreign_array;
integer[] cast_value; object cast_name;
default {
    state_entry() { integer[] value = new integer[](1); wrapper = value; name = xmrTypeName(wrapper); }
    touch_start(integer count) {
        local_match = (name == (object)xmrTypeName(wrapper));
        foreign_match = (foreign_name == (object)xmrTypeName(foreign));
        names_distinct = (name != foreign_name);
    }
    touch_end(integer count) {
        cast_value = (integer[])foreign;
        cast_after = ((object)cast_value == foreign);
        cast_name = xmrTypeName((object)cast_value);
    }
})";
    const auto a = compile_lsl(source);
    const auto b = compile_lsl(whitespace_foreign ? "\n\n" + source + "\n" : source);
    const auto c = compile_lsl(source);
    check(bool(a) && bool(b) && bool(c), "Cross-code Object ownership source rejected");
    LslHost host;
    LslInstance first(a.program, {}, host);
    LslInstance second(b.program, {}, host);
    check(first.run(10000).status == LslRunStatus::Idle && second.run(10000).status == LslRunStatus::Idle,
          "Cross-code Object ownership entry failed");
    auto saved = checkpoint(first);
    const auto foreign = checkpoint(second);
    check(saved.globals.size() == 13 && foreign.globals.size() == 13,
          "Cross-code Object control global inventory differs");
    const auto local_name = std::get<String>(saved.globals[1]);
    const auto foreign_name = std::get<String>(foreign.globals[1]);
    check(local_name && foreign_name && local_name != foreign_name && local_name->units == foreign_name->units,
          "Independent Object names merged before checkpoint");
    saved.globals[2] = foreign.globals[0];
    saved.globals[3] = foreign.globals[1];
    const auto box = make_lsl_object_box(LslBoxedString{"integer[]"}, foreign_name);
    auto list = std::make_shared<LslList>();
    list->items = {foreign_name, box};
    const std::shared_ptr<const LslList> list_reference = list;
    auto array = std::get<std::shared_ptr<const LslFixedArray>>(make_lsl_fixed_array(LslFixedArrayKind::Object, 4));
    lsl_fixed_array_set(array, 0, foreign.globals[0]);
    lsl_fixed_array_set(array, 1, foreign_name);
    lsl_fixed_array_set(array, 2, box);
    lsl_fixed_array_set(array, 3, list_reference);
    saved.globals[8] = box;
    saved.globals[9] = list_reference;
    saved.globals[10] = array;
    const auto wire = encode_lsl_checkpoint(saved);
    const auto decoded = decode_lsl_checkpoint(wire);
    check(decoded.has_value(), "Cross-code Object graph checkpoint rejected by native codec");
    LslInstance target_control(c.program, {}, host);
    check(target_control.run(10000).status == LslRunStatus::Idle, "Target-program Object ownership control failed");
    const auto target_name = std::get<String>(checkpoint(target_control).globals[1]);
    LslInstance restored(c.program, {}, host);
    check(restored.restore_checkpoint(*decoded), "Generic Object roots rejected foreign compiled-class ownership");
    const auto loaded = checkpoint(restored);
    check(std::get<String>(loaded.globals[1]) == target_name &&
              std::get<String>(loaded.globals[1]) != std::get<String>(loaded.globals[3]),
          "Internal reload failed to rebind only the captured owner to the target compiled program");
    check(std::get<String>(loaded.globals[3]) == foreign_name,
          "Foreign owner cache was detached from its still-live compiled program");
    const auto loaded_box = std::get<std::shared_ptr<const LslObjectBox>>(loaded.globals[8]);
    const auto loaded_list = std::get<std::shared_ptr<const LslList>>(loaded.globals[9]);
    const auto loaded_array = std::get<std::shared_ptr<const LslFixedArray>>(loaded.globals[10]);
    check(loaded_box && loaded_box->string_field == foreign_name && loaded_list && loaded_list->items.size() == 2 &&
              std::get<String>(loaded_list->items[0]) == foreign_name &&
              std::get<std::shared_ptr<const LslObjectBox>>(loaded_list->items[1]) == loaded_box,
          "Foreign String owner was lost through SDK box or List graph edges");
    check(loaded_array && lsl_fixed_array_size(loaded_array) == 4 &&
              std::get<Class>(lsl_fixed_array_get(loaded_array, 0)) == std::get<Class>(loaded.globals[2]) &&
              std::get<String>(lsl_fixed_array_get(loaded_array, 1)) == foreign_name &&
              std::get<std::shared_ptr<const LslObjectBox>>(lsl_fixed_array_get(loaded_array, 2)) == loaded_box &&
              std::get<std::shared_ptr<const LslList>>(lsl_fixed_array_get(loaded_array, 3)) == loaded_list,
          "Foreign class/String/box/List aliases split inside an Object backing array");
    check(restored.post_event({"touch_start", {std::int32_t{1}}}), "Cross-code Object observation event rejected");
    const auto observed = restored.run(10000);
    check(observed.status == LslRunStatus::Idle && observed.error.empty(),
          "Foreign wrapper type-name observation faulted: " + observed.error);
    const auto result = checkpoint(restored);
    for (const auto index : {4u, 5u, 6u})
        check(std::get<std::int32_t>(result.globals[index]) == 1,
              "Checkpoint remapped a foreign owner or detached its cached name from its wrapper");
    check(restored.post_event({"touch_end", {std::int32_t{1}}}), "Cross-code cast event rejected");
    const auto cast = restored.run(10000);
    check(cast.status == LslRunStatus::Idle && cast.error.empty(),
          "Same-index foreign generated-array cast failed: " + cast.error);
    const auto cast_state = checkpoint(restored);
    check(std::get<std::int32_t>(cast_state.globals[7]) == 1 &&
              std::get<Class>(cast_state.globals[11]) == std::get<Class>(cast_state.globals[2]) &&
              std::get<String>(cast_state.globals[12]) == foreign_name,
          "Foreign typed cast changed wrapper reference or metadata-name ownership");
    auto internal = restore(c.program, host, cast_state, false);
    const auto internal_state = checkpoint(*internal);
    check(std::get<Class>(internal_state.globals[11]) == std::get<Class>(internal_state.globals[2]) &&
              std::get<String>(internal_state.globals[12]) == foreign_name,
          "Internal checkpoint rejected or detached a foreign typed global");
    const auto external_state = restored.persistent_checkpoint();
    check(external_state.has_value(), "Foreign typed global persistent checkpoint missing");
    auto external = restore(c.program, host, *external_state, true);
    const auto external_loaded = checkpoint(*external);
    check(std::get<Class>(external_loaded.globals[11]) == std::get<Class>(external_loaded.globals[2]),
          "Persistent checkpoint split typed/Object foreign wrapper aliases");
    check(external->post_event({"touch_end", {std::int32_t{1}}}), "Persistent foreign cast event rejected");
    const auto external_cast = external->run(10000);
    check(external_cast.status == LslRunStatus::Idle && external_cast.error.empty(),
          "Persistent foreign typed global failed to rebind class-index metadata");
    const auto external_result = checkpoint(*external);
    check(std::get<Class>(external_result.globals[11]) == std::get<Class>(external_result.globals[2]) &&
              std::get<String>(external_result.globals[12]) == target_name,
          "External class-index rebind did not use target-program metadata name");

    auto wrong = cast_state;
    wrong.globals[2] = make_lsl_reference_string(u"wrong");
    auto invalid = restore(c.program, host, wrong, false);
    check(invalid->post_event({"touch_end", {std::int32_t{1}}}), "Raw String cast control event rejected");
    const auto fault = invalid->run(10000);
    check(fault.status == LslRunStatus::Faulted &&
              fault.error == "InvalidCastException: Unable to cast object of type 'System.String' to type "
                             "'OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj'.",
          "Raw String generated-class cast fault differs from original: " + fault.error);
}

void descriptor_cache_conflicts() {
    const auto compiled = compile_lsl("YOption allowall; object first; object second; default{state_entry(){}}");
    check(bool(compiled), "Descriptor cache admission control rejected");
    const std::int64_t clock = 638000000000000000LL;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    LslInstance initial(compiled.program, {}, host);
    check(initial.run(10000).status == LslRunStatus::Idle, "Descriptor cache control entry failed");
    const auto base = checkpoint(initial);
    const auto owner = framework::Uuid::random();
    auto foreign = [&](std::int32_t index) {
        auto descriptor = std::make_shared<LslScriptClassDescriptor>(
            *make_lsl_script_array_descriptor("foreign-source", "integer", 1));
        descriptor->class_index = index;
        descriptor->type_name_owner = owner;
        const std::array dimensions{std::int32_t{1}};
        return make_lsl_script_array(descriptor, dimensions);
    };
    for (const auto indices : {std::array<std::int32_t, 2>{0, 1}, std::array<std::int32_t, 2>{1, 0}}) {
        auto saved = base;
        saved.globals = {foreign(indices[0]), foreign(indices[1])};
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
        check(decoded.has_value(), "Structurally valid conflicting-index checkpoint was rejected by the codec");
        LslInstance target(compiled.program, {}, host);
        const auto before = encode_lsl_checkpoint(checkpoint(target));
        check(!target.restore_checkpoint(*decoded),
              "Same foreign owner/type key merged two distinct known class indices");
        check(encode_lsl_checkpoint(checkpoint(target)) == before,
              "Rejected class-index conflict changed the receiving VM");
    }
    for (const auto indices :
         {std::array<std::int32_t, 2>{0, 0}, std::array<std::int32_t, 2>{0, -1}, std::array<std::int32_t, 2>{-1, 0}}) {
        auto saved = base;
        saved.globals = {foreign(indices[0]), foreign(indices[1])};
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
        LslInstance target(compiled.program, {}, host);
        check(decoded && target.restore_checkpoint(*decoded),
              "Compatible known/legacy descriptor indices were rejected");
        const auto accepted = checkpoint(target);
        const auto first = std::get<Class>(accepted.globals[0]);
        const auto second = std::get<Class>(accepted.globals[1]);
        check(first && second && first != second && same_lsl_script_class_type(*first->descriptor, *second->descriptor),
              "Compatible descriptor admission merged wrapper instances or changed their type");
    }
}

void write_capture(const std::filesystem::path &root, const std::filesystem::path &destination) {
    const auto original = oracle(root, "windows");
    const auto program = compile(original);
    std::int64_t clock = 100000000;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    auto instance = suspend(program, host);
    compare(checkpoint(*instance), original.row.get("initial"), "fresh-process capture producer");
    const auto saved = instance->persistent_checkpoint();
    check(saved.has_value(), "Fresh-process persistent checkpoint missing");
    const auto write = [](const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        check(bool(output), "Cannot create fresh-process type-name checkpoint");
        output.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        check(bool(output), "Cannot write fresh-process type-name checkpoint");
    };
    write(destination, encode_persistent_lsl_checkpoint(*saved));
    auto internal_destination = destination;
    internal_destination += ".internal";
    write(internal_destination, encode_lsl_checkpoint(checkpoint(*instance)));
}

void read_capture(const std::filesystem::path &root, const std::filesystem::path &source, bool internal) {
    const auto original = oracle(root, "windows");
    const auto program = compile(original);
    std::int64_t clock = 100000000;
    unsigned sleeps = 0;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](std::int32_t) { ++sleeps; };
    // A fresh source run supplies this process's String hash. Stored Int32 hash
    // fields belong to the producer and must never be rewritten on import.
    const auto control = suspend(program, host);
    const auto process_hash = integer(checkpoint(*control), "hash_first");
    check(process_hash == std::bit_cast<std::int32_t>(framework::dotnet_string_hash(u"integer[]")),
          "Fresh descriptor hash differs from current-process Marvin content hash");
    const auto wire = read(source);
    const auto decoded = decode_lsl_checkpoint({reinterpret_cast<const std::uint8_t *>(wire.data()), wire.size()});
    check(decoded.has_value(), "Fresh-process type-name checkpoint codec rejected");
    const auto saved_hash = integer(*decoded, "hash_first");
    LslInstance imported(program, {}, host);
    check(imported.restore_checkpoint(*decoded), "Fresh-process native type-name checkpoint restore rejected");
    compare(checkpoint(imported), original.row.get(internal ? "initial" : "after_load"),
            internal ? "fresh-process internal import" : "fresh-process persistent import");
    resume(imported, clock, original.row.get(internal ? "live_resume" : "final"), saved_hash, process_hash,
           internal ? "fresh-process internal type-name resume" : "fresh-process persistent type-name resume");
    check(sleeps == 1, "Fresh-process reload replayed a source Sleep");
    std::cout << "Preserved captured hash " << saved_hash << "; current-process hash " << process_hash << '\n';
}

void conflicting_metadata_is_rejected() {
    const auto compiled = compile_lsl("YOption allowall; object first; object second; default{state_entry(){}}");
    check(bool(compiled), "Conflicting metadata control did not compile");
    LslHost host;
    LslInstance instance(compiled.program, {}, host);
    check(instance.run(10000).status == LslRunStatus::Idle, "Metadata control did not initialize");
    const auto before = checkpoint(instance);
    auto descriptor = std::make_shared<LslScriptClassDescriptor>();
    descriptor->source_hash = "foreign-code";
    descriptor->canonical_name = "Example";
    descriptor->class_index = 0;
    descriptor->type_name_owner = framework::Uuid("6e7544aa-67f1-4b99-af10-9fb37b301b29");
    auto other = std::make_shared<LslScriptClassDescriptor>(*descriptor);
    other->instance_sizes[5] = 1;
    auto candidate = before;
    candidate.globals = {make_lsl_script_class(descriptor), make_lsl_script_class(other)};
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(candidate));
    check(bool(decoded), "Individually valid descriptor records did not decode");
    check(!instance.restore_checkpoint(*decoded), "Conflicting metadata in one loaded owner was admitted");
    const auto after = checkpoint(instance);
    check(std::holds_alternative<LslNullObject>(after.globals[0]) &&
              std::holds_alternative<LslNullObject>(after.globals[1]),
          "Rejected metadata changed existing VM globals");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2) {
            local_paths(argv[1], "windows");
            local_paths(argv[1], "linux");
            compiled_program_ownership(argv[1]);
            conflicting_metadata_is_rejected();
            foreign_object_ownership(false);
            foreign_object_ownership(true);
            descriptor_cache_conflicts();
        } else if (argc == 4 && std::string_view(argv[1]) == "--capture")
            write_capture(argv[2], argv[3]);
        else if (argc == 4 && std::string_view(argv[1]) == "--resume")
            read_capture(argv[2], argv[3], false);
        else if (argc == 4 && std::string_view(argv[1]) == "--resume-internal")
            read_capture(argv[2], argv[3], true);
        else
            throw std::runtime_error(
                "Expected fixture directory, or --capture/--resume/--resume-internal fixture checkpoint");
        collect_lsl_fixed_arrays();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
