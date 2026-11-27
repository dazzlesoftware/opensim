// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/keyframed_motion_command.hpp"
#include "opensim/script/primitive_parameter_query.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/text.hpp"
#include <cmath>
#include <limits>

namespace opensim::script {
namespace {
const LslValue &at(const LslList &list, std::size_t index) {
    if (index >= list.items.size())
        throw KeyframedMotionCommandError(KeyframedMotionCommandError::Kind::Index,
                                          "Index was outside the bounds of the array.");
    return list.items[index];
}

std::string type_name(const LslValue &value) {
    static constexpr const char *names[]{"LSLInteger", "LSLString",  "LSLFloat", "Vector3",   "Quaternion",
                                         "list",       "LSLInteger", "LSLFloat", "LSLString", "key"};
    return names[value.index()];
}

[[noreturn]] void invalid(const char *expected, const LslValue &value) {
    throw KeyframedMotionCommandError(KeyframedMotionCommandError::Kind::InvalidCast,
                                      std::string(expected) + " expected but " + type_name(value) + " given");
}

std::int32_t integer(const LslValue &input) {
    if (std::holds_alternative<LslBoxedKey>(input))
        invalid("LSLInteger", input);
    const auto value = unbox_lsl_value(input);
    if (value.index() > 2)
        invalid("LSLInteger", value);
    return primitive_rule_integer(value);
}

float strict_float(const LslValue &input) {
    if (std::holds_alternative<LslBoxedKey>(input))
        invalid("LSL float", input);
    const auto value = unbox_lsl_value(input);
    if (const auto number = std::get_if<double>(&value))
        return static_cast<float>(*number);
    if (const auto number = std::get_if<std::int32_t>(&value))
        return static_cast<float>(*number);
    if (const auto text = std::get_if<std::string>(&value))
        return framework::invariant_convert_to_single(framework::utf16(*text));
    invalid("LSL float", value);
}

framework::Vector3 vector(const LslValue &value) {
    if (const auto item = std::get_if<framework::Vector3d>(&value))
        return {static_cast<float>(item->x), static_cast<float>(item->y), static_cast<float>(item->z)};
    invalid("Vector3", value);
}

framework::Quaternion rotation(const LslValue &value) {
    const auto item = std::get_if<LslRotation>(&value);
    if (!item)
        invalid("Quaternion", value);
    auto q = *item;
    const double squared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (squared < static_cast<double>(std::numeric_limits<float>::denorm_min()))
        q = {0, 0, 0, 1};
    else {
        const double inverse = 1.0 / std::sqrt(squared);
        q.x *= inverse;
        q.y *= inverse;
        q.z *= inverse;
        q.w *= inverse;
    }
    return {static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w)};
}

std::int32_t duration(float seconds) {
    const float ms = seconds * 1000.f;
    if (!std::isfinite(ms) || ms < -2147483648.f || ms >= 2147483648.f)
        return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(ms);
}
} // namespace

void set_keyframed_motion(const LslList &frames, const LslList &options, const KeyframedMotionCommandHost &host) {
    if (host.physical || host.attachment)
        return;
    if (!frames.items.empty()) {
        if (host.has_motion())
            host.destroy();
        host.clear();
        auto mode = KeyframedMotionMode::Forward;
        std::int32_t data = 3;
        std::size_t index = 0;
        while (index < options.items.size()) {
            const auto option = integer(at(options, index++));
            if ((option == 1 || option == 2) && index < options.items.size()) {
                const auto value = integer(at(options, index++));
                if (option == 2)
                    data = value;
                else {
                    switch (value) {
                    case 0:
                        mode = KeyframedMotionMode::Forward;
                        break;
                    case 3:
                        mode = KeyframedMotionMode::Reverse;
                        break;
                    case 1:
                        mode = KeyframedMotionMode::Loop;
                        break;
                    case 2:
                        mode = KeyframedMotionMode::PingPong;
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        host.create(mode, data);
        const std::size_t element_length = data == 3 ? 3 : 2;
        const bool translation = (data & 2) != 0, rotate = (data & 1) != 0;
        index = 0;
        std::vector<KeyframedMotionFrame> parsed;
        while (index < frames.items.size()) {
            if (frames.items.size() - index < element_length)
                break;
            KeyframedMotionFrame frame;
            if (translation)
                frame.position = vector(at(frames, index++));
            if (rotate)
                frame.rotation = rotation(at(frames, index++));
            frame.time_ms = duration(strict_float(at(frames, index++)));
            parsed.push_back(frame);
        }
        host.set_keyframes(std::move(parsed));
        host.start();
        return;
    }
    if (!host.has_motion())
        return;
    if (options.items.empty()) {
        host.stop();
        return;
    }
    std::size_t index = 0;
    while (index < options.items.size()) {
        const auto option = integer(at(options, index++));
        if (option == 0) {
            switch (integer(at(options, index++))) {
            case 0:
                host.start();
                break;
            case 1:
                host.stop();
                break;
            case 2:
                host.pause();
                break;
            default:
                break;
            }
        }
    }
}
} // namespace opensim::script
