// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/yengine_reference_projection.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace opensim::script;

namespace {
using Kind = YEngineManagedValue::Kind;
using Class = std::shared_ptr<const LslScriptClass>;
using Array = std::shared_ptr<const LslFixedArray>;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

std::shared_ptr<YEngineManagedValue> node(Kind kind) {
    auto result = std::make_shared<YEngineManagedValue>();
    result->kind = kind;
    return result;
}

YEngineManagedRef integer(std::int32_t value) {
    auto result = node(Kind::Integer);
    result->value = value;
    return result;
}

YEngineManagedRef array(std::string type, YEngineManagedArray values) {
    auto result = node(Kind::Array);
    result->element_type = std::move(type);
    result->value = std::move(values);
    return result;
}

std::shared_ptr<YEngineManagedValue> wrapper(YEngineManagedRef backing, std::int32_t length = 3) {
    auto result = node(Kind::ScriptClass);
    YEngineManagedScriptClass instance;
    instance.class_index = 7;
    for (auto &lane : instance.instance_arrays)
        lane = node(Kind::Null);
    instance.instance_arrays[3] = array("in", {integer(length)});
    instance.instance_arrays[5] = array("ob", {std::move(backing)});
    result->value = std::move(instance);
    return result;
}

YEngineScriptClassProjectionContext context() {
    YEngineScriptClassProjectionContext result;
    result.classes.emplace(7, make_lsl_script_array_descriptor("native-source", "integer", 1));
    return result;
}

void aliases_and_sizes() {
    auto backing = array("in", {integer(11), integer(0), integer(27)});
    auto raw = wrapper(backing);
    YEngineReferenceProjection no_context;
    check(!no_context.project(raw, YEngineReferenceType::Object), "Context-free class projection accepted");
    auto ctx = context();
    std::int32_t observed = -1;
    ctx.check_heap_total = [&](std::int32_t total) {
        observed = total;
        return true;
    };
    YEngineReferenceProjection projection(ctx);
    const auto result = projection.project(raw, YEngineReferenceType::ScriptClass);
    check(result.has_value(), "Validated generated class rejected");
    const auto instance = std::get<Class>(*result);
    check(instance->arrays_heap_usage == 16 && observed == 16 && !instance->identity_hash,
          "Restored own total/hash differs");
    check(std::get<Class>(*projection.project(raw, YEngineReferenceType::Object)) == instance,
          "Class alias lost across declared/Object projection");
    check(std::get<Array>(*projection.project(backing, YEngineReferenceType::Object)) ==
              std::get<Array>(instance->instance_arrays[5][0]),
          "Backing alias lost across roots");
    const std::array index{std::int32_t{2}};
    check(std::get<std::int32_t>(lsl_script_array_get(instance, index)) == 27, "Backing cells differ");
}

void empty_and_wrong_backing() {
    auto raw_empty = array("in", {});
    auto raw = wrapper(raw_empty, 0);
    YEngineReferenceProjection projection(context());
    auto result = projection.project(raw, YEngineReferenceType::ScriptClass);
    check(result && std::get<Class>(*result)->arrays_heap_usage == 4 &&
              std::holds_alternative<LslNullObject>(std::get<Class>(*result)->instance_arrays[5][0]),
          "Empty backing erased wrapper or failed null normalization");
    check(std::holds_alternative<LslNullObject>(*projection.project(raw_empty, YEngineReferenceType::Object)),
          "Empty backing DUPREF normalization differs");
    auto text = node(Kind::String);
    text->value = std::string{"wrong"};
    YEngineReferenceProjection wrong(context());
    auto value = wrong.project(wrapper(text), YEngineReferenceType::ScriptClass);
    check(value && std::get<Class>(*value)->arrays_heap_usage == 14,
          "Public wrong-type Object backing rejected or charged incorrectly");
    check(lsl_string_code_units(std::get<Class>(*value)->instance_arrays[5][0]) == u"wrong",
          "Wrong-type backing string changed");
    YEngineReferenceProjection null(context());
    check(std::holds_alternative<LslNullObject>(*null.project(node(Kind::Null), YEngineReferenceType::ScriptClass)),
          "Null class reference rejected");
}

void known_empty_arrays() {
    constexpr std::array<std::string_view, 13> types{"bo", "ch", "de", "do", "fl", "in", "li",
                                                     "ob", "ro", "sc", "st", "ve", "xa"};
    for (const auto type : types)
        for (const auto suffix : {"", "[]", "[][]"}) {
            const auto raw = array(std::string(type) + suffix, {});
            auto class_node = wrapper(raw, 0);
            YEngineReferenceProjection projection(context());
            const auto value = projection.project(class_node, YEngineReferenceType::ScriptClass);
            check(value && std::get<Class>(*value)->arrays_heap_usage == 4 &&
                      std::holds_alternative<LslNullObject>(std::get<Class>(*value)->instance_arrays[5][0]),
                  "Known empty CLR backing did not normalize null");
            for (unsigned alias = 0; alias < 2; ++alias) {
                const auto projected = projection.project(raw, YEngineReferenceType::Object);
                check(projected && std::holds_alternative<LslNullObject>(*projected),
                      "Known empty array alias did not remain null");
            }
        }
    for (const auto type : {"unknown", "unknown[]", "", "[]", "integer"}) {
        YEngineReferenceProjection projection(context());
        check(!projection.project(array(type, {}), YEngineReferenceType::Object),
              "Unknown empty CLR element type accepted");
    }
    // This change only widens empty normalization, not nonempty carriers.
    YEngineReferenceProjection nonempty(context());
    auto text = node(Kind::String);
    text->value = std::string{"value"};
    check(!nonempty.project(array("st", {text}), YEngineReferenceType::Object),
          "Unsupported nonempty String[] carrier unexpectedly enabled");
}

void cycles() {
    auto raw = wrapper(node(Kind::Null));
    auto back = node(Kind::Array);
    back->element_type = "ob";
    back->value = YEngineManagedArray{raw};
    std::get<YEngineManagedScriptClass>(raw->value).instance_arrays[5] = array("ob", {back});
    YEngineReferenceProjection projection(context());
    auto result = projection.project(raw, YEngineReferenceType::ScriptClass);
    check(result.has_value(), "Class/Object array cycle rejected");
    const auto instance = std::get<Class>(*result);
    const auto backing = std::get<Array>(instance->instance_arrays[5][0]);
    check(std::get<Class>(lsl_fixed_array_get(backing, 0)) == instance && instance->arrays_heap_usage == 4,
          "Class cycle alias or class-zero Object size differs");
    // Break the hand-built raw cycle; production raw nodes are registry-owned.
    back->value = YEngineManagedArray{};
}

void malformed_and_budget() {
    for (unsigned shape = 0; shape < 6; ++shape) {
        auto raw = wrapper(array("in", {integer(0)}), 1);
        auto &instance = std::get<YEngineManagedScriptClass>(raw->value);
        if (shape == 0)
            instance.class_index = 8;
        if (shape == 1)
            instance.instance_arrays[3] = array("ob", {integer(1)});
        if (shape == 2)
            instance.instance_arrays[3] = array("in", {});
        if (shape == 3)
            instance.instance_arrays[5] = array("ob", {});
        if (shape == 4)
            instance.instance_arrays[0] = array("in", {});
        if (shape == 5)
            instance.instance_arrays.pop_back();
        YEngineReferenceProjection projection(context());
        check(!projection.project(raw, YEngineReferenceType::ScriptClass), "Malformed class shape accepted");
    }
    auto ctx = context();
    ctx.check_heap_total = [](std::int32_t total) { return total < 16; };
    auto raw = wrapper(array("in", {integer(0), integer(0), integer(0)}));
    YEngineReferenceProjection limited(ctx);
    check(!limited.project(raw, YEngineReferenceType::ScriptClass), "Class total heap rejection ignored");
    check(!limited.project(raw, YEngineReferenceType::Object), "Failed projection exposed preregistered partial class");
    auto bad = std::make_shared<LslScriptClassDescriptor>(*context().classes.at(7));
    bad->array_rank = 2;
    ctx = context();
    ctx.classes[7] = bad;
    YEngineReferenceProjection wrong(ctx);
    check(!wrong.project(raw, YEngineReferenceType::ScriptClass), "Unsupported descriptor shape accepted");
}
} // namespace

int main() {
    try {
        aliases_and_sizes();
        empty_and_wrong_backing();
        cycles();
        known_empty_arrays();
        malformed_and_budget();
        collect_lsl_fixed_arrays();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
