// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/keyframed_motion.hpp"
#include "opensim/scene/keyframed_motion_nrbf.hpp"
#include "opensim/network/osd_json.hpp"
#include <bit>
#include <iostream>
using namespace opensim;
using namespace scene;
using namespace framework;
using network::Value;

namespace {
Value array(std::vector<Value> items) {
    Value out;
    out.kind = Value::Kind::Array;
    out.array = std::move(items);
    return out;
}

double number(const Value &v) {
    return v.kind == Value::Kind::Real ? v.real : v.integer;
}

Vector3 vector(const Value &v) {
    return {float(number(v.array.at(0))), float(number(v.array.at(1))), float(number(v.array.at(2)))};
}

Quaternion rotation(const Value &v) {
    return {float(number(v.array.at(0))), float(number(v.array.at(1))), float(number(v.array.at(2))),
            float(number(v.array.at(3)))};
}

Value bits(std::initializer_list<float> values) {
    std::vector<Value> out;
    for (auto value : values)
        out.emplace_back(std::bit_cast<std::int32_t>(value));
    return array(std::move(out));
}

Value bits(Vector3 v) {
    return bits({v.x, v.y, v.z});
}

Value bits(Quaternion q) {
    return bits({q.x, q.y, q.z, q.w});
}

Value double_bits(double value) {
    const auto raw = std::bit_cast<std::uint64_t>(value);
    return array({Value(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(raw))),
                  Value(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(raw >> 32)))});
}

std::string trace(const char *name, std::initializer_list<float> values) {
    std::string out = name;
    for (auto value : values) {
        if (out.back() != ':')
            out += ',';
        out += std::to_string(std::bit_cast<std::int32_t>(value));
    }
    return out;
}

std::vector<KeyframedMotionFrame> frames(const Value &input) {
    std::vector<KeyframedMotionFrame> out;
    for (const auto &item : input.array) {
        KeyframedMotionFrame f;
        if (!item.get("p").array.empty())
            f.position = vector(item.get("p"));
        if (!item.get("r").array.empty())
            f.rotation = rotation(item.get("r"));
        f.time_ms = item.get("ms").integer;
        out.push_back(f);
    }
    return out;
}

Value frame(const KeyframedMotionFrame &f) {
    return Value::object({{"p", f.position ? bits(*f.position) : array({})},
                          {"r", f.rotation ? bits(*f.rotation) : array({})},
                          {"ms", Value(f.time_ms)},
                          {"total", Value(f.time_total)},
                          {"startP", bits(f.start_position)},
                          {"startR", bits(f.start_rotation)},
                          {"angular", bits(f.angular_velocity)}});
}

Value frame_list(const std::vector<KeyframedMotionFrame> &input) {
    std::vector<Value> out;
    for (const auto &f : input)
        out.push_back(frame(f));
    return array(std::move(out));
}
} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto input = network::parse_osd_json(line);
        if (input.map.contains("nrbf")) {
            const auto &hex = input.get("nrbf").text;
            std::vector<std::uint8_t> bytes;
            for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
                bytes.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
            const auto decoded = decode_keyframed_motion(bytes);
            if (decoded && input.get("export").boolean) {
                const auto encoded = encode_keyframed_motion(*decoded);
                std::string hex;
                for (auto b : encoded) {
                    hex += "0123456789abcdef"[b >> 4];
                    hex += "0123456789abcdef"[b & 15];
                }
                std::cout << network::osd_json(Value::object({{"nrbf", Value(hex)}}), true) << '\n';
                continue;
            }
            Value result;
            if (decoded) {
                const auto &s = decoded->playback;
                result = Value::object({{"origin", bits(s.serialized_position)},
                                        {"base", bits(s.base_position)},
                                        {"rotation", bits(s.base_rotation)},
                                        {"current", frame(s.current)},
                                        {"frames", frame_list(s.frames)},
                                        {"backing", frame_list(decoded->backing_frames)},
                                        {"keys", s.keyframes ? frame_list(*s.keyframes) : Value{}},
                                        {"version", Value(decoded->list_version)},
                                        {"shared", Value(decoded->keyframes_share_backing)},
                                        {"mode", Value(static_cast<std::int32_t>(s.mode))},
                                        {"data", Value(s.data)},
                                        {"running", Value(s.running)},
                                        {"iterations", Value(s.iterations)},
                                        {"skip", Value(s.skip_loops)},
                                        {"timerStopped", Value(s.timer_stopped)},
                                        {"selected", Value(s.selected)},
                                        {"crossing", Value(s.crossing)},
                                        {"waiting", Value(s.waiting_crossing)}});
            }
            std::cout << (decoded ? network::osd_json(result, true) : "null") << '\n';
            continue;
        }
        auto p = vector(input.get("position"));
        auto r = rotation(input.get("rotation"));
        Vector3 v{}, a{};
        double now = 0;
        bool boundary = input.get("boundary").boolean;
        bool position_error = false;
        std::vector<Value> events;
        KeyframedMotion *current = nullptr;
        KeyframedMotionHost host;
        host.position = [&] {
            if (position_error)
                throw std::runtime_error("Position callback failed");
            return p;
        };
        host.rotation = [&] { return r; };
        host.velocity = [&] { return v; };
        host.now_ms = [&] { return now; };
        host.set_position = [&](Vector3 value) {
            events.emplace_back(trace("position:", {value.x, value.y, value.z}));
            if (boundary && value.x >= 256)
                current->start_crossing_check();
            else
                p = value;
        };
        host.set_rotation = [&](Quaternion value) {
            events.emplace_back(trace("rotation:", {value.x, value.y, value.z, value.w}));
            r = value;
        };
        host.set_velocity = [&](Vector3 value) {
            events.emplace_back(trace("velocity:", {value.x, value.y, value.z}));
            v = value;
        };
        host.set_angular_velocity = [&](Vector3 value) {
            events.emplace_back(trace("angular:", {value.x, value.y, value.z}));
            a = value;
        };
        host.timer_add = [&] { events.emplace_back("timer_add"); };
        host.timer_remove = [&] { events.emplace_back("timer_remove"); };
        host.terse = [&] { events.emplace_back("terse"); };
        host.moving_start = [&] { events.emplace_back("moving_start"); };
        host.moving_end = [&] { events.emplace_back("moving_end"); };
        KeyframedMotion motion(host, static_cast<script::KeyframedMotionMode>(input.get("mode").integer),
                               input.get("data").kind == Value::Kind::Undefined ? 3 : input.get("data").integer);
        current = &motion;
        if (!input.get("nullFrames").boolean)
            motion.set_keyframes(frames(input.get("frames")));
        const auto state_snapshot = [&](const KeyframedMotion &item, Vector3 p, Quaternion r, Vector3 v, Vector3 a) {
            const auto &s = item.state();
            return Value::object({{"p", bits(p)},
                                  {"r", bits(r)},
                                  {"v", bits(v)},
                                  {"a", bits(a)},
                                  {"baseP", bits(s.base_position)},
                                  {"baseR", bits(s.base_rotation)},
                                  {"serialized", bits(s.serialized_position)},
                                  {"next", bits(s.next_position)},
                                  {"currentV", bits(s.current_velocity)},
                                  {"current", frame(s.current)},
                                  {"frames", frame_list(s.frames)},
                                  {"backing", frame_list(s.frame_backing)},
                                  {"shared", Value(s.keyframes_share_backing)},
                                  {"version", Value(s.frame_list_version)},
                                  {"keysNull", Value(!s.keyframes)},
                                  {"keys", frame_list(s.keyframes.value_or(std::vector<KeyframedMotionFrame>{}))},
                                  {"stopped", Value(s.timer_stopped)},
                                  {"running", Value(s.running)},
                                  {"crossing", Value(s.crossing)},
                                  {"waiting", Value(s.waiting_crossing)},
                                  {"selected", Value(s.selected)},
                                  {"iterations", Value(s.iterations)},
                                  {"skip", Value(s.skip_loops)},
                                  {"last", double_bits(s.last_tick_ms)},
                                  {"mode", Value(static_cast<std::int32_t>(s.mode))},
                                  {"data", Value(s.data)}});
        };
        Vector3 cp{}, cv{}, ca{};
        Quaternion cr{};
        std::unique_ptr<KeyframedMotion> copied;
        const auto copy_host = [&] {
            auto result = host;
            result.position = [&] { return cp; };
            result.rotation = [&] { return cr; };
            result.velocity = [&] { return cv; };
            result.set_position = [&](Vector3 value) {
                events.emplace_back(trace("copy.position:", {value.x, value.y, value.z}));
                if (boundary && value.x >= 256)
                    copied->start_crossing_check();
                else
                    cp = value;
            };
            result.set_rotation = [&](Quaternion value) {
                events.emplace_back(trace("copy.rotation:", {value.x, value.y, value.z, value.w}));
                cr = value;
            };
            result.set_velocity = [&](Vector3 value) {
                events.emplace_back(trace("copy.velocity:", {value.x, value.y, value.z}));
                cv = value;
            };
            result.set_angular_velocity = [&](Vector3 value) {
                events.emplace_back(trace("copy.angular:", {value.x, value.y, value.z}));
                ca = value;
            };
            result.timer_add = [&] { events.emplace_back("copy.timer_add"); };
            result.timer_remove = [&] { events.emplace_back("copy.timer_remove"); };
            result.terse = [&] { events.emplace_back("copy.terse"); };
            result.moving_start = [&] { events.emplace_back("copy.moving_start"); };
            result.moving_end = [&] { events.emplace_back("copy.moving_end"); };
            return result;
        };
        const auto snapshot = [&](std::string error) {
            return Value::object({{"original", state_snapshot(motion, p, r, v, a)},
                                  {"copied", copied ? state_snapshot(*copied, cp, cr, cv, ca) : Value{}},
                                  {"events", array(events)},
                                  {"error", Value(std::move(error))}});
        };
        std::vector<Value> output{snapshot("")};
        for (const auto &action : input.get("actions").array) {
            events.clear();
            std::string error;
            const auto &op = action.get("op").text;
            try {
                const bool use_copy = action.get("target").text == "copy";
                if (use_copy && !copied)
                    throw std::bad_optional_access();
                auto &target = use_copy ? *copied : motion;
                auto &tp = use_copy ? cp : p;
                auto &tr = use_copy ? cr : r;
                auto &tv = use_copy ? cv : v;
                if (op == "copy") {
                    cp = vector(action.get("position"));
                    cr = action.get("rotation").array.empty() ? r : rotation(action.get("rotation"));
                    cv = v;
                    ca = a;
                    copied = motion.copy(copy_host());
                } else if (op == "serialize") {
                    target.serialize();
                } else if (op == "positionError") {
                    position_error = action.get("value").boolean;
                } else if (op == "restore") {
                    cp = vector(action.get("position"));
                    cr = r;
                    cv = v;
                    ca = a;
                    auto bytes = motion.serialize();
                    if (action.get("alias").boolean) {
                        auto decoded = decode_keyframed_motion(bytes).value();
                        decoded.playback.keyframes = decoded.backing_frames;
                        decoded.keyframes_share_backing = true;
                        bytes = encode_keyframed_motion(decoded);
                    }
                    copied = KeyframedMotion::from_data(copy_host(), bytes, true, action.get("selected").boolean);
                } else if (op == "start")
                    target.start();
                else if (op == "stop")
                    target.stop();
                else if (op == "pause")
                    target.pause();
                else if (op == "suspend")
                    target.suspend();
                else if (op == "resume")
                    target.resume();
                else if (op == "erase")
                    target.erase();
                else if (op == "selected")
                    target.selected(action.get("value").boolean);
                else if (op == "update")
                    target.update_scene_object(!action.get("null").boolean);
                else if (op == "cross")
                    target.start_crossing_check();
                else if (op == "fail")
                    target.crossing_failure();
                else if (op == "boundary")
                    boundary = action.get("value").boolean;
                else if (op == "external") {
                    if (!action.get("position").array.empty())
                        tp = vector(action.get("position"));
                    if (!action.get("rotation").array.empty())
                        tr = rotation(action.get("rotation"));
                    if (!action.get("velocity").array.empty())
                        tv = vector(action.get("velocity"));
                } else if (op == "set") {
                    if (action.get("null").boolean)
                        target.set_keyframes(std::nullopt);
                    else
                        target.set_keyframes(frames(action.get("frames")));
                } else if (op == "tick") {
                    const int count =
                        action.get("repeat").kind == Value::Kind::Undefined ? 1 : action.get("repeat").integer;
                    const auto dt = action.get("dt").kind == Value::Kind::Undefined ? 50. : number(action.get("dt"));
                    const auto duration =
                        action.get("duration").kind == Value::Kind::Undefined ? 50. : number(action.get("duration"));
                    for (int i = 0; i < count; ++i) {
                        now += dt;
                        target.tick(duration);
                    }
                }
            } catch (const std::bad_optional_access &) {
                error = "NullReferenceException";
            } catch (const std::exception &) {
                error = "InvalidOperationException";
            }
            output.push_back(snapshot(error));
        }
        std::cout << network::osd_json(array(std::move(output)), true) << '\n';
    }
}
