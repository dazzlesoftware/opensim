// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/scene/keyframed_motion_nrbf.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <pugixml.hpp>
using namespace opensim;

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string write(const scene::ObjectAssetPart &part, bool crossing) {
    std::u16string text;
    framework::XmlTextWriterIo io;
    io.encoding = [] { return std::shared_ptr<framework::NativeXmlTextEncoding>{}; };
    io.write = [&](auto, auto value) {
        if (value)
            text.append(*value);
    };
    io.write_array = [&](auto data, int offset, int count) { text.append(data->data() + offset, count); };
    io.write_whole_array = [&](auto data) { text.append(data->data(), data->size()); };
    framework::NativeXmlTextWriter writer(std::move(io));
    if (crossing)
        scene::write_crossing_object_xml(writer, part, {});
    else
        scene::write_object_asset_xml(writer, part, {});
    return framework::utf8(text);
}

auto read(const std::string &text) {
    return scene::read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}
} // namespace

int main() {
    try {
        scene::ObjectAssetPart part;
        part.object = scene::SceneObject::primitive(framework::Uuid::random(), {30, 30, 30});
        part.object.rez_start_string = "motion";
        scene::KeyframedMotionSerializedState state;
        state.playback.serialized_position = state.playback.base_position = part.object.position;
        state.playback.base_rotation = {0, 0, 0, 1};
        state.playback.running = true;
        scene::KeyframedMotionFrame frame;
        frame.position = framework::Vector3{4, 0, 0};
        frame.time_ms = 200;
        state.playback.keyframes = std::vector{frame};
        part.object.keyframed_motion = scene::encode_keyframed_motion(state);
        for (bool crossing : {false, true}) {
            const auto xml = write(part, crossing);
            pugi::xml_document document;
            check(bool(document.load_string(xml.c_str())), "Generated motion XML invalid");
            const auto group = document.child("SceneObjectGroup");
            check(std::string_view(group.child("OtherParts").next_sibling().name()) == "KeyframeMotion",
                  "Motion element is not immediately after OtherParts");
            const auto restored = read(xml);
            check(restored &&
                      restored->linksets.front().parts.front().object.keyframed_motion == part.object.keyframed_motion,
                  "Object XML did not retain original NRBF motion bytes");
            group.child("KeyframeMotion").text().set("*");
            std::ostringstream bad;
            document.print(bad);
            check(!read(bad.str()), "Invalid motion base64 did not reject object XML");
            group.child("KeyframeMotion").text().set("AQID");
            std::ostringstream malformed;
            document.print(malformed);
            const auto ignored = read(malformed.str());
            check(ignored && ignored->linksets.front().parts.front().object.keyframed_motion.empty(),
                  "Invalid NRBF did not become an absent motion");
        }
        part.object.keyframed_motion.clear();
        check(write(part, false).find("KeyframeMotion") == std::string::npos, "Absent motion generated an XML element");
        std::cout << "PASS: original/crossing XML motion bytes, placement and invalid input handling\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
