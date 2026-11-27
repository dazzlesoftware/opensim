// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/script/lsl_reference_string.hpp"
#include "opensim/script/lsl_fixed_array.hpp"
#include "opensim/script/lsl_script_class.hpp"
#include "opensim/script/yengine_xml_values.hpp"
#include "opensim/framework/invariant_number_format.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace opensim::script {
namespace {
constexpr std::array<std::uint8_t, 4> signature{'L', 'S', 'C', 1};
constexpr std::array<std::uint8_t, 4> listener_signature{'L', 'S', 'L', 1};
constexpr std::uint8_t listener_version = 2;
constexpr std::uint8_t permissions_version = 3;
constexpr std::uint8_t start_parameter_version = 4;
constexpr std::uint8_t sensor_version = 5;
constexpr std::uint8_t regex_listener_version = 6;
constexpr std::uint8_t list_reference_version = 7;
constexpr std::uint8_t call_frame_version = 8;
constexpr std::uint8_t event_wait_version = 9;
constexpr std::uint8_t event_context_version = 10;
constexpr std::uint8_t boxed_value_version = 11;
constexpr std::uint8_t global_initialization_version = 12;
constexpr std::uint8_t direct_handler_version = 13;
constexpr std::uint8_t character_version = 14;
constexpr std::uint8_t utf16_version = 15;
constexpr std::uint8_t nullable_string_version = 16;
constexpr std::uint8_t nullable_list_version = 17;
constexpr std::uint8_t raw_event_value_version = 18;
constexpr std::uint8_t object_reference_version = 19;
constexpr std::uint8_t fixed_array_version = 20;
constexpr std::uint8_t script_class_version = 21;
constexpr std::uint8_t class_type_name_version = 22;
static_assert(std::variant_size_v<LslValue> == 26, "Update explicit checkpoint value tags for new LSL value kinds");
constexpr std::uint8_t regex_listener_blob_version = 2;

bool has_regex_listeners(const std::vector<LslListenerCheckpoint> &listeners) {
    return std::ranges::any_of(listeners, [](const auto &listener) { return listener.regex_flags != 0; });
}

// Individual string/array lengths use UInt32 fields; total checkpoints have no byte envelope.
constexpr std::size_t maximum_field_length = std::numeric_limits<std::uint32_t>::max();
// Collection counts and reference identifiers use the same UInt32 wire fields.
constexpr std::size_t maximum_entries = maximum_field_length;
constexpr unsigned maximum_depth = 64;

std::size_t minimum_array_cell_bytes(LslFixedArrayKind kind) {
    switch (kind) {
    case LslFixedArrayKind::Character:
        return 2;
    case LslFixedArrayKind::Float:
        return 8;
    case LslFixedArrayKind::Integer:
        return 4;
    case LslFixedArrayKind::Object:
        return 1; // A null Object cell still has a value tag.
    }
    throw std::invalid_argument("Invalid fixed array kind");
}

void validate_class_descriptor(const LslScriptClassDescriptor &descriptor) {
    if (descriptor.source_hash.empty() || descriptor.canonical_name.empty() || descriptor.class_index < -1)
        throw std::invalid_argument("Invalid script class descriptor");
    if (descriptor.array_rank) {
        if (descriptor.element_type.empty() || descriptor.instance_sizes[3] != descriptor.array_rank ||
            descriptor.instance_sizes[5] != 1)
            throw std::invalid_argument("Invalid generated array class descriptor");
        for (std::size_t lane = 0; lane < 11; ++lane)
            if (lane != 3 && lane != 5 && descriptor.instance_sizes[lane])
                throw std::invalid_argument("Invalid generated array instance lane");
        // Match the compiler's bracket insertion for jagged element types.
        const auto suffix = descriptor.element_type.find('[');
        const auto insertion = suffix == std::string::npos ? descriptor.element_type.size() : suffix;
        // Validate length before constructing a rank-sized canonical string.
        if (descriptor.canonical_name.size() !=
            descriptor.element_type.size() + static_cast<std::uint64_t>(descriptor.array_rank) + 1)
            throw std::invalid_argument("Noncanonical generated array class name");
        auto expected = descriptor.element_type;
        expected.insert(insertion, "[" + std::string(descriptor.array_rank - 1, ',') + "]");
        if (descriptor.canonical_name != expected)
            throw std::invalid_argument("Noncanonical generated array class name");
    } else if (!descriptor.element_type.empty())
        throw std::invalid_argument("Ordinary class descriptor has an array element type");
}

void validate_class_fields(const LslScriptClass &instance) {
    for (std::size_t lane = 0; lane < 11; ++lane) {
        if (instance.instance_arrays[lane].size() != instance.descriptor->instance_sizes[lane])
            throw std::invalid_argument("Script class instance size differs from descriptor");
        // Generated dimensions are real Int32 fields, not Object boxes. Public
        // Object storage deliberately accepts arbitrary values, including cycles.
        if (instance.descriptor->array_rank && lane == 3)
            for (const auto &value : instance.instance_arrays[lane])
                if (!std::holds_alternative<std::int32_t>(value))
                    throw std::invalid_argument("Invalid generated array dimension field");
    }
}

double detector_xml_number(double value) {
    return framework::invariant_convert_to_double(framework::format_invariant_double_utf16(value, u"0.000000"));
}

framework::Vector3d detector_xml_vector(const framework::Vector3d &value) {
    // SDK ToString uses the invariant Infinity symbol, which its en-US
    // string constructor rejects. A failed component resets the whole tuple.
    if (std::isinf(value.x) || std::isinf(value.y) || std::isinf(value.z))
        return {};
    return {detector_xml_number(value.x), detector_xml_number(value.y), detector_xml_number(value.z)};
}

LslRotation detector_xml_rotation(const LslRotation &value) {
    if (std::isinf(value.x) || std::isinf(value.y) || std::isinf(value.z) || std::isinf(value.w))
        return {0, 0, 0, 1};
    // Unlike the binary SDK Quaternion constructor, the string constructor
    // does not normalize a successfully parsed all-zero rotation.
    return {detector_xml_number(value.x), detector_xml_number(value.y), detector_xml_number(value.z),
            detector_xml_number(value.w)};
}

std::vector<LslDetector> xml_detectors(const std::vector<LslDetector> &values) {
    auto result = values;
    for (auto &detector : result) {
        detector.position = detector_xml_vector(detector.position);
        detector.velocity = detector_xml_vector(detector.velocity);
        detector.grab = detector_xml_vector(detector.grab);
        detector.rotation = detector_xml_rotation(detector.rotation);
        // DetectArray/EventQueue XML omits the surface fields. Restore creates
        // fresh DetectParams instances before filling the serialized fields.
        detector.touch_face = -1;
        detector.touch_pos = detector.touch_normal = detector.touch_binormal = {};
        detector.touch_st = detector.touch_uv = {-1, -1, 0};
    }
    return result;
}

std::vector<LslDetector> binary_detectors(const std::vector<LslDetector> &values) {
    auto result = values;
    const auto surface_vector = [](const framework::Vector3d &value) {
        return framework::Vector3d{static_cast<float>(value.x), static_cast<float>(value.y),
                                   static_cast<float>(value.z)};
    };
    for (auto &detector : result) {
        // xmrEventDequeue stores selected detectors in a handwritten frame as
        // SDK values. ObjArrToDetPrms narrows only the five surface vectors.
        if (auto &rotation = detector.rotation;
            rotation.x == 0. && rotation.y == 0. && rotation.z == 0. && rotation.w == 0.)
            rotation.w = 1.;
        detector.touch_pos = surface_vector(detector.touch_pos);
        detector.touch_normal = surface_vector(detector.touch_normal);
        detector.touch_binormal = surface_vector(detector.touch_binormal);
        detector.touch_st = surface_vector(detector.touch_st);
        detector.touch_uv = surface_vector(detector.touch_uv);
        if (detector.sdk_name) {
            detector.name = framework::utf8(*detector.sdk_name);
            detector.sdk_name.reset();
        }
    }
    return result;
}

LslCheckpoint external_detector_state(const LslCheckpoint &checkpoint) {
    auto result = checkpoint;
    for (auto &event : result.queued_events)
        event.detectors = xml_detectors(event.detectors);
    for (auto &frame : result.resume_frames)
        if (frame.background_event)
            frame.background_event->detectors = binary_detectors(frame.background_event->detectors);

    // The VM's persistent_checkpoint projects source capture frames first.
    // Their active event is the actual XML array; resume background events
    // above are the selected arrays stored in handwritten binary frames.
    // Raw instruction-budget contexts need the program's compiler metadata
    // for that projection. Leave legacy/unprojected caches intact rather than
    // guessing their roles from the nearest event name or context kind.
    if (result.event_contexts.empty() && result.active_event)
        result.active_event->detectors = xml_detectors(result.active_event->detectors);
    return result;
}

// Select the schema before writing service/context extensions. A boxed value
// may occur only in a suspended caller's journal, not in the leading globals.
// Walk iteratively: list depth and size remain the writer's existing contract.
template <class Predicate> bool contains_values(const LslCheckpoint &checkpoint, Predicate matches) {
    std::vector<const LslValue *> pending;
    std::set<const LslList *> visited;
    std::set<const LslObjectBox *> visited_boxes;
    std::set<const LslFixedArray *> visited_arrays;
    std::set<const LslScriptClass *> visited_classes;
    const auto values = [&](const auto &items) {
        for (const auto &item : items)
            pending.push_back(&item);
    };
    const auto evaluations = [&](const auto &items) {
        for (const auto &item : items)
            pending.push_back(&item.value);
    };
    const auto capture_point = [&](const auto &point) {
        if (point)
            evaluations(point->evaluations);
    };
    const auto frames = [&](const auto &items) {
        for (const auto &item : items) {
            values(item.locals);
            evaluations(item.evaluations);
            capture_point(item.capture_point);
        }
    };
    values(checkpoint.globals);
    values(checkpoint.locals);
    for (const auto &event : checkpoint.queued_events)
        values(event.arguments);
    if (checkpoint.active_event)
        values(checkpoint.active_event->arguments);
    frames(checkpoint.call_stack);
    evaluations(checkpoint.evaluations);
    capture_point(checkpoint.capture_point);
    for (const auto &context : checkpoint.event_contexts) {
        values(context.active_event.arguments);
        values(context.locals);
        frames(context.call_stack);
        evaluations(context.evaluations);
        capture_point(context.capture_point);
        if (context.invoked_event)
            values(context.invoked_event->arguments);
    }
    for (const auto &frame : checkpoint.resume_frames) {
        values(frame.locals);
        evaluations(frame.evaluations);
        if (frame.background_event)
            values(frame.background_event->arguments);
    }
    while (!pending.empty()) {
        const auto &value = *pending.back();
        pending.pop_back();
        if (matches(value))
            return true;
        if (const auto instance = std::get_if<std::shared_ptr<const LslScriptClass>>(&value);
            instance && *instance && visited_classes.insert(instance->get()).second)
            for (const auto &lane : (*instance)->instance_arrays)
                values(lane);
        if (const auto array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value);
            array && *array && visited_arrays.insert(array->get()).second)
            if (const auto cells = std::get_if<std::vector<LslValue>>(&(*array)->cells))
                values(*cells);
        if (const auto box = std::get_if<std::shared_ptr<const LslObjectBox>>(&value);
            box && *box && visited_boxes.insert(box->get()).second)
            pending.push_back(&(*box)->value);
        if (const auto list = std::get_if<std::shared_ptr<const LslList>>(&value);
            list && *list && visited.insert(list->get()).second) {
            if ((*list)->items.size() > maximum_entries)
                throw std::length_error("LSL checkpoint collection exceeds the transfer limit");
            values((*list)->items);
        }
    }
    return false;
}

bool has_object_references(const LslCheckpoint &checkpoint) {
    return contains_values(checkpoint, [](const LslValue &value) {
        return std::holds_alternative<std::shared_ptr<const LslStringReference>>(value) ||
               std::holds_alternative<std::shared_ptr<const LslObjectBox>>(value);
    });
}

template <class T> bool has_local_ledger(const T &value) {
    return value.local_heap_base.has_value() || !value.local_object_heap_usage.empty();
}

bool has_heap_ledger(const LslCheckpoint &c) {
    if (c.arrays_heap_usage || c.locals_heap_usage || has_local_ledger(c) || c.heap_restore_pending)
        return true;
    const auto point = [](const auto &p) { return p && has_local_ledger(*p); };
    const auto frame = [&](const auto &f) { return has_local_ledger(f) || point(f.capture_point); };
    if (point(c.capture_point) || std::any_of(c.call_stack.begin(), c.call_stack.end(), frame))
        return true;
    for (const auto &e : c.event_contexts)
        if (has_local_ledger(e) || point(e.capture_point) ||
            std::any_of(e.call_stack.begin(), e.call_stack.end(), frame))
            return true;
    for (const auto &r : c.resume_frames)
        if (has_local_ledger(r))
            return true;
    return false;
}

bool has_list_identity_hash(const LslCheckpoint &checkpoint) {
    return contains_values(checkpoint, [](const LslValue &value) {
        const auto list = std::get_if<std::shared_ptr<const LslList>>(&value);
        return list && *list && (*list)->identity_hash.has_value();
    });
}

bool has_boxed_values(const LslCheckpoint &checkpoint) {
    return contains_values(checkpoint, [](const LslValue &value) {
        return std::holds_alternative<LslBoxedInteger>(value) || std::holds_alternative<LslBoxedFloat>(value) ||
               std::holds_alternative<LslBoxedString>(value) || std::holds_alternative<LslBoxedKey>(value) ||
               std::holds_alternative<LslBoxedUtf16String>(value) || std::holds_alternative<LslBoxedUtf16Key>(value);
    });
}

bool has_character_values(const LslCheckpoint &checkpoint) {
    return contains_values(checkpoint,
                           [](const LslValue &value) { return std::holds_alternative<LslCharacter>(value); });
}

bool has_nullable_lists(const LslCheckpoint &checkpoint) {
    return contains_values(checkpoint, [](const LslValue &value) {
        return std::holds_alternative<LslNullList>(value) || std::holds_alternative<LslNullObject>(value);
    });
}

bool has_nullable_strings(const LslCheckpoint &checkpoint) {
    return contains_values(checkpoint, [](const LslValue &value) {
        return std::holds_alternative<LslNullString>(value) || std::holds_alternative<LslBoxedNullString>(value) ||
               std::holds_alternative<LslBoxedNullKey>(value);
    });
}

bool has_utf16_values(const LslCheckpoint &checkpoint) {
    return contains_values(checkpoint, [](const LslValue &value) {
        return std::holds_alternative<LslUtf16String>(value) || std::holds_alternative<LslBoxedUtf16String>(value) ||
               std::holds_alternative<LslBoxedUtf16Key>(value);
    });
}

bool has_raw_event_values(const LslCheckpoint &checkpoint) {
    return contains_values(checkpoint, [](const LslValue &value) {
        return std::holds_alternative<LslRawSingle>(value) || std::holds_alternative<LslRawBoolean>(value) ||
               std::holds_alternative<LslRawUuid>(value);
    });
}

bool has_utf16_detector_names(const LslCheckpoint &checkpoint) {
    const auto event = [](const LslEvent &value) {
        return std::ranges::any_of(value.detectors,
                                   [](const LslDetector &detector) { return detector.sdk_name.has_value(); });
    };
    if (checkpoint.active_event && event(*checkpoint.active_event))
        return true;
    for (const auto &value : checkpoint.queued_events)
        if (event(value))
            return true;
    for (const auto &context : checkpoint.event_contexts)
        if (event(context.active_event) || (context.invoked_event && event(*context.invoked_event)))
            return true;
    for (const auto &frame : checkpoint.resume_frames)
        if (frame.background_event && event(*frame.background_event))
            return true;
    return false;
}

template <class Archive, class Checkpoint> void checkpoint_fields(Archive &archive, Checkpoint &value) {
    archive.fields(value.program_hash, value.state, value.next_state, value.globals, value.locals, value.queued_events,
                   value.active_event, value.initialized_globals, value.instruction, value.sleep_remaining,
                   value.timer_interval, value.timer_remaining, value.timer_elapsed, value.minimum_event_delay,
                   value.event_delay_remaining, value.running, value.sleep_requested);
}

template <class Archive, class Event> void event_fields(Archive &archive, Event &value) {
    archive.fields(value.name, value.arguments, value.detectors);
}

template <class Archive, class Evaluation> void evaluation_fields(Archive &archive, Evaluation &value) {
    archive.fields(value.expression, value.value);
}

template <class Archive, class Frame> void call_frame_fields(Archive &archive, Frame &value) {
    archive.fields(value.function, value.instruction, value.return_expression, value.locals, value.evaluations);
}

template <class Archive, class Point> void capture_point_fields(Archive &archive, Point &value) {
    archive.fields(value.instruction, value.evaluations, value.check_stack);
}

template <class Archive, class Frame> void resume_frame_fields(Archive &archive, Frame &value) {
    archive.fields(value.method, value.instruction, value.locals, value.evaluations, value.complete_restore,
                   value.check_stack, value.event_wait, value.background_event, value.global_initializer_cursor);
}

template <class Archive, class Wait> void event_wait_fields(Archive &archive, Wait &value) {
    archive.fields(value.expression, value.deadline_utc_ticks, value.return_mask1, value.return_mask2);
}

bool valid_wait(const LslEventWaitCheckpoint &wait, const std::vector<LslEvaluationCheckpoint> &evaluations) {
    return wait.deadline_utc_ticks >= 0 && wait.deadline_utc_ticks <= 3155378975999999999LL &&
           !(std::bit_cast<std::uint32_t>(wait.background_mask1) & std::bit_cast<std::uint32_t>(wait.return_mask1)) &&
           !(std::bit_cast<std::uint32_t>(wait.background_mask2) & std::bit_cast<std::uint32_t>(wait.return_mask2)) &&
           std::ranges::none_of(evaluations,
                                [&](const auto &evaluation) { return evaluation.expression == wait.expression; });
}

bool has_background_wait(const LslEventWaitCheckpoint &wait) {
    return wait.background_mask1 || wait.background_mask2;
}

bool needs_event_context_version(const LslCheckpoint &checkpoint) {
    return !checkpoint.event_contexts.empty() || (checkpoint.event_wait && has_background_wait(*checkpoint.event_wait));
}

bool valid_event_wait(const LslCheckpoint &checkpoint) {
    if (checkpoint.event_wait && !valid_wait(*checkpoint.event_wait, checkpoint.evaluations))
        return false;
    if (!checkpoint.event_contexts.empty() && (!checkpoint.active_event || checkpoint.active_event->name.empty()))
        return false;
    for (const auto &context : checkpoint.event_contexts) {
        if (context.active_event.name.empty())
            return false;
        if (context.kind == LslEventContextKind::DequeueBackground) {
            if (!context.event_wait || !has_background_wait(*context.event_wait) ||
                !valid_wait(*context.event_wait, context.evaluations) || context.direct_call_expression)
                return false;
        } else if (context.kind == LslEventContextKind::DirectHandler) {
            if (context.event_wait || !context.direct_call_expression)
                return false;
        } else
            return false;
    }
    return true;
}

bool needs_direct_handler_version(const LslCheckpoint &checkpoint) {
    const auto captured_frames = [](const auto &frames) {
        return std::ranges::any_of(frames, [](const auto &frame) { return frame.capture_point.has_value(); });
    };
    return checkpoint.pending_resume_fault.has_value() || checkpoint.capture_point ||
           !checkpoint.resume_frames.empty() || checkpoint.resume_entry_pending ||
           checkpoint.execution_slice_elapsed_ticks.has_value() || captured_frames(checkpoint.call_stack) ||
           std::ranges::any_of(checkpoint.event_contexts, [&](const auto &context) {
               return context.kind != LslEventContextKind::DequeueBackground || context.direct_call_expression ||
                      context.capture_point || context.invoked_event || captured_frames(context.call_stack);
           });
}

bool valid_direct_handler_state(const LslCheckpoint &checkpoint) {
    if (needs_direct_handler_version(checkpoint) && !checkpoint.global_initialization_pending.has_value())
        return false;
    if ((checkpoint.pending_resume_fault || checkpoint.capture_point || !checkpoint.resume_frames.empty() ||
         checkpoint.resume_entry_pending || checkpoint.execution_slice_elapsed_ticks) &&
        (!checkpoint.active_event || checkpoint.active_event->name.empty()))
        return false;
    if (checkpoint.pending_resume_fault && checkpoint.pending_resume_fault->empty())
        return false;
    if (checkpoint.execution_slice_elapsed_ticks && *checkpoint.execution_slice_elapsed_ticks < 0)
        return false;
    for (const auto &context : checkpoint.event_contexts)
        if (context.invoked_event && context.invoked_event->name.empty())
            return false;
    for (const auto &frame : checkpoint.resume_frames) {
        if (frame.method.empty() ||
            (frame.global_initializer_cursor && *frame.global_initializer_cursor >= checkpoint.globals.size()))
            return false;
        if (frame.method == "xmrEventDequeue") {
            if (!frame.event_wait || !valid_wait(*frame.event_wait, frame.evaluations) ||
                frame.background_event.has_value() == frame.complete_restore ||
                (frame.background_event &&
                 (frame.background_event->name.empty() || !has_background_wait(*frame.event_wait))))
                return false;
        } else if (frame.event_wait || frame.background_event)
            return false;
    }
    return true;
}

bool valid_global_initialization(const LslCheckpoint &checkpoint) {
    const auto valid_cursor = [&](const auto &cursor, const LslEvent *event) {
        return !cursor ||
               (checkpoint.global_initialization_pending.has_value() && event && checkpoint.state == "default" &&
                event->name == "state_entry" && *cursor < checkpoint.globals.size());
    };
    if (!valid_cursor(checkpoint.global_initializer_cursor,
                      checkpoint.active_event ? &*checkpoint.active_event : nullptr))
        return false;
    for (const auto &context : checkpoint.event_contexts)
        if (!valid_cursor(context.global_initializer_cursor, &context.active_event))
            return false;
    return true;
}

template <class Archive, class Context> void event_context_fields(Archive &archive, Context &value) {
    archive.fields(value.active_event, value.function, value.instruction, value.locals, value.call_stack,
                   value.evaluations);
}

template <class Archive, class Listener> void listener_fields(Archive &archive, Listener &value) {
    archive.fields(value.sender, value.handle, value.channel, value.name, value.message, value.active);
}

template <class Archive, class Detector> void detector_fields(Archive &archive, Detector &value) {
    archive.fields(value.key, value.touch_face, value.touch_pos, value.touch_normal, value.touch_binormal,
                   value.touch_st, value.touch_uv, value.name, value.owner, value.group, value.position, value.velocity,
                   value.grab, value.rotation, value.type, value.link_number);
}

bool has_unpaired_utf16(std::u16string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto unit = text[i];
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff)
                return true;
        } else if (unit >= 0xdc00 && unit <= 0xdfff)
            return true;
    }
    return false;
}

class Writer {
    std::vector<std::uint8_t> output_{signature.begin(), signature.end()};
    unsigned depth_ = 0;
    bool include_listener_regex_ = false;
    bool include_wait_background_ = false;
    bool include_global_initialization_ = false;
    bool include_direct_handler_ = false;
    std::unordered_map<const LslList *, std::size_t> lists_;
    std::unordered_map<const LslStringReference *, std::size_t> string_references_;
    std::map<std::u16string, std::size_t> persistent_strings_;
    std::unordered_map<const LslObjectBox *, std::size_t> object_boxes_;
    std::unordered_map<const LslFixedArray *, std::size_t> arrays_;
    std::unordered_map<const LslScriptClass *, std::size_t> classes_;
    std::size_t written_strings_ = 0, written_boxes_ = 0;
    bool wrote_list_reference_ = false;
    bool persistent_ = false, include_utf16_ = false, include_identity_hash_ = false;
    bool xml_queue_arguments_ = false;
    bool include_arrays_ = false;
    bool include_class_owners_ = false;

    void append(std::span<const std::uint8_t> bytes) {
        if (bytes.size() > output_.max_size() - output_.size())
            throw std::length_error("LSL checkpoint exceeds the transfer limit");
        output_.insert(output_.end(), bytes.begin(), bytes.end());
    }

    void number(std::uint64_t bits, unsigned width) {
        std::array<std::uint8_t, 8> bytes{};
        for (unsigned i = 0; i < width; ++i)
            bytes[i] = static_cast<std::uint8_t>(bits >> (i * 8));
        append(std::span(bytes).first(width));
    }

    void field(bool value) {
        number(value, 1);
    }

    void field(std::int32_t value) {
        number(std::bit_cast<std::uint32_t>(value), 4);
    }

    void field(std::int64_t value) {
        number(std::bit_cast<std::uint64_t>(value), 8);
    }

    void field(std::size_t value) {
        number(value, 8);
    }

    void field(double value) {
        number(std::bit_cast<std::uint64_t>(value), 8);
    }

    void field(const LslRawSingle &value) {
        number(std::bit_cast<std::uint32_t>(value.value), 4);
    }

    void field(const LslRawBoolean &value) {
        field(value.value);
    }

    void field(const LslRawUuid &value) {
        field(value.value);
    }

    void field(const std::string &value) {
        if (value.size() > maximum_field_length)
            throw std::length_error("LSL checkpoint string exceeds the transfer limit");
        number(value.size(), 4);
        append({reinterpret_cast<const std::uint8_t *>(value.data()), value.size()});
    }

    void field(const framework::Uuid &value) {
        append(value.bytes());
    }

    void field(const framework::Vector3d &value) {
        fields(value.x, value.y, value.z);
    }

    void field(const LslRotation &value) {
        fields(value.x, value.y, value.z, value.w);
    }

    void field(const LslBoxedInteger &value) {
        field(value.value);
    }

    void field(const LslBoxedFloat &value) {
        field(value.value);
    }

    void field(const LslBoxedString &value) {
        field(value.value);
    }

    void field(const std::u16string &value) {
        if (value.size() > maximum_field_length)
            throw std::length_error("LSL checkpoint UTF16 string exceeds the transfer limit");
        number(value.size(), 4);
        for (const auto unit : value)
            number(static_cast<std::uint16_t>(unit), 2);
    }

    void field(const LslUtf16String &value) {
        field(value.value);
    }

    void field(const LslBoxedUtf16String &value) {
        field(value.value);
    }

    void field(const LslBoxedUtf16Key &value) {
        field(value.value);
    }

    void field(const LslNullList &) {}

    void field(const LslNullObject &) {}

    void field(const LslNullString &) {}

    void field(const LslBoxedNullString &) {}

    void field(const LslBoxedNullKey &) {}

    void field(const LslCharacter &value) {
        if (persistent_ && value.value >= 0xd800 && value.value <= 0xdfff)
            throw std::runtime_error(
                "ArgumentException: Unicode surrogate characters must be written out as pairs together in the same "
                "call, not individually. Consider passing in a character array instead.");
        number(static_cast<std::uint16_t>(value.value), 2);
    }

    void field(const LslBoxedKey &value) {
        field(value.value);
    }

    void field(const LslEvent &value) {
        event_fields(*this, value);
    }

    void field(const std::vector<LslEvent> &events) {
        if (events.size() > maximum_entries)
            throw std::length_error("LSL checkpoint collection exceeds the transfer limit");
        number(events.size(), 4);
        for (const auto &event : events) {
            if (!persistent_) {
                field(event);
                continue;
            }
            field(event.name);
            // These values already passed the XML text constructors. In
            // particular a queued all-zero Quaternion must remain zero,
            // whereas an active/binary LSLROT receives the SDK W=1 guard.
            xml_queue_arguments_ = true;
            field(event.arguments);
            xml_queue_arguments_ = false;
            field(event.detectors);
        }
    }

    void field(const LslDetector &value) {
        if (persistent_ && value.sdk_name) {
            if (has_unpaired_utf16(*value.sdk_name))
                throw std::runtime_error("ArgumentException: Invalid surrogate pair in external detector XML name.");
            auto normalized = value;
            normalized.name = framework::utf8(*value.sdk_name);
            normalized.sdk_name.reset();
            detector_fields(*this, normalized);
        } else
            detector_fields(*this, value);
        if (include_utf16_)
            field(value.sdk_name);
    }

    void field(const LslListenerCheckpoint &value) {
        listener_fields(*this, value);
        if (include_listener_regex_)
            field(value.regex_flags);
    }

    void field(const LslSensorQuery &value) {
        fields(value.name, value.key, value.type, value.range, value.arc, value.interval);
    }

    void field(const LslEvaluationCheckpoint &value) {
        evaluation_fields(*this, value);
    }

    void field(const LslCallFrameCheckpoint &value) {
        call_frame_fields(*this, value);
        if (include_direct_handler_)
            field(value.capture_point);
        if (include_arrays_)
            heap_fields(value);
    }

    void field(const LslCapturePointCheckpoint &value) {
        capture_point_fields(*this, value);
        if (include_arrays_)
            heap_fields(value);
    }

    void field(const LslResumeFrameCheckpoint &value) {
        resume_frame_fields(*this, value);
        if (include_arrays_)
            heap_fields(value);
    }

    void field(const LslEventWaitCheckpoint &value) {
        event_wait_fields(*this, value);
        if (include_wait_background_)
            fields(value.background_mask1, value.background_mask2);
    }

    void field(const LslEventContextCheckpoint &value) {
        event_context_fields(*this, value);
        if (include_direct_handler_) {
            number(static_cast<std::uint8_t>(value.kind), 1);
            fields(value.event_wait, value.direct_call_expression);
        } else {
            field(*value.event_wait);
        }
        if (include_global_initialization_)
            field(value.global_initializer_cursor);
        if (include_direct_handler_)
            fields(value.capture_point, value.invoked_event);
        if (include_arrays_)
            heap_fields(value);
    }

    template <class T> void heap_fields(const T &value) {
        if (persistent_)
            fields(std::optional<std::int32_t>{}, std::vector<std::optional<std::int32_t>>{});
        else
            fields(value.local_heap_base, value.local_object_heap_usage);
    }

    template <class T> void field(const std::vector<T> &values) {
        if (values.size() > maximum_entries)
            throw std::length_error("LSL checkpoint collection exceeds the transfer limit");
        number(values.size(), 4);
        for (const auto &value : values)
            field(value);
    }

    template <class T> void field(const std::optional<T> &value) {
        field(value.has_value());
        if (value)
            field(*value);
    }

    void field(const LslValue &value) {
        if (persistent_ && !xml_queue_arguments_)
            if (const auto array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value);
                array && *array && !lsl_fixed_array_size(*array)) {
                field(LslValue{LslNullObject{}});
                return;
            }
        if (persistent_ && !xml_queue_arguments_) {
            if (const auto *rotation = std::get_if<LslRotation>(&value);
                rotation && rotation->x == 0. && rotation->y == 0. && rotation->z == 0. && rotation->w == 0.) {
                // Original MigrateIn constructs an SDK Quaternion for each
                // LSLROT value. Its all-zero guard changes only W to +1;
                // signed XYZ zeros survive. Apply those receive semantics to
                // external output without mutating live/internal checkpoints.
                auto normalized = *rotation;
                normalized.w = 1.;
                field(LslValue{normalized});
                return;
            }
            if (const auto *text = std::get_if<LslUtf16String>(&value)) {
                field(LslValue{framework::utf8(text->value)});
                return;
            }
            if (const auto *text = std::get_if<LslBoxedUtf16String>(&value)) {
                field(LslValue{LslBoxedString{framework::utf8(text->value)}});
                return;
            }
            if (const auto *text = std::get_if<LslBoxedUtf16Key>(&value)) {
                field(LslValue{LslBoxedKey{framework::utf8(text->value)}});
                return;
            }
        }
        if (const auto *list = std::get_if<std::shared_ptr<const LslList>>(&value)) {
            if (const auto found = lists_.find(list->get()); found != lists_.end()) {
                number(6, 1);
                number(found->second, 4);
                wrote_list_reference_ = true;
                return;
            }
        }
        // Tags are fixed by the wire version and independent of host ABI sizes.
        // Tag6 is the original list-reference tag, not variant index6.
        number(value.index() < 6 ? value.index() : value.index() + 1, 1);
        std::visit([&](const auto &item) { field(item); }, value);
    }

    void field(const std::shared_ptr<const LslStringReference> &value) {
        if (!value)
            throw std::invalid_argument("Invalid CLR String reference");
        std::optional<std::size_t> previous;
        if (persistent_ && !xml_queue_arguments_) {
            if (auto found = persistent_strings_.find(value->units); found != persistent_strings_.end())
                previous = found->second;
        } else if (!persistent_) {
            if (auto found = string_references_.find(value.get()); found != string_references_.end())
                previous = found->second;
        }
        if (previous) {
            number(1, 1);
            number(*previous, 4);
            return;
        }
        if (written_strings_ == maximum_entries)
            throw std::length_error("LSL checkpoint String references exceed the transfer limit");
        const auto index = written_strings_++;
        number(0, 1);
        const auto cached = persistent_ ? std::optional<std::int32_t>{} : lsl_reference_string_cached_integer(value);
        const auto array_type =
            persistent_ ? std::optional<std::uint8_t>{} : lsl_reference_string_cached_array_type(value);
        const auto class_type =
            persistent_ ? std::optional<LslClassTypeNameOrigin>{} : lsl_reference_string_class_type_name(value);
        number(class_type   ? 4
               : array_type ? 3
               : cached     ? 2
                            : (persistent_ ? value->units.empty() : lsl_reference_string_is_literal(value)),
               1);
        if (class_type)
            fields(class_type->owner, class_type->source_hash, class_type->canonical_name);
        if (array_type)
            number(*array_type, 1);
        if (cached)
            field(*cached);
        // Original dictionary equality runs before BinaryWriter's encoding.
        // Distinct pre-encoding strings that normalize to the same replacement
        // text remain distinct definitions in the external wire.
        field(persistent_ ? framework::utf16_replacing(framework::utf8(value->units)) : value->units);
        if (persistent_ && !xml_queue_arguments_)
            persistent_strings_.emplace(value->units, index);
        else if (!persistent_)
            string_references_.emplace(value.get(), index);
    }

    void field(const std::shared_ptr<const LslObjectBox> &value) {
        if (!value || !valid_lsl_object_box(*value))
            throw std::invalid_argument("Invalid CLR Object box payload or string field");
        if (!persistent_)
            if (auto found = object_boxes_.find(value.get()); found != object_boxes_.end()) {
                number(1, 1);
                number(found->second, 4);
                return;
            }
        if (written_boxes_ == maximum_entries)
            throw std::length_error("LSL checkpoint Object boxes exceed the transfer limit");
        const auto index = written_boxes_++;
        number(0, 1);
        field(value->value);
        field(bool(value->string_field));
        if (value->string_field)
            field(value->string_field);
        // CLR value cases precede the original object-reference dictionary.
        // Persistent output therefore emits a fresh box for every occurrence.
        if (!persistent_)
            object_boxes_.emplace(value.get(), index);
    }

    void field(const std::shared_ptr<const LslList> &value) {
        if (!value || depth_ == maximum_depth)
            throw std::invalid_argument("Invalid nested LSL checkpoint list");
        ++depth_;
        if (include_arrays_)
            lists_.emplace(value.get(), lists_.size());
        field(value->items);
        if (include_identity_hash_) {
            const auto hash = persistent_ ? std::optional<std::uint32_t>{} : value->identity_hash;
            if (hash && (!*hash || *hash > 0x3ffffffu))
                throw std::invalid_argument("Invalid LSL List identity hash");
            field(hash.has_value());
            if (hash)
                number(*hash, 4);
        }
        --depth_;
        if (!include_arrays_)
            lists_.emplace(value.get(), lists_.size());
    }

    void field(const std::shared_ptr<const LslScriptClass> &instance) {
        if (!instance || !instance->descriptor)
            throw std::invalid_argument("Invalid script class carrier");
        if (persistent_ && xml_queue_arguments_) {
            number(2, 1);
            return;
        }
        if (auto it = classes_.find(instance.get()); it != classes_.end()) {
            number(1, 1);
            number(it->second, 4);
            return;
        }
        if (classes_.size() == maximum_entries || depth_ == maximum_depth)
            throw std::length_error("LSL checkpoint class graph exceeds transfer limits");
        const auto &descriptor = *instance->descriptor;
        validate_class_descriptor(descriptor);
        validate_class_fields(*instance);
        classes_.emplace(instance.get(), classes_.size());
        number(0, 1);
        fields(descriptor.source_hash, descriptor.canonical_name, descriptor.element_type);
        if (include_class_owners_) {
            field(persistent_ ? framework::Uuid{} : descriptor.type_name_owner);
            field(descriptor.class_index);
        }
        number(descriptor.array_rank, 4);
        for (std::size_t lane = 0; lane < 11; ++lane) {
            if (instance->instance_arrays[lane].size() != descriptor.instance_sizes[lane])
                throw std::invalid_argument("Script class instance size differs from descriptor");
            number(descriptor.instance_sizes[lane], 4);
        }
        const auto hash = persistent_ ? std::optional<std::uint32_t>{} : instance->identity_hash;
        if (hash && (!*hash || *hash > 0x3ffffffu))
            throw std::invalid_argument("Invalid script class identity hash");
        field(hash.has_value());
        if (hash)
            number(*hash, 4);
        auto heap_usage = instance->arrays_heap_usage;
        if (persistent_) {
            // RecvArrays rebuilds class storage independently of global usage.
            // Class references cost zero, so class cycles do not recurse here.
            std::uint32_t rebuilt = 0;
            constexpr std::array<std::uint32_t, 11> widths{0, 2, 8, 4, 0, 0, 32, 0, 24, 0, 8};
            for (std::size_t lane = 0; lane < 11; ++lane) {
                rebuilt += static_cast<std::uint32_t>(instance->instance_arrays[lane].size()) * widths[lane];
                if (lane == 4 || lane == 5 || lane == 7)
                    for (const auto &value : instance->instance_arrays[lane]) {
                        // RecvArrays uses HeapTrackerString.Size for the typed
                        // string lane; Object fields retain Object.Size semantics.
                        if (lane == 7) {
                            const auto text = lsl_string_code_units(value);
                            if (!text || std::all_of(text->begin(), text->end(), framework::unicode_whitespace))
                                continue;
                        }
                        rebuilt += static_cast<std::uint32_t>(lsl_script_class_object_size(value));
                    }
            }
            heap_usage = std::bit_cast<std::int32_t>(rebuilt);
        }
        field(heap_usage);
        ++depth_;
        for (const auto &lane : instance->instance_arrays)
            for (const auto &value : lane)
                field(value);
        --depth_;
    }

    void field(const std::shared_ptr<const LslFixedArray> &array) {
        if (!array)
            throw std::invalid_argument("Null fixed array carrier");
        if (persistent_ && xml_queue_arguments_) {
            number(2, 1);
            number(static_cast<std::uint8_t>(array->kind), 1);
            return;
        }
        if (auto it = arrays_.find(array.get()); it != arrays_.end()) {
            number(1, 1);
            number(it->second, 4);
            return;
        }
        if (arrays_.size() == maximum_entries || depth_ == maximum_depth)
            throw std::length_error("LSL checkpoint array graph exceeds transfer limits");
        arrays_.emplace(array.get(), arrays_.size());
        number(0, 1);
        number(static_cast<std::uint8_t>(array->kind), 1);
        const auto hash = persistent_ ? std::optional<std::uint32_t>{} : array->identity_hash;
        if (hash && (!*hash || *hash > 0x3ffffffu))
            throw std::invalid_argument("Invalid array identity hash");
        field(hash.has_value());
        if (hash)
            number(*hash, 4);
        const auto length = lsl_fixed_array_size(array);
        // The wire carries a UInt32 cell count. Allocation on read is checked
        // against the bytes actually present, rather than a fixed total envelope.
        if (length > maximum_field_length)
            throw std::length_error("LSL checkpoint array length is not representable");
        number(length, 4);
        ++depth_;
        switch (array->kind) {
        case LslFixedArrayKind::Character:
            for (auto cell : std::get<std::vector<char16_t>>(array->cells)) {
                field(LslCharacter{cell});
            }
            break;
        case LslFixedArrayKind::Float:
            for (auto cell : std::get<std::vector<double>>(array->cells))
                field(cell);
            break;
        case LslFixedArrayKind::Integer:
            for (auto cell : std::get<std::vector<std::int32_t>>(array->cells))
                field(cell);
            break;
        case LslFixedArrayKind::Object:
            for (const auto &cell : std::get<std::vector<LslValue>>(array->cells))
                field(cell);
            break;
        default:
            throw std::invalid_argument("Invalid fixed array kind");
        }
        --depth_;
    }

  public:
    explicit Writer(bool persistent = false) : persistent_(persistent) {}

    template <class... T> void fields(const T &...values) {
        (field(values), ...);
    }

    std::vector<std::uint8_t> encode(const LslCheckpoint &checkpoint, bool include_extensions) {
        if (!valid_event_wait(checkpoint))
            throw std::invalid_argument("Invalid LSL event wait checkpoint");
        if (!valid_global_initialization(checkpoint))
            throw std::invalid_argument("Invalid LSL constructor initialization checkpoint");
        if (!valid_direct_handler_state(checkpoint))
            throw std::invalid_argument("Invalid LSL direct handler checkpoint");
        LslCheckpoint queued_values;
        queued_values.queued_events = checkpoint.queued_events;
        const bool queued_arrays = persistent_ && contains_values(queued_values, [](const LslValue &value) {
                                       return std::holds_alternative<std::shared_ptr<const LslFixedArray>>(value);
                                   });
        if (!persistent_ && checkpoint.class_type_name_owner && checkpoint.class_type_name_owner->is_zero())
            throw std::invalid_argument("Invalid checkpoint class type-name owner");
        const bool has_class_owner_origins =
            !persistent_ &&
            (checkpoint.class_type_name_owner.has_value() || contains_values(checkpoint, [](const LslValue &value) {
                 if (const auto text = std::get_if<std::shared_ptr<const LslStringReference>>(&value))
                     return lsl_reference_string_class_type_name(*text).has_value();
                 if (const auto instance = std::get_if<std::shared_ptr<const LslScriptClass>>(&value))
                     return *instance && (*instance)->descriptor && !(*instance)->descriptor->type_name_owner.is_zero();
                 return false;
             }));
        const bool has_class_indices = contains_values(checkpoint, [](const LslValue &value) {
            const auto instance = std::get_if<std::shared_ptr<const LslScriptClass>>(&value);
            return instance && *instance && (*instance)->descriptor && (*instance)->descriptor->class_index >= 0;
        });
        const bool has_class_names = has_class_owner_origins || has_class_indices;
        include_class_owners_ = has_class_names;
        const bool has_classes = contains_values(checkpoint, [](const LslValue &value) {
            return std::holds_alternative<std::shared_ptr<const LslScriptClass>>(value);
        });
        const bool has_arrays =
            has_class_names || has_classes ||
            contains_values(checkpoint,
                            [&](const LslValue &value) {
                                const auto array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value);
                                const auto text = std::get_if<std::shared_ptr<const LslStringReference>>(&value);
                                return (array && (!persistent_ || !*array || lsl_fixed_array_size(*array))) ||
                                       (!persistent_ && text &&
                                        lsl_reference_string_cached_array_type(*text).has_value());
                            }) ||
            queued_arrays || (!persistent_ && has_heap_ledger(checkpoint));
        include_arrays_ = has_arrays;
        const bool has_references =
            has_arrays || has_object_references(checkpoint) || (!persistent_ && has_list_identity_hash(checkpoint));
        include_identity_hash_ = has_references;
        const bool has_raw_values = has_raw_event_values(checkpoint);
        const bool has_list_nulls = has_nullable_lists(checkpoint) ||
                                    (persistent_ && contains_values(checkpoint, [](const LslValue &value) {
                                         const auto array = std::get_if<std::shared_ptr<const LslFixedArray>>(&value);
                                         return array && *array && !lsl_fixed_array_size(*array);
                                     }));
        const bool has_nulls = has_nullable_strings(checkpoint);
        const bool has_boxes = has_boxed_values(checkpoint);
        const bool has_characters = has_character_values(checkpoint);
        const bool has_utf16 = !persistent_ && (has_utf16_values(checkpoint) || has_utf16_detector_names(checkpoint));
        include_utf16_ = has_references || has_raw_values || has_list_nulls || has_nulls || has_utf16;
        include_global_initialization_ = has_references || has_raw_values || has_list_nulls || has_nulls || has_utf16 ||
                                         has_characters || checkpoint.global_initialization_pending.has_value();
        include_direct_handler_ = has_references || has_raw_values || has_list_nulls || has_nulls || has_utf16 ||
                                  has_characters || needs_direct_handler_version(checkpoint);
        checkpoint_fields(*this, checkpoint);
        const bool has_call_frames =
            !checkpoint.function.empty() || !checkpoint.call_stack.empty() || !checkpoint.evaluations.empty();
        const bool has_permissions = !checkpoint.permissions_granter.is_zero() || checkpoint.permissions_mask != 0;
        const bool has_event_context = needs_event_context_version(checkpoint);
        include_listener_regex_ = include_extensions && has_regex_listeners(checkpoint.listeners);
        if (wrote_list_reference_ || checkpoint.start_parameter != 0 || !checkpoint.sensors.empty() ||
            include_listener_regex_ || has_call_frames || checkpoint.event_wait || has_event_context || has_boxes ||
            include_global_initialization_ || include_direct_handler_) {
            output_[3] = has_class_names                  ? class_type_name_version
                         : has_classes                    ? script_class_version
                         : has_arrays                     ? fixed_array_version
                         : has_references                 ? object_reference_version
                         : has_raw_values                 ? raw_event_value_version
                         : has_list_nulls                 ? nullable_list_version
                         : has_nulls                      ? nullable_string_version
                         : has_utf16                      ? utf16_version
                         : has_characters                 ? character_version
                         : include_direct_handler_        ? direct_handler_version
                         : include_global_initialization_ ? global_initialization_version
                         : has_boxes                      ? boxed_value_version
                         : has_event_context              ? event_context_version
                         : checkpoint.event_wait          ? event_wait_version
                         : has_call_frames                ? call_frame_version
                         : wrote_list_reference_          ? list_reference_version
                         : include_listener_regex_        ? regex_listener_version
                         : checkpoint.sensors.empty()     ? start_parameter_version
                                                          : sensor_version;
            include_listener_regex_ = output_[3] >= regex_listener_version;
            include_wait_background_ = output_[3] >= event_context_version;
            if (include_extensions) {
                fields(checkpoint.listeners, checkpoint.permissions_granter, checkpoint.permissions_mask);
            } else {
                fields(std::vector<LslListenerCheckpoint>{}, framework::Uuid{}, std::int32_t{0});
            }
            field(checkpoint.start_parameter);
            if (output_[3] >= sensor_version)
                field(checkpoint.sensors);
            // Call frames are core state even when optional service extensions are excluded.
            if (output_[3] >= call_frame_version)
                fields(checkpoint.function, checkpoint.call_stack, checkpoint.evaluations);
            if (output_[3] >= event_wait_version)
                field(checkpoint.event_wait);
            if (output_[3] >= event_context_version)
                field(checkpoint.event_contexts);
            if (include_global_initialization_)
                fields(checkpoint.global_initialization_pending, checkpoint.global_initializer_cursor);
            if (include_direct_handler_)
                fields(checkpoint.pending_resume_fault, checkpoint.capture_point, checkpoint.resume_frames,
                       checkpoint.resume_entry_pending, checkpoint.execution_slice_elapsed_ticks);
            if (include_arrays_) {
                if (persistent_)
                    fields(std::optional<std::int32_t>{}, std::optional<std::int32_t>{});
                else
                    fields(checkpoint.arrays_heap_usage, checkpoint.locals_heap_usage);
                heap_fields(checkpoint);
                field(!persistent_ && checkpoint.heap_restore_pending);
            }
            if (include_class_owners_)
                field(persistent_ ? std::optional<framework::Uuid>{} : checkpoint.class_type_name_owner);
        } else if (include_extensions && (!checkpoint.listeners.empty() || has_permissions)) {
            output_[3] = has_permissions ? permissions_version : listener_version;
            field(checkpoint.listeners);
            if (has_permissions)
                fields(checkpoint.permissions_granter, checkpoint.permissions_mask);
        }
        return std::move(output_);
    }

    std::vector<std::uint8_t> encode_listeners(const std::vector<LslListenerCheckpoint> &listeners) {
        output_.assign(listener_signature.begin(), listener_signature.end());
        include_listener_regex_ = has_regex_listeners(listeners);
        if (include_listener_regex_)
            output_[3] = regex_listener_blob_version;
        field(listeners);
        return std::move(output_);
    }
};

class Reader {
    std::span<const std::uint8_t> remaining_;
    unsigned depth_ = 0;
    bool include_listener_regex_ = false;
    bool include_wait_background_ = false;
    bool include_global_initialization_ = false;
    bool include_direct_handler_ = false;
    bool allow_list_references_ = false;
    bool allow_boxed_values_ = false, read_boxed_value_ = false;
    bool allow_characters_ = false, read_character_ = false;
    bool allow_utf16_ = false, read_utf16_ = false;
    bool allow_nullable_ = false, read_nullable_ = false;
    bool allow_nullable_lists_ = false, read_nullable_lists_ = false;
    bool allow_raw_values_ = false, read_raw_values_ = false;
    bool allow_object_references_ = false, read_object_references_ = false;
    bool allow_arrays_ = false, read_arrays_ = false;
    bool allow_classes_ = false, read_classes_ = false;
    bool allow_class_names_ = false, read_class_names_ = false;
    std::vector<std::shared_ptr<const LslScriptClass>> classes_;
    std::vector<std::shared_ptr<const LslFixedArray>> arrays_;
    std::vector<std::shared_ptr<const LslStringReference>> string_references_;
    std::vector<std::shared_ptr<const LslObjectBox>> object_boxes_;
    std::vector<std::shared_ptr<const LslList>> lists_;

    [[noreturn]] static void invalid() {
        throw std::invalid_argument("Invalid LSL checkpoint");
    }

    std::span<const std::uint8_t> take(std::size_t count) {
        if (count > remaining_.size())
            invalid();
        const auto bytes = remaining_.first(count);
        remaining_ = remaining_.subspan(count);
        return bytes;
    }

    std::uint64_t number(unsigned width) {
        const auto bytes = take(width);
        std::uint64_t value = 0;
        for (unsigned i = 0; i < width; ++i)
            value |= std::uint64_t(bytes[i]) << (i * 8);
        return value;
    }

    void field(bool &value) {
        const auto bit = number(1);
        if (bit > 1)
            invalid();
        value = bit != 0;
    }

    void field(std::int32_t &value) {
        value = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(number(4)));
    }

    void field(std::int64_t &value) {
        value = std::bit_cast<std::int64_t>(number(8));
    }

    void field(std::size_t &value) {
        const auto size = number(8);
        if (size > std::numeric_limits<std::size_t>::max())
            invalid();
        value = static_cast<std::size_t>(size);
    }

    void field(double &value) {
        value = std::bit_cast<double>(number(8));
    }

    void field(LslRawSingle &value) {
        value.value = std::bit_cast<float>(static_cast<std::uint32_t>(number(4)));
    }

    void field(LslRawBoolean &value) {
        field(value.value);
    }

    void field(LslRawUuid &value) {
        field(value.value);
    }

    void field(std::string &value) {
        const auto bytes = take(static_cast<std::size_t>(number(4)));
        value.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    }

    void field(framework::Uuid &value) {
        value = framework::Uuid::from_bytes(take(16));
    }

    void field(framework::Vector3d &value) {
        fields(value.x, value.y, value.z);
    }

    void field(LslRotation &value) {
        fields(value.x, value.y, value.z, value.w);
    }

    void field(LslBoxedInteger &value) {
        field(value.value);
    }

    void field(LslBoxedFloat &value) {
        field(value.value);
    }

    void field(LslBoxedString &value) {
        field(value.value);
    }

    void field(std::u16string &value) {
        const auto count = number(4);
        if (count > remaining_.size() / 2)
            invalid();
        value.reserve(static_cast<std::size_t>(count));
        for (std::uint64_t i = 0; i < count; ++i)
            value.push_back(static_cast<char16_t>(number(2)));
    }

    void field(LslUtf16String &value) {
        field(value.value);
    }

    void field(LslBoxedUtf16String &value) {
        field(value.value);
    }

    void field(LslBoxedUtf16Key &value) {
        field(value.value);
    }

    void field(LslCharacter &value) {
        value.value = static_cast<char16_t>(number(2));
    }

    void field(LslBoxedKey &value) {
        field(value.value);
    }

    void field(LslEvent &value) {
        event_fields(*this, value);
    }

    void field(LslDetector &value) {
        detector_fields(*this, value);
        if (allow_utf16_) {
            field(value.sdk_name);
            if (value.sdk_name)
                read_utf16_ = true;
        }
    }

    void field(LslListenerCheckpoint &value) {
        listener_fields(*this, value);
        if (include_listener_regex_)
            field(value.regex_flags);
    }

    void field(LslSensorQuery &value) {
        fields(value.name, value.key, value.type, value.range, value.arc, value.interval);
        if (value.interval && (!std::isfinite(*value.interval) || *value.interval == 0.))
            invalid();
    }

    void field(LslEvaluationCheckpoint &value) {
        evaluation_fields(*this, value);
    }

    void field(LslCallFrameCheckpoint &value) {
        call_frame_fields(*this, value);
        if (include_direct_handler_)
            field(value.capture_point);
        if (allow_arrays_)
            heap_fields(value);
    }

    void field(LslCapturePointCheckpoint &value) {
        capture_point_fields(*this, value);
        if (allow_arrays_)
            heap_fields(value);
    }

    void field(LslResumeFrameCheckpoint &value) {
        resume_frame_fields(*this, value);
        if (allow_arrays_)
            heap_fields(value);
    }

    void field(LslEventWaitCheckpoint &value) {
        event_wait_fields(*this, value);
        if (include_wait_background_)
            fields(value.background_mask1, value.background_mask2);
    }

    void field(LslEventContextCheckpoint &value) {
        event_context_fields(*this, value);
        if (include_direct_handler_) {
            const auto kind = number(1);
            if (kind > static_cast<std::uint8_t>(LslEventContextKind::DirectHandler))
                invalid();
            value.kind = static_cast<LslEventContextKind>(kind);
            fields(value.event_wait, value.direct_call_expression);
        } else {
            field(value.event_wait.emplace());
        }
        if (include_global_initialization_)
            field(value.global_initializer_cursor);
        if (include_direct_handler_)
            fields(value.capture_point, value.invoked_event);
        if (allow_arrays_)
            heap_fields(value);
    }

    template <class T> void heap_fields(T &value) {
        fields(value.local_heap_base, value.local_object_heap_usage);
        if (has_local_ledger(value))
            read_arrays_ = true;
        if constexpr (requires { value.locals; })
            if (!value.local_object_heap_usage.empty() && value.local_object_heap_usage.size() != value.locals.size())
                invalid();
    }

    template <class T> void field(std::vector<T> &values) {
        const auto count = number(4);
        if (count > maximum_entries || count > remaining_.size())
            invalid();
        for (std::uint64_t i = 0; i < count; ++i)
            field(values.emplace_back());
    }

    template <class T> void field(std::optional<T> &value) {
        bool present;
        field(present);
        if (present)
            field(value.emplace());
    }

    void field(LslValue &value) {
        const auto tag = number(1);
        switch (tag) {
        case 0:
            field(value.emplace<std::int32_t>());
            break;
        case 1:
            field(value.emplace<std::string>());
            break;
        case 2:
            field(value.emplace<double>());
            break;
        case 3:
            field(value.emplace<framework::Vector3d>());
            break;
        case 4:
            field(value.emplace<LslRotation>());
            break;
        case 5:
            field(value.emplace<std::shared_ptr<const LslList>>());
            break;
        case 6: {
            if (!allow_list_references_)
                invalid();
            const auto index = number(4);
            if (index >= lists_.size())
                invalid();
            value = lists_[static_cast<std::size_t>(index)];
            break;
        }
        case 7:
            if (!allow_boxed_values_)
                invalid();
            read_boxed_value_ = true;
            field(value.emplace<LslBoxedInteger>());
            break;
        case 8:
            if (!allow_boxed_values_)
                invalid();
            read_boxed_value_ = true;
            field(value.emplace<LslBoxedFloat>());
            break;
        case 9:
            if (!allow_boxed_values_)
                invalid();
            read_boxed_value_ = true;
            field(value.emplace<LslBoxedString>());
            break;
        case 10:
            if (!allow_boxed_values_)
                invalid();
            read_boxed_value_ = true;
            field(value.emplace<LslBoxedKey>());
            break;
        case 11:
            if (!allow_characters_)
                invalid();
            read_character_ = true;
            field(value.emplace<LslCharacter>());
            break;
        case 12:
            if (!allow_utf16_)
                invalid();
            read_utf16_ = true;
            field(value.emplace<LslUtf16String>());
            break;
        case 13:
            if (!allow_utf16_)
                invalid();
            read_utf16_ = true;
            field(value.emplace<LslBoxedUtf16String>());
            break;
        case 14:
            if (!allow_utf16_)
                invalid();
            read_utf16_ = true;
            field(value.emplace<LslBoxedUtf16Key>());
            break;
        case 15:
            if (!allow_nullable_)
                invalid();
            read_nullable_ = true;
            value = LslNullString{};
            break;
        case 16:
            if (!allow_nullable_)
                invalid();
            read_nullable_ = true;
            value = LslBoxedNullString{};
            break;
        case 17:
            if (!allow_nullable_)
                invalid();
            read_nullable_ = true;
            value = LslBoxedNullKey{};
            break;
        case 18:
        case 19:
            if (!allow_nullable_lists_)
                invalid();
            read_nullable_lists_ = true;
            value = tag == 18 ? LslValue(LslNullList{}) : LslValue(LslNullObject{});
            break;
        case 20:
            if (!allow_raw_values_)
                invalid();
            read_raw_values_ = true;
            field(value.emplace<LslRawSingle>());
            break;
        case 21:
            if (!allow_raw_values_)
                invalid();
            read_raw_values_ = true;
            field(value.emplace<LslRawBoolean>());
            break;
        case 22:
            if (!allow_raw_values_)
                invalid();
            read_raw_values_ = true;
            field(value.emplace<LslRawUuid>());
            break;
        case 23:
            if (!allow_object_references_)
                invalid();
            read_object_references_ = true;
            field(value.emplace<std::shared_ptr<const LslStringReference>>());
            break;
        case 24:
            if (!allow_object_references_)
                invalid();
            read_object_references_ = true;
            field(value.emplace<std::shared_ptr<const LslObjectBox>>());
            break;
        case 25:
            if (!allow_arrays_)
                invalid();
            read_arrays_ = true;
            field(value.emplace<std::shared_ptr<const LslFixedArray>>());
            break;
        case 26:
            if (!allow_classes_)
                invalid();
            read_classes_ = true;
            field(value.emplace<std::shared_ptr<const LslScriptClass>>());
            break;
        default:
            invalid();
        }
    }

    void field(std::shared_ptr<const LslStringReference> &value) {
        const auto kind = number(1);
        if (kind == 1) {
            const auto index = number(4);
            if (index >= string_references_.size())
                invalid();
            value = string_references_[index];
        } else if (kind == 0) {
            if (string_references_.size() == maximum_entries)
                invalid();
            const auto origin = number(1);
            if (origin > (allow_class_names_ ? 4u : allow_arrays_ ? 3u : 2u))
                invalid();
            std::optional<LslClassTypeNameOrigin> class_type;
            if (origin == 4) {
                class_type.emplace();
                fields(class_type->owner, class_type->source_hash, class_type->canonical_name);
                read_class_names_ = true;
            }
            std::uint8_t array_type = 0;
            if (origin == 3) {
                const auto kind = number(1);
                if (kind > 3)
                    invalid();
                array_type = static_cast<std::uint8_t>(kind);
                read_arrays_ = true;
            }
            std::int32_t cached = -1;
            if (origin == 2) {
                field(cached);
                if (cached < 0 || cached >= 300)
                    invalid();
            }
            std::u16string units;
            field(units);
            const auto restored = origin == 4   ? make_lsl_class_type_name_string(*class_type)
                                  : origin == 3 ? make_lsl_cached_array_type_string(array_type)
                                  : origin == 2 ? make_lsl_cached_integer_string(cached)
                                                : make_lsl_reference_string(units, origin == 1);
            value = std::get<std::shared_ptr<const LslStringReference>>(restored);
            if (value->units != units)
                invalid();
            string_references_.push_back(value);
        } else
            invalid();
    }

    void field(std::shared_ptr<const LslObjectBox> &value) {
        const auto kind = number(1);
        if (kind == 1) {
            const auto index = number(4);
            if (index >= object_boxes_.size())
                invalid();
            value = object_boxes_[index];
        } else if (kind == 0) {
            if (object_boxes_.size() == maximum_entries || depth_ == maximum_depth)
                invalid();
            ++depth_;
            LslObjectBox box;
            field(box.value);
            bool field_present;
            field(field_present);
            if (field_present)
                field(box.string_field);
            --depth_;
            if (!valid_lsl_object_box(box))
                invalid();
            value = std::make_shared<const LslObjectBox>(std::move(box));
            object_boxes_.push_back(value);
        } else
            invalid();
    }

    void field(std::shared_ptr<const LslList> &value) {
        if (depth_ == maximum_depth)
            invalid();
        ++depth_;
        auto list = std::make_shared<LslList>();
        if (allow_arrays_)
            lists_.push_back(list);
        field(list->items);
        if (allow_object_references_) {
            bool present;
            field(present);
            if (present) {
                std::uint32_t hash;
                hash = static_cast<std::uint32_t>(number(4));
                if (!hash || hash > 0x3ffffffu)
                    invalid();
                list->identity_hash = hash;
                read_object_references_ = true;
            }
        }
        value = std::move(list);
        --depth_;
        if (allow_list_references_ && !allow_arrays_)
            lists_.push_back(value);
    }

    void field(std::shared_ptr<const LslScriptClass> &value) {
        const auto marker = number(1);
        if (marker == 1) {
            const auto index = number(4);
            if (index >= classes_.size())
                invalid();
            value = classes_[index];
            return;
        }
        if (marker != 0 || classes_.size() == maximum_entries || depth_ == maximum_depth)
            invalid();
        auto descriptor = std::make_shared<LslScriptClassDescriptor>();
        fields(descriptor->source_hash, descriptor->canonical_name, descriptor->element_type);
        if (allow_class_names_) {
            fields(descriptor->type_name_owner, descriptor->class_index);
            read_class_names_ =
                read_class_names_ || !descriptor->type_name_owner.is_zero() || descriptor->class_index >= 0;
        }
        descriptor->array_rank = static_cast<std::uint32_t>(number(4));
        std::uint64_t cells = 0;
        for (auto &size : descriptor->instance_sizes) {
            size = static_cast<std::uint32_t>(number(4));
            cells += size;
        }
        validate_class_descriptor(*descriptor);
        if (cells > remaining_.size())
            invalid();
        bool present;
        field(present);
        std::optional<std::uint32_t> hash;
        if (present) {
            const auto n = number(4);
            if (!n || n > 0x3ffffffu)
                invalid();
            hash = static_cast<std::uint32_t>(n);
        }
        std::int32_t heap_usage;
        field(heap_usage);
        value = std::get<std::shared_ptr<const LslScriptClass>>(make_lsl_script_class(descriptor));
        value->identity_hash = hash;
        value->arrays_heap_usage = heap_usage;
        classes_.push_back(value);
        ++depth_;
        for (auto &lane : value->instance_arrays)
            for (auto &cell : lane)
                field(cell);
        --depth_;
        validate_class_fields(*value);
    }

    void field(std::shared_ptr<const LslFixedArray> &value) {
        const auto marker = number(1);
        if (marker == 1) {
            const auto index = number(4);
            if (index >= arrays_.size())
                invalid();
            value = arrays_[index];
            return;
        }
        // Marker2 is a successfully written XML array argument whose CLR
        // Convert.ChangeType reload is unsupported. The public decoder fails closed.
        if (marker != 0 || arrays_.size() == maximum_entries || depth_ == maximum_depth)
            invalid();
        const auto kind = number(1);
        if (kind > 3)
            invalid();
        bool present;
        field(present);
        std::optional<std::uint32_t> hash;
        if (present) {
            const auto n = number(4);
            if (!n || n > 0x3ffffffu)
                invalid();
            hash = static_cast<std::uint32_t>(n);
        }
        const auto length = number(4);
        // Reject impossible lengths before allocating the declared storage.
        if (length > remaining_.size() / minimum_array_cell_bytes(static_cast<LslFixedArrayKind>(kind)))
            invalid();
        value = std::get<std::shared_ptr<const LslFixedArray>>(
            make_lsl_fixed_array(static_cast<LslFixedArrayKind>(kind), length));
        value->identity_hash = hash;
        arrays_.push_back(value);
        ++depth_;
        switch (value->kind) {
        case LslFixedArrayKind::Character:
            for (auto &cell : std::get<std::vector<char16_t>>(value->cells))
                cell = static_cast<char16_t>(number(2));
            break;
        case LslFixedArrayKind::Float:
            for (auto &cell : std::get<std::vector<double>>(value->cells))
                field(cell);
            break;
        case LslFixedArrayKind::Integer:
            for (auto &cell : std::get<std::vector<std::int32_t>>(value->cells))
                field(cell);
            break;
        case LslFixedArrayKind::Object:
            for (auto &cell : std::get<std::vector<LslValue>>(value->cells))
                field(cell);
            break;
        }
        --depth_;
    }

    void read_listeners(std::vector<LslListenerCheckpoint> &listeners) {
        field(listeners);
        std::set<std::int32_t> handles;
        for (const auto &listener : listeners)
            if (listener.handle <= 0 || !handles.insert(listener.handle).second)
                invalid();
    }

  public:
    explicit Reader(std::span<const std::uint8_t> bytes) : remaining_(bytes) {}

    template <class... T> void fields(T &...values) {
        (field(values), ...);
    }

    LslCheckpoint decode() {
        const auto header = take(signature.size());
        if (!std::ranges::equal(header.first(3), std::span(signature).first(3)) || header[3] < signature[3] ||
            header[3] > class_type_name_version)
            invalid();
        allow_object_references_ = header[3] >= object_reference_version;
        allow_arrays_ = header[3] >= fixed_array_version;
        allow_classes_ = header[3] >= script_class_version;
        allow_class_names_ = header[3] >= class_type_name_version;
        allow_raw_values_ = header[3] >= raw_event_value_version;
        allow_nullable_lists_ = header[3] >= nullable_list_version;
        allow_nullable_ = header[3] >= nullable_string_version;
        allow_utf16_ = header[3] >= utf16_version;
        allow_characters_ = header[3] >= character_version;
        allow_list_references_ = header[3] >= list_reference_version;
        allow_boxed_values_ = header[3] >= boxed_value_version;
        include_global_initialization_ = header[3] >= global_initialization_version;
        include_direct_handler_ = header[3] >= direct_handler_version;
        LslCheckpoint checkpoint;
        checkpoint_fields(*this, checkpoint);
        include_listener_regex_ = header[3] >= regex_listener_version;
        include_wait_background_ = header[3] >= event_context_version;
        if (header[3] >= listener_version)
            read_listeners(checkpoint.listeners);
        if (header[3] >= permissions_version)
            fields(checkpoint.permissions_granter, checkpoint.permissions_mask);
        if (header[3] >= start_parameter_version)
            field(checkpoint.start_parameter);
        if (header[3] >= sensor_version)
            field(checkpoint.sensors);
        if (header[3] >= call_frame_version)
            fields(checkpoint.function, checkpoint.call_stack, checkpoint.evaluations);
        if (header[3] >= event_wait_version) {
            field(checkpoint.event_wait);
            if (header[3] >= event_context_version)
                field(checkpoint.event_contexts);
            if ((header[3] == event_wait_version && !checkpoint.event_wait) ||
                (header[3] == event_context_version && !needs_event_context_version(checkpoint)) ||
                !valid_event_wait(checkpoint))
                invalid();
        }
        if (header[3] == boxed_value_version && !read_boxed_value_)
            invalid();
        if (include_global_initialization_) {
            fields(checkpoint.global_initialization_pending, checkpoint.global_initializer_cursor);
            if ((header[3] < character_version && !checkpoint.global_initialization_pending.has_value()) ||
                !valid_global_initialization(checkpoint))
                invalid();
        }
        if (include_direct_handler_) {
            fields(checkpoint.pending_resume_fault, checkpoint.capture_point, checkpoint.resume_frames,
                   checkpoint.resume_entry_pending, checkpoint.execution_slice_elapsed_ticks);
            if ((header[3] < character_version && !needs_direct_handler_version(checkpoint)) ||
                !valid_direct_handler_state(checkpoint))
                invalid();
        }
        if (allow_arrays_) {
            fields(checkpoint.arrays_heap_usage, checkpoint.locals_heap_usage);
            heap_fields(checkpoint);
            field(checkpoint.heap_restore_pending);
            if (checkpoint.arrays_heap_usage || checkpoint.locals_heap_usage || checkpoint.heap_restore_pending)
                read_arrays_ = true;
        }
        if (allow_class_names_) {
            field(checkpoint.class_type_name_owner);
            if (checkpoint.class_type_name_owner) {
                if (checkpoint.class_type_name_owner->is_zero())
                    invalid();
                read_class_names_ = true;
            }
        }
        if (header[3] == character_version && !read_character_)
            invalid();
        if (header[3] == nullable_list_version && !read_nullable_lists_)
            invalid();
        if (header[3] == object_reference_version && !read_object_references_)
            invalid();
        if (header[3] == class_type_name_version && !read_class_names_)
            invalid();
        if (header[3] == script_class_version && !read_classes_)
            invalid();
        if (header[3] == fixed_array_version && !read_arrays_)
            invalid();
        if (header[3] == raw_event_value_version && !read_raw_values_)
            invalid();
        if (header[3] == nullable_string_version && !read_nullable_)
            invalid();
        if (header[3] == utf16_version && !read_utf16_)
            invalid();
        if (!remaining_.empty())
            invalid();
        return checkpoint;
    }

    std::vector<LslListenerCheckpoint> decode_listeners() {
        const auto header = take(listener_signature.size());
        if (!std::ranges::equal(header.first(3), std::span(listener_signature).first(3)) ||
            header[3] < listener_signature[3] || header[3] > regex_listener_blob_version)
            invalid();
        include_listener_regex_ = header[3] >= regex_listener_blob_version;
        std::vector<LslListenerCheckpoint> listeners;
        read_listeners(listeners);
        if (!remaining_.empty())
            invalid();
        return listeners;
    }
};
} // namespace

std::vector<std::uint8_t> encode_lsl_checkpoint(const LslCheckpoint &checkpoint, bool include_extensions) {
    return Writer{}.encode(checkpoint, include_extensions);
}

std::vector<std::uint8_t> encode_persistent_lsl_checkpoint(const LslCheckpoint &checkpoint, bool include_extensions) {
    auto normalized = external_detector_state(checkpoint);
    try {
        for (auto &event : normalized.queued_events)
            event.arguments = normalize_yengine_xml_event_arguments(event.arguments);
    } catch (...) {
        const auto queue_error = std::current_exception();
        // GetExecutionState captures the binary VM and root DetectArray before
        // writing EventQueue XML. A queued null must not hide an earlier
        // binary character or root detector encoding failure.
        auto preceding = external_detector_state(checkpoint);
        preceding.queued_events.clear();
        (void)Writer{true}.encode(preceding, include_extensions);
        std::rethrow_exception(queue_error);
    }
    return Writer{true}.encode(normalized, include_extensions);
}

std::vector<std::uint8_t> encode_lsl_listeners(const std::vector<LslListenerCheckpoint> &listeners) {
    return Writer{}.encode_listeners(listeners);
}

std::optional<std::vector<LslListenerCheckpoint>> decode_lsl_listeners(std::span<const std::uint8_t> bytes) {
    try {
        return Reader{bytes}.decode_listeners();
    } catch (const std::invalid_argument &) {
        collect_lsl_fixed_arrays();
        return std::nullopt;
    }
}

std::optional<LslCheckpoint> decode_lsl_checkpoint(std::span<const std::uint8_t> bytes) {
    try {
        return Reader{bytes}.decode();
    } catch (const std::invalid_argument &) {
        return std::nullopt;
    }
}
} // namespace opensim::script
