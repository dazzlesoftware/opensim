// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/keyframed_motion_command.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_json.hpp"
#include <bit>
#include <iostream>
using namespace opensim;

namespace {
network::Value array(std::vector<network::Value> items) {
    network::Value value;
    value.kind = network::Value::Kind::Array;
    value.array = std::move(items);
    return value;
}

double number(const network::Value &v) {
    return v.kind == network::Value::Kind::Real ? v.real : v.integer;
}

script::LslList list(const network::Value &input) {
    script::LslList result;
    for (const auto &item : input.array) {
        const auto &type = item.array.at(0).text;
        const auto &value = item.array.at(1);
        if (type == "i")
            result.items.emplace_back(static_cast<std::int32_t>(number(value)));
        else if (type == "f")
            result.items.emplace_back(number(value));
        else if (type == "s")
            result.items.emplace_back(value.text);
        else if (type == "v")
            result.items.emplace_back(
                framework::Vector3d{number(value.array.at(0)), number(value.array.at(1)), number(value.array.at(2))});
        else if (type == "q")
            result.items.emplace_back(script::LslRotation{number(value.array.at(0)), number(value.array.at(1)),
                                                          number(value.array.at(2)), number(value.array.at(3))});
        else
            result.items.emplace_back(std::make_shared<const script::LslList>());
    }
    return result;
}

network::Value bits(std::initializer_list<float> values) {
    std::vector<network::Value> out;
    for (auto value : values)
        out.emplace_back(std::bit_cast<std::int32_t>(value));
    return array(std::move(out));
}
} // namespace

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto input = network::parse_osd_json(line);
        bool present = input.get("existing").boolean, frames_set = false;
        std::int32_t mode = -1, data = -1;
        std::vector<network::Value> events;
        std::vector<script::KeyframedMotionFrame> frames;
        const auto event = [&](const char *name) {
            events.emplace_back(name);
            if (input.get("throws").text == name)
                throw std::runtime_error(std::string("controlled ") + name);
        };
        script::KeyframedMotionCommandHost host;
        host.physical = input.get("physical").boolean;
        host.attachment = input.get("attachment").boolean;
        host.has_motion = [&] { return present; };
        host.destroy = [&] { event("delete"); };
        host.clear = [&] {
            event("clear");
            present = false;
            mode = data = -1;
            frames_set = false;
            frames.clear();
        };
        host.create = [&](auto m, auto d) {
            event("create");
            present = true;
            mode = static_cast<std::int32_t>(m);
            data = d;
        };
        host.set_keyframes = [&](auto parsed) {
            event("set");
            frames = std::move(parsed);
            frames_set = true;
        };
        host.start = [&] { event("start"); };
        host.stop = [&] { event("stop"); };
        host.pause = [&] { event("pause"); };
        std::string error, message;
        try {
            script::set_keyframed_motion(list(input.get("frames")), list(input.get("options")), host);
        } catch (const script::KeyframedMotionCommandError &failure) {
            error = failure.kind == script::KeyframedMotionCommandError::Kind::Index ? "IndexOutOfRangeException"
                                                                                     : "InvalidCastException";
            message = failure.what();
        } catch (const framework::InvariantNumberError &failure) {
            error =
                failure.kind == framework::InvariantNumberError::Kind::Format ? "FormatException" : "OverflowException";
            message = framework::utf8(failure.message);
        } catch (const std::exception &failure) {
            error = "InvalidOperationException";
            message = failure.what();
        }
        std::vector<network::Value> output_frames;
        for (const auto &frame : frames) {
            output_frames.push_back(network::Value::object(
                {{"p", frame.position ? bits({frame.position->x, frame.position->y, frame.position->z}) : bits({})},
                 {"r", frame.rotation
                           ? bits({frame.rotation->x, frame.rotation->y, frame.rotation->z, frame.rotation->w})
                           : bits({})},
                 {"ms", network::Value(frame.time_ms)}}));
        }
        std::cout << network::osd_json(network::Value::object({{"present", network::Value(present)},
                                                               {"mode", network::Value(mode)},
                                                               {"data", network::Value(data)},
                                                               {"framesSet", network::Value(frames_set)},
                                                               {"frames", array(std::move(output_frames))},
                                                               {"events", array(std::move(events))},
                                                               {"error", network::Value(error)},
                                                               {"message", network::Value(message)}}),
                                       true)
                  << '\n';
    }
}
