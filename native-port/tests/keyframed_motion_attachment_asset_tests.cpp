// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/scene/keyframed_motion_nrbf.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <stdexcept>
#include <pugixml.hpp>

using namespace opensim;
using namespace framework;
using namespace scene;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string write(const ObjectAssetPart &root, const ObjectAssetPart &child, bool crossing) {
    std::u16string text;
    XmlTextWriterIo io;
    io.encoding = [] { return std::shared_ptr<NativeXmlTextEncoding>{}; };
    io.write = [&](auto, auto value) {
        if (value)
            text.append(*value);
    };
    io.write_array = [&](auto data, int offset, int count) { text.append(data->data() + offset, count); };
    io.write_whole_array = [&](auto data) { text.append(data->data(), data->size()); };
    NativeXmlTextWriter writer(std::move(io));
    const std::span<const ObjectAssetPart> children(&child, 1);
    if (crossing)
        write_crossing_object_xml(writer, root, children);
    else
        write_object_asset_xml(writer, root, children);
    return utf8(text);
}

auto read(const std::string &xml) {
    return read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
}

Vector3 vector(pugi::xml_node node) {
    return {node.child("X").text().as_float(), node.child("Y").text().as_float(), node.child("Z").text().as_float()};
}

void scenario(bool crossing) {
    const Vector3 group_position{120, 130, 40}, attachment_position{.25f, .5f, .75f}, root_offset{2, 3, 4};
    ObjectAssetPart root;
    root.object = SceneObject::primitive(Uuid::random(), attachment_position);
    root.object.local_id = 100;
    root.object.parent_id = 900;
    root.object.is_attachment = true;
    root.object.shape.state = 5;
    root.object.attached_group_position = group_position;
    root.object.root_offset_position = root_offset;
    root.attachment_position = attachment_position;
    root.last_attachment_point = 5;
    KeyframedMotionSerializedState motion;
    motion.playback.base_position = motion.playback.serialized_position = group_position;
    motion.playback.running = true;
    KeyframedMotionFrame frame;
    frame.position = Vector3{4, 0, 0};
    frame.time_ms = 200;
    motion.playback.keyframes = std::vector{frame};
    root.object.keyframed_motion = encode_keyframed_motion(motion);
    ObjectAssetPart child;
    child.object = SceneObject::primitive(root.object.owner, {1, 2, 3});
    child.object.local_id = 101;
    child.object.parent_id = root.object.local_id;

    const auto xml = write(root, child, crossing);
    pugi::xml_document document;
    check(bool(document.load_string(xml.c_str())), "Attachment XML was invalid");
    const auto group = document.child("SceneObjectGroup");
    const auto part = crossing ? group.child("SceneObjectPart") : group.child("RootPart").child("SceneObjectPart");
    check(vector(part.child("GroupPosition")) == group_position &&
              vector(part.child("AttachedPos")) == attachment_position &&
              vector(part.child("OffsetPosition")) == root_offset,
          "Attachment group position overwrote normal attachment/root offsets");
    const auto restored = read(xml);
    check(restored && restored->linksets.front().parts.size() == 2, "Could not import original-shaped attachment XML");
    const auto &parts = restored->linksets.front().parts;
    check(parts[0].object.attached_group_position == group_position && parts[0].object.position == group_position &&
              parts[0].attachment_position == attachment_position && parts[0].object.parent_id == 900 &&
              parts[0].object.shape.state == 5 && !parts[0].object.is_attachment &&
              parts[0].object.root_offset_position == root_offset,
          "Attachment import lost group position or changed pre-admission ground rez semantics");
    check(parts[0].object.keyframed_motion == root.object.keyframed_motion,
          "Attachment XML changed the serialized motion origin");
    check(parts[1].object.position == child.object.position && !parts[1].object.attached_group_position,
          "Child offset was mistaken for an attachment root");

    // Attachment admission separately selects AttachedPos and binds the wearer.
    auto admitted = parts[0];
    admitted.object.is_attachment = true;
    admitted.object.position = admitted.attachment_position;
    const auto roundtrip = read(write(admitted, parts[1], crossing));
    check(roundtrip && roundtrip->linksets.front().parts[0].object.attached_group_position == group_position &&
              roundtrip->linksets.front().parts[0].attachment_position == attachment_position &&
              roundtrip->linksets.front().parts[0].object.root_offset_position == root_offset,
          "Attachment save/restore conflated group and attachment positions");
    // Shape metadata must retain the imported origin before wearer admission,
    // even if a caller has already selected the local attachment position.
    admitted.object.is_attachment = false;
    const auto unbound = read(write(admitted, parts[1], crossing));
    check(unbound && unbound->linksets.front().parts[0].object.attached_group_position == group_position,
          "Unbound attachment-shaped asset lost its imported group origin");

    // A dropped root can retain remembered attachment metadata, which must not
    // override its new ground position or make its children attachment roots.
    const Vector3 dropped_position{80, 90, 30};
    admitted.object.is_attachment = false;
    admitted.object.parent_id = 0;
    admitted.object.shape.state = 0;
    admitted.object.position = dropped_position;
    const auto dropped = read(write(admitted, parts[1], crossing));
    check(dropped && dropped->linksets.front().parts[0].object.position == dropped_position &&
              !dropped->linksets.front().parts[0].object.attached_group_position &&
              dropped->linksets.front().parts[0].attachment_position == attachment_position &&
              dropped->linksets.front().parts[0].object.root_offset_position == root_offset,
          "Dropped attachment metadata replaced the ground position");
    admitted.object.parent_id = 900;
    const auto stale_parent = read(write(admitted, parts[1], crossing));
    check(stale_parent && !stale_parent->linksets.front().parts[0].object.attached_group_position,
          "Stale ParentID alone identified an attachment asset");

    admitted.object.parent_id = 0;
    admitted.object.shape.pcode = 255;
    admitted.object.shape.state = 5;
    const auto non_prim = read(write(admitted, parts[1], crossing));
    check(non_prim && !non_prim->linksets.front().parts[0].object.attached_group_position,
          "Non-primitive shape state identified an attachment asset");
}
} // namespace

int main() {
    try {
        scenario(false);
        scenario(true);
        std::cout << "PASS: attachment group position, offset and motion XML persistence; ground/child isolation\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
