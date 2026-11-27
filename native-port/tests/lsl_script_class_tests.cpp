// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_script_class.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace opensim::script;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <class F> void rejects(F &&f, const char *message) {
    bool rejected = false;
    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, message);
}

Class integers(std::int32_t length) {
    const std::array dimensions{length};
    return std::get<Class>(make_lsl_script_array(make_lsl_script_array_descriptor("source", "integer", 1), dimensions));
}

void storage() {
    auto value = integers(3);
    auto alias = value;
    check(value == alias, "Class alias lost identity");
    check(value->arrays_heap_usage == 16, "Integer array constructor ledger changed");
    check(lsl_script_array_length(value) == 3, "Length differs from dimension");
    const std::array index{std::int32_t{2}};
    lsl_script_array_set(value, index, std::int32_t{11});
    lsl_script_class_store_field(value, 3, 0, std::int32_t{1});
    check(lsl_script_array_length(value) == 1 && lsl_script_array_length(value, -1) == 0 &&
              lsl_script_array_length(value, 1) == 0,
          "Public dimension semantics changed");
    check(std::get<std::int32_t>(lsl_script_array_get(alias, index)) == 11,
          "Public dimension incorrectly restricted backing access");
    rejects(
        [&] {
            const std::array outside{std::int32_t{3}};
            (void)lsl_script_array_get(value, outside);
        },
        "Backing bounds ignored");
    lsl_script_class_store_field(value, 5, 0, std::string{"wrong"});
    check(value->arrays_heap_usage == 14, "Object field string charge differs from original");
    rejects([&] { (void)lsl_script_array_get(value, index); }, "Wrong Object backing accepted");
    check(lsl_script_class_object_size(value) == 0, "Class storage aggregated as Object size");
    auto zero = integers(0);
    check(zero && zero->arrays_heap_usage == 4, "Zero-length wrapper disappeared");
    auto backing = std::get<Array>(lsl_script_class_field(zero, 5, 0));
    check(backing && lsl_fixed_array_size(backing) == 0, "Live zero-length backing normalized early");
}

void rectangular_dimensions() {
    const auto descriptor = make_lsl_script_array_descriptor("source", "integer", 2);
    const auto same = make_lsl_script_array_descriptor("source", "integer", 2);
    const auto other = make_lsl_script_array_descriptor("other-source", "integer", 2);
    check(same_lsl_script_class_type(*descriptor, *same) && !same_lsl_script_class_type(*descriptor, *other),
          "Logical class type identity changed");
    const std::array dimensions{std::int32_t{2}, std::int32_t{3}};
    auto value = std::get<Class>(make_lsl_script_array(descriptor, dimensions));
    const std::array beyond_first_dimension{std::int32_t{0}, std::int32_t{4}};
    check(lsl_script_array_index(value, beyond_first_dimension) == 4, "Index added a per-dimension bounds check");
    lsl_script_array_set(value, beyond_first_dimension, std::int32_t{27});
    const std::array equivalent{std::int32_t{1}, std::int32_t{1}};
    check(std::get<std::int32_t>(lsl_script_array_get(value, equivalent)) == 27, "Rectangular index is not row-major");
}

void heap_checks() {
    check(lsl_script_class_object_size(std::string{"   "}) == 6, "Class Object sizing discarded whitespace");
    auto value = integers(3);
    const auto old = lsl_script_class_field(value, 5, 0);
    LslScriptClassHeapHooks deny;
    deny.check_delta = [](std::int32_t) { throw std::runtime_error("heap limit"); };
    rejects([&] { lsl_script_class_store_field(value, 5, 0, std::string{"wrong"}, deny); },
            "Heap hook did not reject assignment");
    check(std::get<Array>(lsl_script_class_field(value, 5, 0)) == std::get<Array>(old) &&
              value->arrays_heap_usage == 16,
          "Rejected assignment mutated class");
    auto backing = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Object, 1));
    lsl_fixed_array_set(backing, 0, std::string{"a"});
    lsl_script_class_store_field(value, 5, 0, backing);
    const auto charge = value->arrays_heap_usage;
    lsl_fixed_array_set(backing, 0, std::string{"longer"});
    std::int32_t delta = 999;
    LslScriptClassHeapHooks observe;
    observe.check_delta = [&](std::int32_t amount) { delta = amount; };
    lsl_script_class_store_field(value, 5, 0, backing, observe);
    check(delta == 0 && value->arrays_heap_usage == charge, "Self assignment repaired stale class ledger");
}

void runtime_object_reference() {
    const auto compiled = compile_lsl(R"(YOption allowall;
object input; object alias; string kind; string text; integer same; integer hashes;
default {
    state_entry() { }
    touch_start(integer count) {
        kind = xmrTypeName(input);
        text = (string)input;
        same = input == alias;
        hashes = xmrHashCode(input) == xmrHashCode(alias);
    }
})");
    check(bool(compiled), "Class Object integration control did not compile");
    LslHost host;
    LslInstance instance(compiled.program, {}, host);
    (void)instance.run(10000);
    auto state = instance.checkpoint();
    check(bool(state) && state->globals.size() == 6, "Object control checkpoint missing");
    auto original = integers(3);
    state->globals[0] = original;
    state->globals[1] = original;
    check(instance.restore_checkpoint(*state), "Object global rejected class reference");
    check(instance.post_event({"touch_start", {std::int32_t{1}}}), "Object control event rejected");
    const auto result = instance.run(10000);
    check(result.error.empty(), "Class Object integration faulted");
    const auto output = instance.checkpoint();
    check(bool(output) && lsl_string_code_units(output->globals[2]) == u"integer[]" &&
              lsl_string_code_units(output->globals[3]) == u"OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj" &&
              std::get<std::int32_t>(output->globals[4]) == 1 && std::get<std::int32_t>(output->globals[5]) == 1,
          "Class Object identity/type/string behavior changed");
    const auto restored = std::get<Class>(output->globals[0]);
    check(restored != original && restored == std::get<Class>(output->globals[1]),
          "Runtime restore did not detach class graph while retaining aliases");
}

void mixed_graph() {
    std::weak_ptr<const LslScriptClass> weak_class;
    std::weak_ptr<const LslFixedArray> weak_array;
    std::weak_ptr<const LslList> weak_list;
    {
        auto value = integers(1);
        auto backing = std::get<Array>(make_lsl_fixed_array(LslFixedArrayKind::Object, 1));
        auto list = std::make_shared<LslList>();
        list->items = {value};
        // Assign before mutation: List.Size rejects class entries when charged.
        lsl_script_class_store_field(value, 5, 0, backing);
        lsl_fixed_array_set(backing, 0, std::shared_ptr<const LslList>(list));
        weak_class = value;
        weak_array = backing;
        weak_list = list;
        collect_lsl_fixed_arrays();
        check(!weak_class.expired() && !weak_array.expired() && !weak_list.expired(),
              "Collector removed externally rooted mixed graph");
    }
    collect_lsl_fixed_arrays();
    check(weak_class.expired() && weak_array.expired() && weak_list.expired(),
          "Unreachable mixed class/array/List cycle leaked");
}
} // namespace

int main() {
    try {
        storage();
        rectangular_dimensions();
        heap_checks();
        mixed_graph();
        runtime_object_reference();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
