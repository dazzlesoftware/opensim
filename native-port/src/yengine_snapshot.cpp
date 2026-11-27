// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <algorithm>
#include <bit>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace opensim::script {
namespace {
using Kind = YEngineManagedValue::Kind;

// Raw managed graphs can contain array/list cycles. The registry owns nothing;
// trial deletion distinguishes references inside the graph from caller roots.
// Construction and collection share a lock, while published graphs are immutable.
std::recursive_mutex managed_graph_mutex;
std::vector<std::weak_ptr<YEngineManagedValue>> managed_graph_nodes;

void register_managed_node(const std::shared_ptr<YEngineManagedValue> &node) {
    managed_graph_nodes.push_back(node);
}

void collect_managed_graphs() {
    std::lock_guard lock(managed_graph_mutex);
    std::vector<std::shared_ptr<YEngineManagedValue>> nodes;
    std::unordered_map<const YEngineManagedValue *, std::size_t> indices;
    std::erase_if(managed_graph_nodes, [&](const auto &weak) {
        auto node = weak.lock();
        if (!node)
            return true;
        indices.emplace(node.get(), nodes.size());
        nodes.push_back(std::move(node));
        return false;
    });
    std::vector<std::vector<std::size_t>> edges(nodes.size());
    std::vector<std::size_t> incoming(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto edge = [&](const YEngineManagedRef &child) {
            if (const auto found = indices.find(child.get()); found != indices.end()) {
                edges[index].push_back(found->second);
                ++incoming[found->second];
            }
        };
        if (const auto *array = std::get_if<YEngineManagedArray>(&nodes[index]->value)) {
            for (const auto &child : *array)
                edge(child);
        } else if (const auto *child = std::get_if<YEngineManagedRef>(&nodes[index]->value))
            edge(*child);
        else if (const auto *instance = std::get_if<YEngineManagedScriptClass>(&nodes[index]->value))
            for (const auto &array : instance->instance_arrays)
                edge(array);
    }
    std::vector<bool> reachable(nodes.size());
    std::vector<std::size_t> pending;
    for (std::size_t index = 0; index < nodes.size(); ++index)
        if (nodes[index].use_count() > static_cast<long>(incoming[index] + 1)) {
            reachable[index] = true;
            pending.push_back(index);
        }
    while (!pending.empty()) {
        const auto index = pending.back();
        pending.pop_back();
        for (const auto child : edges[index])
            if (!reachable[child]) {
                reachable[child] = true;
                pending.push_back(child);
            }
    }
    for (std::size_t index = 0; index < nodes.size(); ++index)
        if (!reachable[index])
            nodes[index]->value = std::int32_t{};
}

class SnapshotReader {
    std::span<const std::uint8_t> bytes_;
    std::vector<std::optional<YEngineManagedRef>> references_;
    std::unordered_map<const YEngineManagedValue *, std::shared_ptr<const LslList>> backing_lists_;
    // Every original value consumes at least one wire byte. A large primitive
    // array is not a collection of distinct reference definitions.
    std::size_t entries_left_, projected_entries_left_;
    std::size_t string_bytes_left_, projected_string_bytes_left_;

    [[noreturn]] static void invalid() {
        throw std::invalid_argument("Unsupported YEngine snapshot");
    }

    static void consume(std::size_t &budget, std::size_t count) {
        if (count > budget)
            invalid();
        budget -= count;
    }

    std::span<const std::uint8_t> take(std::size_t count) {
        if (count > bytes_.size())
            invalid();
        const auto field = bytes_.first(count);
        bytes_ = bytes_.subspan(count);
        return field;
    }

    template <class Number> Number number() {
        using Bits = std::conditional_t<sizeof(Number) == 8, std::uint64_t, std::uint32_t>;
        Bits bits = 0;
        unsigned shift = 0;
        for (const auto byte : take(sizeof(Number))) {
            bits |= static_cast<Bits>(byte) << shift;
            shift += 8;
        }
        return std::bit_cast<Number>(bits);
    }

    std::string text() {
        std::uint32_t length = 0;
        for (unsigned shift = 0; shift <= 28; shift += 7) {
            const auto byte = take(1).front();
            if (shift == 28 && byte > 7)
                invalid();
            length |= static_cast<std::uint32_t>(byte & 127) << shift;
            if (!(byte & 128)) {
                consume(string_bytes_left_, length);
                const auto field = take(length);
                return {reinterpret_cast<const char *>(field.data()), field.size()};
            }
        }
        invalid();
    }

    char16_t character() {
        // BinaryWriter.Write(char) writes UTF-8 rather than a little-endian UInt16.
        const auto first = take(1).front();
        if (first < 0x80)
            return static_cast<char16_t>(first);
        const unsigned count = first >= 0xc2 && first <= 0xdf ? 2 : first >= 0xe0 && first <= 0xef ? 3 : 0;
        if (!count)
            invalid();
        std::uint32_t value = first & (count == 2 ? 0x1fu : 0x0fu);
        for (unsigned index = 1; index < count; ++index) {
            const auto next = take(1).front();
            if ((next & 0xc0u) != 0x80u)
                invalid();
            value = (value << 6) | (next & 0x3fu);
        }
        if ((count == 3 && value < 0x800) || (value >= 0xd800 && value <= 0xdfff))
            invalid();
        return static_cast<char16_t>(value);
    }

    YEngineManagedRef object(unsigned depth = 0) {
        if (depth >= 64)
            invalid();
        consume(entries_left_, 1);
        const auto tag = take(1).front();
        if (tag == 15) {
            const auto index = number<std::uint32_t>();
            if (index >= references_.size() || !references_[index])
                invalid();
            return *references_[index];
        }
        auto node = std::make_shared<YEngineManagedValue>();
        node->kind = static_cast<Kind>(tag);
        switch (node->kind) {
        case Kind::Null:
        case Kind::Instance:
            return node;
        case Kind::EventCode:
        case Kind::LslInteger:
        case Kind::Integer:
            node->value = number<std::int32_t>();
            return node;
        case Kind::LslFloat:
        case Kind::Double:
            node->value = number<double>();
            return node;
        case Kind::Float:
            node->value = number<float>();
            return node;
        case Kind::Boolean:
            node->value = take(1).front() != 0;
            return node;
        case Kind::Character:
            node->value = character();
            return node;
        case Kind::LslRotation:
            node->value = LslRotation{number<double>(), number<double>(), number<double>(), number<double>()};
            return node;
        case Kind::LslVector:
            node->value = framework::Vector3d{number<double>(), number<double>(), number<double>()};
            return node;
        case Kind::LslKey:
        case Kind::LslString: {
            const auto child = object(depth + 1);
            if (child->kind != Kind::Null && child->kind != Kind::String)
                invalid();
            node->value = child;
            return node;
        }
        case Kind::ScriptClass: {
            // RecvObjValue publishes the empty class before Restore reads its
            // class index and instance arrays, allowing references back to self.
            references_.emplace_back(node);
            register_managed_node(node);
            const auto class_index = object(depth + 1);
            if (class_index->kind != Kind::Integer || std::get<std::int32_t>(class_index->value) < 0)
                invalid();
            YEngineManagedScriptClass instance;
            instance.class_index = std::get<std::int32_t>(class_index->value);
            for (auto &array : instance.instance_arrays) {
                array = object(depth + 1);
                if (array->kind != Kind::Null && array->kind != Kind::Array)
                    invalid();
            }
            node->value = std::move(instance);
            return node;
        }
        case Kind::String:
        case Kind::Array:
        case Kind::LslList: {
            const auto index = references_.size();
            references_.emplace_back();
            // Arrays are visible to their own elements in RecvObjValue. Lists
            // retain the original pending placeholder until their backing is read.
            if (node->kind == Kind::Array) {
                references_[index] = node;
                register_managed_node(node);
            } else if (node->kind == Kind::LslList) {
                // RecvObjValue installs a null placeholder for LSLLIST until
                // its backing array has finished loading.
                references_[index] = std::make_shared<YEngineManagedValue>();
                register_managed_node(node);
            }
            if (node->kind == Kind::String) {
                node->value = text();
            } else if (node->kind == Kind::Array) {
                node->element_type = text();
                const auto length = number<std::int32_t>();
                if (length < 0 || static_cast<std::size_t>(length) > bytes_.size())
                    invalid();
                YEngineManagedArray values;
                values.reserve(static_cast<std::size_t>(length));
                for (int index = 0; index < length; ++index)
                    values.push_back(object(depth + 1));
                node->value = std::move(values);
            } else {
                const auto child = object(depth + 1);
                if (child->kind != Kind::Null && child->kind != Kind::Array)
                    invalid();
                if (child->kind == Kind::Array && child->element_type != "ob")
                    invalid();
                node->value = child;
            }
            references_[index] = node;
            return node;
        }
        }
        // Delegates, XMR arrays and BinaryFormatter objects
        // require separate decoders; no managed activation is performed here.
        invalid();
    }

    static bool array_or_null(const YEngineManagedRef &value) {
        return value->kind == Kind::Array || value->kind == Kind::Null;
    }

    std::optional<LslValue> project(const YEngineManagedRef &value, unsigned depth = 0) {
        if (depth >= 64)
            return {};
        consume(projected_entries_left_, 1);
        switch (value->kind) {
        case Kind::LslInteger:
            return LslValue(LslBoxedInteger{std::get<std::int32_t>(value->value)});
        case Kind::Integer:
            return LslValue(std::get<std::int32_t>(value->value));
        case Kind::LslFloat:
            return LslValue(LslBoxedFloat{std::get<double>(value->value)});
        case Kind::Double:
            return LslValue(std::get<double>(value->value));
        case Kind::Float:
            return LslValue(LslRawSingle{std::get<float>(value->value)});
        case Kind::String: {
            const auto &text = std::get<std::string>(value->value);
            consume(projected_string_bytes_left_, text.size());
            return LslValue(text);
        }
        case Kind::LslKey:
        case Kind::LslString: {
            const auto &child = std::get<YEngineManagedRef>(value->value);
            if (child->kind != Kind::String && child->kind != Kind::Null)
                return {};
            if (child->kind == Kind::Null)
                return LslValue(LslBoxedNullString{});
            const auto text = std::get<std::string>(child->value);
            consume(projected_string_bytes_left_, text.size());
            // Original XMRInstAbstract aliases LSL_Key to LSLString, so binary tag 4 is a string wrapper.
            return LslValue(LslBoxedString{text});
        }
        case Kind::Character:
            return LslValue(LslCharacter{std::get<char16_t>(value->value)});
        case Kind::LslVector:
            return LslValue(std::get<framework::Vector3d>(value->value));
        case Kind::LslRotation: {
            auto rotation = std::get<LslRotation>(value->value);
            // Recv constructs an LSL Quaternion; retain the raw graph above,
            // but apply its all-zero constructor rule to script values.
            if (rotation.x == 0. && rotation.y == 0. && rotation.z == 0. && rotation.w == 0.)
                rotation.w = 1.;
            return LslValue(rotation);
        }
        case Kind::LslList: {
            const auto &backing = std::get<YEngineManagedRef>(value->value);
            const auto key = backing->kind == Kind::Null ? value.get() : backing.get();
            auto &cached = backing_lists_[key];
            if (cached)
                return LslValue(cached);
            std::vector<LslValue> items;
            if (backing->kind != Kind::Null) {
                for (const auto &element : std::get<YEngineManagedArray>(backing->value)) {
                    auto item = element->kind == Kind::Null ? std::optional<LslValue>(LslNullObject{})
                                                            : project(element, depth + 1);
                    if (!item)
                        return {};
                    items.push_back(std::move(*item));
                }
            }
            cached = std::make_shared<const LslList>(LslList{std::move(items)});
            return LslValue(cached);
        }
        default:
            return {};
        }
    }

    bool project_array(const YEngineManagedRef &value, std::vector<LslValue> &destination,
                       bool scalar_storage = false) {
        if (value->kind == Kind::Null)
            return true;
        if (value->kind != Kind::Array)
            return false;
        for (const auto &element : std::get<YEngineManagedArray>(value->value)) {
            auto item = project(element);
            if (!item && element->kind == Kind::Null && value->element_type == "st")
                item = LslValue(LslNullString{});
            if (!item && element->kind == Kind::Null && value->element_type == "li")
                item = LslValue(LslNullList{});
            if (!item && element->kind == Kind::Null && value->element_type == "ob")
                item = LslValue(LslNullObject{});
            if (!item)
                return false;
            destination.push_back(scalar_storage ? unbox_lsl_value(*item) : std::move(*item));
        }
        return true;
    }

  public:
    explicit SnapshotReader(std::span<const std::uint8_t> bytes)
        : bytes_(bytes), entries_left_(bytes.size()), projected_entries_left_(bytes.size()),
          string_bytes_left_(bytes.size()), projected_string_bytes_left_(bytes.size()) {}

    YEngineSnapshot read() {
        if (take(1).front() != 12)
            invalid();
        take(1); // Original reserved byte is ignored.
        YEngineSnapshot snapshot{};
        snapshot.managed_event_arguments = object();
        if (!array_or_null(snapshot.managed_event_arguments))
            invalid();
        const auto initialize_globals = take(1).front();
        if (initialize_globals > 1)
            invalid();
        const auto state = number<std::int32_t>(), event = number<std::int32_t>();
        if (state < 0 || event < -1 || event >= 42)
            invalid();
        if (event == -1 && snapshot.managed_event_arguments->kind == Kind::Array &&
            !std::get<YEngineManagedArray>(snapshot.managed_event_arguments->value).empty())
            invalid();
        snapshot.state_index = static_cast<std::size_t>(state);
        snapshot.event_code = event;
        snapshot.initialize_globals = initialize_globals != 0;
        for (auto &globals : snapshot.managed_globals) {
            globals = object();
            if (!array_or_null(globals))
                invalid();
        }
        for (;;) {
            auto function = text();
            if (function.empty())
                break;
            if (function == "**newStateCode**") {
                const auto target = number<std::int32_t>();
                if (target < 0 || event != 1)
                    invalid();
                snapshot.next_state_index = static_cast<std::size_t>(target);
                continue;
            }
            consume(entries_left_, 1);
            const auto call_no = number<std::int32_t>();
            const auto slots = object();
            if (!array_or_null(slots) || (slots->kind == Kind::Array && slots->element_type != "ob"))
                invalid();
            snapshot.managed_frames.push_back({std::move(function), call_no, slots});
        }
        if (!bytes_.empty())
            invalid();
        snapshot.lsl_projection_complete = project_array(snapshot.managed_event_arguments, snapshot.event_arguments);
        for (std::size_t index = 0; index < snapshot.globals.size(); ++index)
            if (!project_array(snapshot.managed_globals[index], snapshot.globals[index], true))
                snapshot.lsl_projection_complete = false;
        return snapshot;
    }
};
} // namespace

std::optional<YEngineSnapshot> decode_yengine_snapshot(std::span<const std::uint8_t> bytes) {
    std::lock_guard lock(managed_graph_mutex);

    struct CollectAfterDecode {
        ~CollectAfterDecode() {
            try {
                collect_managed_graphs();
            } catch (...) {
            }
        }
    } collect_after_decode;

    try {
        return SnapshotReader(bytes).read();
    } catch (const std::invalid_argument &) {
        return std::nullopt;
    }
}

void collect_yengine_managed_cycles() noexcept {
    try {
        collect_managed_graphs();
    } catch (...) {
        // A later collection can retry after a temporary allocation failure.
    }
}
} // namespace opensim::script
