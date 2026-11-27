// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/keyframed_motion_command.hpp"
#include <span>

namespace opensim::scene {
struct KeyframedMotionFrame : script::KeyframedMotionFrame {
    framework::Vector3 start_position, angular_velocity;
    framework::Quaternion start_rotation{0, 0, 0, 0};
    std::int32_t time_total = 0;
};

struct KeyframedMotionState {
    framework::Vector3 serialized_position, base_position, next_position, current_velocity;
    framework::Quaternion base_rotation{0, 0, 0, 0};
    KeyframedMotionFrame current;
    std::vector<KeyframedMotionFrame> frames;
    // Preserve List<Keyframe> storage, including stale unused value entries.
    std::vector<KeyframedMotionFrame> frame_backing;
    std::int32_t frame_list_version = 0;
    std::optional<std::vector<KeyframedMotionFrame>> keyframes;
    bool keyframes_share_backing = false;
    script::KeyframedMotionMode mode = script::KeyframedMotionMode::Forward;
    std::int32_t data = 3, iterations = 0, skip_loops = 0;
    bool timer_stopped = true, crossing = false, waiting_crossing = false, running = false, selected = false;
    double last_tick_ms = 0;
};

struct KeyframedMotionHost {
    std::function<framework::Vector3()> position, velocity;
    std::function<framework::Quaternion()> rotation;
    std::function<void(framework::Vector3)> set_position, set_velocity, set_angular_velocity;
    std::function<void(framework::Quaternion)> set_rotation;
    std::function<double()> now_ms;
    std::function<void()> timer_add, timer_remove, terse, moving_start, moving_end;
};

// Serialized caller/owner-thread playback kernel. Live timer locking, scene
// lifecycle and API dispatch are separate bindings.
class KeyframedMotion {
  public:
    KeyframedMotion(KeyframedMotionHost, script::KeyframedMotionMode, std::int32_t data = 3, bool has_group = true);
    KeyframedMotion(const KeyframedMotion &) = delete;
    KeyframedMotion &operator=(const KeyframedMotion &) = delete;
    std::unique_ptr<KeyframedMotion> copy(KeyframedMotionHost destination);
    std::vector<std::uint8_t> serialize();
    static std::unique_ptr<KeyframedMotion> from_data(KeyframedMotionHost, std::span<const std::uint8_t>,
                                                      bool has_group = true, bool group_selected = false);
    void set_keyframes(std::optional<std::vector<KeyframedMotionFrame>>);
    void start();
    void stop();
    void pause();
    void suspend();
    void resume();
    void erase();
    void selected(bool);
    void update_scene_object(bool has_group = true);
    void start_crossing_check();
    void crossing_failure();
    void tick(double duration_ms = 50.);

    const KeyframedMotionState &state() const {
        return state_;
    }

  private:
    void start_timer();
    void stop_timer();
    void next_list();
    void done();
    void advance(double);
    void clear_frames();
    void append_frame(const KeyframedMotionFrame &);
    void remove_first_frame();
    KeyframedMotionHost host_;
    KeyframedMotionState state_;
    bool has_group_;
};
} // namespace opensim::scene
