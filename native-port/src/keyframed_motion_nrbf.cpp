// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/keyframed_motion_nrbf.hpp"
#include "opensim/framework/nrbf_graph.hpp"
#include <bit>
#include <stdexcept>

namespace opensim::scene {
namespace {
using namespace framework;
using N = std::shared_ptr<NrbfNode>;
constexpr std::string_view root_name = "OpenSim.Region.Framework.Scenes.KeyframeMotion";
[[noreturn]] void bad();

// Emit ordinary typed NRBF records. CLR activation is confined to the original
// comparison oracle; the native runtime only writes named value records.
struct MotionWriter {
    std::vector<std::uint8_t> bytes;
    std::uint32_t next = 2;

    void byte(std::uint8_t v) {
        bytes.push_back(v);
    }

    void number(std::uint32_t v) {
        for (int i = 0; i < 4; ++i)
            byte(static_cast<std::uint8_t>(v >> (i * 8)));
    }

    void text(std::string_view v) {
        if (v.size() > INT32_MAX)
            bad();
        auto count = v.size();
        while (count >= 128) {
            byte(static_cast<std::uint8_t>(count | 128));
            count >>= 7;
        }
        byte(static_cast<std::uint8_t>(count));
        bytes.insert(bytes.end(), v.begin(), v.end());
    }

    void library(int id, std::string_view name) {
        byte(12);
        number(id);
        text(name);
    }

    void reference(int id) {
        byte(9);
        number(id);
    }

    struct Field {
        const char *name;
        std::uint8_t primitive = 0;
    };

    void record(int id, std::string_view name, std::initializer_list<Field> fields, int library) {
        byte(library ? 5 : 4);
        number(id);
        text(name);
        number(static_cast<std::uint32_t>(fields.size()));
        for (auto f : fields)
            text(f.name);
        for (auto f : fields)
            byte(f.primitive ? 0 : 2);
        for (auto f : fields)
            if (f.primitive)
                byte(f.primitive);
        if (library)
            number(library);
    }

    void vector(Vector3 v) {
        record(next++, "OpenMetaverse.Vector3", {{"X", 11}, {"Y", 11}, {"Z", 11}}, 3);
        number(std::bit_cast<std::uint32_t>(v.x));
        number(std::bit_cast<std::uint32_t>(v.y));
        number(std::bit_cast<std::uint32_t>(v.z));
    }

    void rotation(Quaternion v) {
        record(next++, "OpenMetaverse.Quaternion", {{"X", 11}, {"Y", 11}, {"Z", 11}, {"W", 11}}, 3);
        number(std::bit_cast<std::uint32_t>(v.x));
        number(std::bit_cast<std::uint32_t>(v.y));
        number(std::bit_cast<std::uint32_t>(v.z));
        number(std::bit_cast<std::uint32_t>(v.w));
    }

    void frame(const KeyframedMotionFrame &f) {
        record(next++, std::string(root_name) + "+Keyframe",
               {{"Position"},
                {"Rotation"},
                {"StartRotation"},
                {"TimeMS", 8},
                {"TimeTotal", 8},
                {"AngularVelocity"},
                {"StartPosition"}},
               2);
        if (f.position)
            vector(*f.position);
        else
            byte(10);
        if (f.rotation)
            rotation(*f.rotation);
        else
            byte(10);
        rotation(f.start_rotation);
        number(static_cast<std::uint32_t>(f.time_ms));
        number(static_cast<std::uint32_t>(f.time_total));
        vector(f.angular_velocity);
        vector(f.start_position);
    }

    void frames(int id, const std::vector<KeyframedMotionFrame> &frames) {
        if (frames.size() > INT32_MAX)
            bad();
        byte(7);
        number(id);
        byte(0);
        number(1);
        number(static_cast<std::uint32_t>(frames.size()));
        byte(4);
        text(std::string(root_name) + "+Keyframe");
        number(2);
        for (const auto &f : frames)
            frame(f);
    }

    void enumeration(const char *name, std::int32_t value) {
        record(next++, std::string(root_name) + name, {{"value__", 8}}, 2);
        number(static_cast<std::uint32_t>(value));
    }
};

[[noreturn]] void bad() {
    throw std::invalid_argument("Invalid keyframed motion NRBF graph");
}

N member(const NrbfGraph &graph, const N &node, const char *name) {
    if (!node || std::find(node->names.begin(), node->names.end(), name) == node->names.end())
        bad();
    return graph.field(node, name);
}

void type(const N &node, std::string_view name, std::string_view library) {
    if (!node || node->kind != 100 || node->text != name || !node->library.starts_with(library))
        bad();
}

std::int32_t integer(const N &node) {
    if (!node || node->kind != 8)
        bad();
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(node->bits));
}

float real(const N &node) {
    if (!node || node->kind != 11)
        bad();
    return std::bit_cast<float>(static_cast<std::uint32_t>(node->bits));
}

Vector3 vector(const NrbfGraph &graph, const N &node) {
    type(node, "OpenMetaverse.Vector3", "OpenMetaverseTypes,");
    return {real(member(graph, node, "X")), real(member(graph, node, "Y")), real(member(graph, node, "Z"))};
}

Quaternion rotation(const NrbfGraph &graph, const N &node) {
    type(node, "OpenMetaverse.Quaternion", "OpenMetaverseTypes,");
    return {real(member(graph, node, "X")), real(member(graph, node, "Y")), real(member(graph, node, "Z")),
            real(member(graph, node, "W"))};
}

KeyframedMotionFrame frame(const NrbfGraph &graph, const N &node) {
    type(node, std::string(root_name) + "+Keyframe", "OpenSim.Region.Framework,");
    KeyframedMotionFrame result;
    if (auto position = member(graph, node, "Position"))
        result.position = vector(graph, position);
    if (auto orientation = member(graph, node, "Rotation"))
        result.rotation = rotation(graph, orientation);
    result.start_rotation = rotation(graph, member(graph, node, "StartRotation"));
    result.time_ms = integer(member(graph, node, "TimeMS"));
    result.time_total = integer(member(graph, node, "TimeTotal"));
    result.angular_velocity = vector(graph, member(graph, node, "AngularVelocity"));
    result.start_position = vector(graph, member(graph, node, "StartPosition"));
    return result;
}

std::vector<KeyframedMotionFrame> frames(const NrbfGraph &graph, const N &node) {
    if (!node || node->kind != 103)
        bad();
    std::vector<KeyframedMotionFrame> result;
    result.reserve(node->values.size());
    for (const auto &entry : node->values)
        result.push_back(frame(graph, graph.resolve(entry)));
    return result;
}
} // namespace

std::optional<KeyframedMotionSerializedState> decode_keyframed_motion(std::span<const std::uint8_t> bytes) {
    try {
        const auto parsed = NrbfGraph::decode(bytes);
        if (!parsed)
            return std::nullopt;
        const auto &graph = *parsed;
        const auto &root = graph.root;
        type(root, root_name, "OpenSim.Region.Framework,");
        KeyframedMotionSerializedState result;
        auto &state = result.playback;
        state.serialized_position = vector(graph, member(graph, root, "m_serializedPosition"));
        state.base_position = vector(graph, member(graph, root, "m_basePosition"));
        state.base_rotation = rotation(graph, member(graph, root, "m_baseRotation"));
        state.current = frame(graph, member(graph, root, "m_currentFrame"));
        const auto active = member(graph, root, "m_frames");
        if (!active || active->kind != 100 ||
            !active->text.starts_with(
                "System.Collections.Generic.List`1[[OpenSim.Region.Framework.Scenes.KeyframeMotion+Keyframe,"))
            bad();
        const auto backing = member(graph, active, "_items");
        result.backing_frames = frames(graph, backing);
        const auto size = integer(member(graph, active, "_size"));
        if (size < 0 || static_cast<std::size_t>(size) > result.backing_frames.size())
            bad();
        state.frames.assign(result.backing_frames.begin(), result.backing_frames.begin() + size);
        result.list_version = integer(member(graph, active, "_version"));
        state.frame_backing = result.backing_frames;
        state.frame_list_version = result.list_version;
        const auto keys = member(graph, root, "m_keyframes");
        if (keys)
            state.keyframes = frames(graph, keys);
        result.keyframes_share_backing = keys && keys == backing;
        state.keyframes_share_backing = result.keyframes_share_backing;
        const auto mode = member(graph, root, "m_mode");
        type(mode, std::string(root_name) + "+PlayMode", "OpenSim.Region.Framework,");
        state.mode = static_cast<script::KeyframedMotionMode>(integer(member(graph, mode, "value__")));
        const auto data = member(graph, root, "m_data");
        type(data, std::string(root_name) + "+DataFormat", "OpenSim.Region.Framework,");
        state.data = integer(member(graph, data, "value__"));
        const auto running = member(graph, root, "m_running");
        if (!running || running->kind != 1)
            bad();
        state.running = running->bits != 0;
        state.iterations = integer(member(graph, root, "m_iterations"));
        state.skip_loops = integer(member(graph, root, "m_skipLoops"));
        // BinaryFormatter bypasses the constructor. NonSerialized fields get
        // CLR defaults, not the live constructor's timer_stopped=true value.
        state.timer_stopped = false;
        return result;
    } catch (...) {
        return std::nullopt;
    }
}

std::vector<std::uint8_t> encode_keyframed_motion(const KeyframedMotionSerializedState &state) {
    const auto &s = state.playback;
    if (s.frames.size() > state.backing_frames.size() || s.frames.size() > INT32_MAX)
        bad();
    MotionWriter w;
    w.byte(0);
    w.number(1);
    w.number(UINT32_MAX);
    w.number(1);
    w.number(0);
    w.library(2, "OpenSim.Region.Framework, Version=0.9.3.1, Culture=neutral, PublicKeyToken=null");
    w.library(3, "OpenMetaverseTypes, Version=0.0.0.0, Culture=neutral, PublicKeyToken=null");
    // Library IDs and object IDs occupy independent NRBF namespaces.
    w.record(1, root_name,
             {{"m_serializedPosition"},
              {"m_basePosition"},
              {"m_baseRotation"},
              {"m_currentFrame"},
              {"m_frames"},
              {"m_keyframes"},
              {"m_mode"},
              {"m_data"},
              {"m_running", 1},
              {"m_iterations", 8},
              {"m_skipLoops", 8}},
             2);
    w.vector(s.serialized_position);
    w.vector(s.base_position);
    w.rotation(s.base_rotation);
    w.frame(s.current);
    w.record(w.next++,
             "System.Collections.Generic.List`1[[OpenSim.Region.Framework.Scenes.KeyframeMotion+Keyframe, "
             "OpenSim.Region.Framework, Version=0.9.3.1, Culture=neutral, PublicKeyToken=null]]",
             {{"_items"}, {"_size", 8}, {"_version", 8}}, 0);
    const auto backing_id = w.next++;
    w.reference(backing_id);
    w.number(static_cast<std::uint32_t>(s.frames.size()));
    w.number(static_cast<std::uint32_t>(state.list_version));
    std::uint32_t keys_id = 0;
    if (s.keyframes) {
        keys_id = state.keyframes_share_backing ? backing_id : w.next++;
        w.reference(keys_id);
    } else
        w.byte(10);
    w.enumeration("+PlayMode", static_cast<std::int32_t>(s.mode));
    w.enumeration("+DataFormat", s.data);
    w.byte(s.running);
    w.number(static_cast<std::uint32_t>(s.iterations));
    w.number(static_cast<std::uint32_t>(s.skip_loops));
    auto backing = state.backing_frames;
    // The active range is authoritative; retain unused serialized capacity.
    std::copy(s.frames.begin(), s.frames.end(), backing.begin());
    w.frames(backing_id, backing);
    if (keys_id && keys_id != backing_id)
        w.frames(keys_id, *s.keyframes);
    w.byte(11);
    return std::move(w.bytes);
}
} // namespace opensim::scene
