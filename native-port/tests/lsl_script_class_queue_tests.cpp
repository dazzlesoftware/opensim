// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_xml_values.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

using namespace opensim::script;

namespace {
using Class = std::shared_ptr<const LslScriptClass>;
using List = std::shared_ptr<const LslList>;
constexpr auto class_name = "OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj";

void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

void queued_class(bool nested) {
    // Original compiler-created integer[] oracle: direct and List argument
    // aliases both write XML successfully and both fail Convert.ChangeType on
    // restore. This is a backend queue boundary, not typed event acceptance.
    const std::array dimensions{std::int32_t{3}};
    auto value = std::get<Class>(
        make_lsl_script_array(make_lsl_script_array_descriptor("queue-source", "integer", 1), dimensions));
    const std::array index{std::int32_t{0}};
    lsl_script_array_set(value, index, std::int32_t{11});
    std::vector<LslValue> arguments{value, value};
    List list;
    if (nested) {
        auto mutable_list = std::make_shared<LslList>();
        mutable_list->items = arguments;
        list = mutable_list;
        arguments = {list};
    }
    const auto live = reference_yengine_xml_event_arguments(arguments);
    if (nested) {
        check(std::get<List>(live[0]) == list, "Live queued List identity changed");
        check(std::get<Class>(list->items[0]) == value && std::get<Class>(list->items[1]) == value,
              "Live queued class aliases split");
    } else
        check(std::get<Class>(live[0]) == value && std::get<Class>(live[1]) == value,
              "Live queued class references boxed or copied");
    // Normalization performs XML text validation but defers reload failure.
    const auto normalized = normalize_yengine_xml_event_arguments(arguments);
    const auto &cells = nested ? std::get<List>(normalized[0])->items : normalized;
    check(std::get<Class>(cells[0]) == value && std::get<Class>(cells[1]) == value,
          "XML write lost deferred class marker");
    LslCheckpoint checkpoint;
    checkpoint.program_hash = "queue-source";
    checkpoint.state = "default";
    checkpoint.globals = {value};
    checkpoint.queued_events = {{"touch_start", arguments, {}}};
    const auto internal_wire = encode_lsl_checkpoint(checkpoint);
    auto restored = decode_lsl_checkpoint(internal_wire);
    check(bool(restored), "Internal queued class graph rejected");
    const auto &restored_arguments = restored->queued_events[0].arguments;
    const auto &restored_cells = nested ? std::get<List>(restored_arguments[0])->items : restored_arguments;
    auto global = std::get<Class>(restored->globals[0]);
    check(std::get<Class>(restored_cells[0]) == global && std::get<Class>(restored_cells[1]) == global,
          "Internal queued/global class aliases lost");
    const auto persistent_wire = encode_persistent_lsl_checkpoint(checkpoint);
    check(!decode_lsl_checkpoint(persistent_wire), "Queued XML class restored despite original InvalidCast");
    check(std::get<std::int32_t>(lsl_script_array_get(value, index)) == 11,
          "Persistent queue writer mutated live class");
}

void scalar_restore() {
    bool rejected = false;
    try {
        (void)parse_yengine_xml_scalar(class_name, u"OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj");
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()) == "InvalidCastException: Invalid cast from 'System.String' to "
                                                "'OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj'.";
    }
    check(rejected, "Queued class XML restore exception differs from original");
    check(std::holds_alternative<LslNullObject>(parse_yengine_xml_scalar("Unknown.Script.Type", u"text")),
          "Unknown XML type resolution changed");
}
} // namespace

int main() {
    try {
        queued_class(false);
        queued_class(true);
        scalar_restore();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
