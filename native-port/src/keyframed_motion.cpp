// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/keyframed_motion.hpp"
#include "opensim/scene/keyframed_motion_nrbf.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <bit>
#include <cmath>
#include <limits>

namespace opensim::scene {
namespace {
using namespace framework;
using Mode = script::KeyframedMotionMode;

Vector3 add(Vector3 a, Vector3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vector3 subtract(Vector3 a, Vector3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vector3 scale(Vector3 a, float amount) {
    return {a.x * amount, a.y * amount, a.z * amount};
}

Vector3 divide(Vector3 a, float amount) {
    return scale(a, 1.f / amount);
}

Quaternion multiply(Quaternion a, Quaternion b) {
    if (std::abs(a.w) > .999999f)
        return b;
    if (std::abs(b.w) > .999999f)
        return a;
    return sdk_multiply_quaternion(a, b);
}

Quaternion normalized(Quaternion q) {
    const auto squared = sdk_quaternion_length_squared(q);
    if (!(squared > 1.e-6f))
        return {0, 0, 0, 1};
    const auto magnitude = std::sqrt(squared);
    if (sdk_sse41_supported())
        return {q.x / magnitude, q.y / magnitude, q.z / magnitude, q.w / magnitude};
    const auto inverse = 1.f / magnitude;
    return {q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse};
}

std::int32_t integer(double value) {
    if (!std::isfinite(value) || value < -2147483648. || value >= 2147483648.)
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}

std::int32_t plus(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}

std::int32_t minus(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b));
}

void offset(std::optional<Vector3> &position, Vector3 delta) {
    if (position)
        position = add(*position, delta);
}
} // namespace

KeyframedMotion::KeyframedMotion(KeyframedMotionHost host, Mode mode, std::int32_t data, bool has_group)
    : host_(std::move(host)), has_group_(has_group) {
    state_.mode = mode;
    state_.data = data;
    if (has_group_) {
        state_.base_position = host_.position();
        state_.base_rotation = host_.rotation();
    }
}

void KeyframedMotion::set_keyframes(std::optional<std::vector<KeyframedMotionFrame>> frames) {
    state_.keyframes = std::move(frames);
    state_.keyframes_share_backing = false;
}

std::vector<std::uint8_t> KeyframedMotion::serialize() {
    const bool was_stopped = state_.timer_stopped;
    stop_timer();
    const bool had_group = has_group_;
    has_group_ = false;
    if (!state_.selected && had_group)
        state_.serialized_position = host_.position();
    auto bytes = encode_keyframed_motion(
        {state_, state_.frame_backing, state_.frame_list_version, state_.keyframes_share_backing});
    // The original has no finally: a serialization failure leaves the group
    // detached and the timer stopped. Restore only after successful encoding.
    has_group_ = had_group;
    if (!was_stopped && state_.running && !state_.waiting_crossing)
        start_timer();
    return bytes;
}

std::unique_ptr<KeyframedMotion> KeyframedMotion::from_data(KeyframedMotionHost host,
                                                            std::span<const std::uint8_t> bytes, bool has_group,
                                                            bool group_selected) {
    try {
        auto decoded = decode_keyframed_motion(bytes);
        if (!decoded)
            return {};
        auto result =
            std::make_unique<KeyframedMotion>(std::move(host), decoded->playback.mode, decoded->playback.data, false);
        result->state_ = std::move(decoded->playback);
        result->has_group_ = has_group;
        result->state_.selected = has_group && group_selected;
        return result;
    } catch (...) {
        return {};
    }
}

void KeyframedMotion::clear_frames() {
    // Keyframe contains only value types: List<T>.Clear does not clear _items.
    state_.frame_list_version = plus(state_.frame_list_version, 1);
    state_.frames.clear();
}

void KeyframedMotion::append_frame(const KeyframedMotionFrame &frame) {
    if (state_.frames.size() == state_.frame_backing.size()) {
        state_.frame_backing.resize(state_.frame_backing.empty() ? 4 : state_.frame_backing.size() * 2);
        state_.keyframes_share_backing = false;
    }
    state_.frame_backing[state_.frames.size()] = frame;
    state_.frames.push_back(frame);
    if (state_.keyframes_share_backing)
        state_.keyframes = state_.frame_backing;
    state_.frame_list_version = plus(state_.frame_list_version, 1);
}

void KeyframedMotion::remove_first_frame() {
    state_.frames.erase(state_.frames.begin());
    std::copy(state_.frames.begin(), state_.frames.end(), state_.frame_backing.begin());
    if (state_.keyframes_share_backing)
        state_.keyframes = state_.frame_backing;
    state_.frame_list_version = plus(state_.frame_list_version, 1);
}

std::unique_ptr<KeyframedMotion> KeyframedMotion::copy(KeyframedMotionHost destination) {
    stop_timer();
    auto result = std::make_unique<KeyframedMotion>(std::move(destination), state_.mode, state_.data, false);
    result->has_group_ = true;
    auto &copied = result->state_;
    copied.keyframes = state_.keyframes;
    copied.frames = state_.frames;
    // List<T>(ICollection<T>) uses exact Count capacity and a fresh version.
    copied.frame_backing = state_.frames;
    copied.base_position = state_.base_position;
    copied.base_rotation = state_.base_rotation;
    copied.serialized_position = state_.selected ? state_.serialized_position
                                 : has_group_    ? host_.position()
                                                 : state_.serialized_position;
    copied.current = state_.current;
    copied.iterations = state_.iterations;
    copied.running = state_.running;
    if (state_.running && !state_.waiting_crossing)
        start_timer();
    return result;
}

void KeyframedMotion::start_timer() {
    host_.timer_add();
    state_.last_tick_ms = host_.now_ms();
    state_.timer_stopped = false;
}

void KeyframedMotion::stop_timer() {
    state_.timer_stopped = true;
}

void KeyframedMotion::start() {
    state_.crossing = state_.waiting_crossing = false;
    if (state_.keyframes && has_group_ && !state_.keyframes->empty()) {
        start_timer();
        state_.running = true;
        host_.moving_start();
    } else {
        stop_timer();
        state_.running = false;
    }
}

void KeyframedMotion::stop() {
    stop_timer();
    const bool running = state_.running;
    state_.running = state_.crossing = state_.waiting_crossing = false;
    state_.base_position = host_.position();
    state_.base_rotation = host_.rotation();
    host_.set_velocity({});
    host_.set_angular_velocity({});
    host_.terse();
    clear_frames();
    if (running)
        host_.moving_end();
}

void KeyframedMotion::pause() {
    stop_timer();
    const bool running = state_.running;
    state_.running = false;
    host_.set_velocity({});
    host_.set_angular_velocity({});
    host_.terse();
    if (running)
        host_.moving_end();
}

void KeyframedMotion::suspend() {
    if (!state_.timer_stopped)
        state_.timer_stopped = true;
}

void KeyframedMotion::resume() {
    if (state_.timer_stopped && state_.running && !state_.waiting_crossing)
        start_timer();
}

void KeyframedMotion::erase() {
    state_.running = false;
    stop_timer();
    state_.crossing = state_.waiting_crossing = false;
    clear_frames();
    state_.keyframes.reset();
}

void KeyframedMotion::selected(bool value) {
    if (has_group_) {
        if (!value && state_.selected)
            update_scene_object();
        else if (value && !state_.selected) {
            stop_timer();
            state_.serialized_position = host_.position();
        }
    }
    state_.crossing = state_.waiting_crossing = false;
    state_.selected = value;
}

void KeyframedMotion::update_scene_object(bool has_group) {
    state_.crossing = state_.waiting_crossing = false;
    stop_timer();
    if (!has_group)
        return;
    has_group_ = true;
    const auto position = host_.position();
    const auto delta = subtract(position, state_.serialized_position);
    state_.serialized_position = position;
    state_.base_position = add(state_.base_position, delta);
    offset(state_.current.position, delta);
    state_.next_position = add(state_.next_position, delta);
    for (std::size_t i = 0; i < state_.frames.size(); ++i) {
        offset(state_.frames[i].position, delta);
        state_.frame_backing[i] = state_.frames[i];
        state_.frame_list_version = plus(state_.frame_list_version, 1);
    }
    if (state_.keyframes_share_backing)
        state_.keyframes = state_.frame_backing;
    if (state_.running)
        start();
}

void KeyframedMotion::next_list() {
    clear_frames();
    auto position = state_.base_position;
    auto rotation = state_.base_rotation;
    if (state_.mode != Mode::Loop && state_.mode != Mode::PingPong && state_.iterations != 0)
        return;
    const bool reverse = state_.mode == Mode::Reverse || (state_.mode == Mode::PingPong && (state_.iterations & 1));
    const auto count = state_.keyframes.value().size();
    for (std::size_t step = 0; step < count; ++step) {
        auto frame = (*state_.keyframes)[reverse ? count - 1 - step : step];
        frame.start_position = position;
        frame.position = frame.position ? add(scale(*frame.position, reverse ? -1.f : 1.f), position) : position;
        frame.start_rotation = rotation;
        if (frame.rotation) {
            auto relative = *frame.rotation;
            if (reverse)
                relative = {-relative.x, -relative.y, -relative.z, relative.w};
            frame.rotation = multiply(rotation, relative);
        } else
            frame.rotation = rotation;
        frame.time_total = frame.time_ms;
        append_frame(frame);
        position = *frame.position;
        rotation = *frame.rotation;
    }
    state_.base_position = position;
    state_.base_rotation = rotation;
    state_.iterations = plus(state_.iterations, 1);
}

void KeyframedMotion::done() {
    host_.timer_remove();
    state_.timer_stopped = true;
    state_.running = state_.crossing = state_.waiting_crossing = false;
    state_.base_position = host_.position();
    state_.base_rotation = host_.rotation();
    host_.set_velocity({});
    host_.set_angular_velocity({});
    host_.terse();
    clear_frames();
}

void KeyframedMotion::tick(double duration_ms) {
    if (state_.timer_stopped)
        host_.timer_remove();
    else
        advance(duration_ms);
}

void KeyframedMotion::advance(double duration_ms) {
    if (state_.skip_loops > 0) {
        --state_.skip_loops;
        return;
    }
    if (!has_group_)
        return;
    if (state_.selected) {
        if (host_.velocity() != Vector3{}) {
            host_.set_velocity({});
            host_.terse();
        }
        return;
    }
    if (state_.crossing) {
        state_.crossing = false;
        host_.set_position(state_.next_position);
        if (!state_.crossing) {
            stop_timer();
            start_timer();
        }
        return;
    }
    const auto now = host_.now_ms();
    if (state_.frames.empty()) {
        next_list();
        if (state_.frames.empty()) {
            done();
            host_.moving_end();
            return;
        }
        state_.current = state_.frames.front();
        state_.next_position = host_.position();
        state_.current_velocity =
            divide(subtract(*state_.current.position, state_.next_position), state_.current.time_ms * .001f);
        state_.current.time_ms = plus(state_.current.time_ms, integer(duration_ms));
        state_.last_tick_ms = now - 50.f;
    }
    auto elapsed = integer(now - state_.last_tick_ms);
    if (elapsed > 3 * duration_ms)
        elapsed = integer(duration_ms);
    state_.current.time_ms = minus(state_.current.time_ms, elapsed);
    state_.last_tick_ms = now;
    const double remaining = static_cast<double>(state_.current.time_ms) / duration_ms;
    if (remaining <= 1.0) {
        host_.set_velocity({});
        host_.set_angular_velocity({});
        state_.next_position = *state_.current.position;
        host_.set_position(state_.next_position);
        host_.set_rotation(*state_.current.rotation);
        remove_first_frame();
        if (!state_.frames.empty()) {
            state_.current = state_.frames.front();
            state_.current_velocity =
                divide(subtract(*state_.current.position, state_.next_position), state_.current.time_ms * .001f);
            host_.set_velocity(state_.current_velocity);
            state_.current.time_ms = plus(state_.current.time_ms, integer(duration_ms));
        } else
            host_.set_velocity({});
    } else {
        const auto current = host_.position();
        const auto motion = divide(subtract(*state_.current.position, current), static_cast<float>(remaining));
        state_.next_position = add(current, motion);
        if (*state_.current.rotation != host_.rotation()) {
            const float completed =
                (static_cast<float>(state_.current.time_total) - static_cast<float>(state_.current.time_ms)) /
                static_cast<float>(state_.current.time_total);
            host_.set_rotation(
                normalized(sdk_slerp_quaternion(state_.current.start_rotation, *state_.current.rotation, completed)));
        }
        host_.set_position(state_.next_position);
        host_.set_velocity(state_.current_velocity);
    }
    host_.terse();
}

void KeyframedMotion::start_crossing_check() {
    stop_timer();
    state_.crossing = state_.waiting_crossing = true;
    if (host_.velocity() != Vector3{}) {
        host_.set_velocity({});
        host_.terse();
    }
}

void KeyframedMotion::crossing_failure() {
    state_.waiting_crossing = false;
    if (has_group_) {
        host_.set_velocity({});
        host_.terse();
        if (state_.running) {
            stop_timer();
            state_.skip_loops = 1200;
            start_timer();
        }
    }
}
} // namespace opensim::scene
