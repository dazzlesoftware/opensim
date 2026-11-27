// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/scene/keyframed_motion_nrbf.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/sdk_utf8.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/script/yengine_xml_values.hpp"
#include "opensim/framework/vector_text.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include "opensim/framework/primitive_base_shape.hpp"
#include "opensim/network/osd_value.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <pugixml.hpp>

namespace opensim::scene {
namespace {
template <class Number> bool read_script_number(std::string_view text, Number &value) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return false;
    text = text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
    if (text.starts_with('+'))
        text.remove_prefix(1);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

bool read_script_rotation(std::string_view text, script::LslRotation &rotation) {
    rotation = std::get<script::LslRotation>(script::parse_yengine_xml_scalar(
        "OpenSim.Region.ScriptEngine.Shared.LSL_Types+Quaternion", framework::utf16_replacing(text)));
    return true;
}

std::optional<script::LslValue> read_event_argument(pugi::xml_node argument, unsigned depth = 0) {
    const std::string_view type = argument.attribute("type").value();
    if (type == "list") {
        if (depth >= 64)
            throw std::length_error("YEngine XML List depth exceeded");
        auto list = std::make_shared<script::LslList>();
        for (const auto item : argument.children("item"))
            list->items.push_back(*read_event_argument(item, depth + 1));
        return std::shared_ptr<const script::LslList>(std::move(list));
    }
    return script::parse_yengine_xml_scalar(type, framework::utf16_replacing(argument.child_value()));
}

std::optional<script::LslDetector> read_event_detector(pugi::xml_node element);

using XmlNode = std::shared_ptr<framework::NiniXmlDomNode>;
framework::NiniXmlDom load_xml_dom(std::span<const std::uint8_t>);

std::u16string xml_text(const framework::NiniWriterText &text) {
    return text ? *text : std::u16string{};
}

std::u16string xml_name(const XmlNode &node) {
    return xml_text(node->name());
}

std::u16string xml_attribute(const XmlNode &node, std::u16string_view name) {
    auto attribute = node->attribute(framework::nini_writer_text(std::u16string(name)));
    return attribute ? xml_text(attribute->value()) : std::u16string{};
}

std::u16string xml_inner_text(const XmlNode &node) {
    std::u16string result;
    const auto append = [&](const auto &self, const XmlNode &current) -> void {
        const auto type = current->node_type();
        if (type == 3 || type == 4 || type == 13 || type == 14)
            result += xml_text(current->value());
        else
            for (auto child = current->first_child(); child; child = child->next_sibling())
                self(self, child);
    };
    append(append, node);
    return result;
}

XmlNode xml_child(const XmlNode &node, std::u16string_view name) {
    for (auto child = node->first_child(); child; child = child->next_sibling())
        if (child->node_type() == 1 && xml_name(child) == name)
            return child;
    return {};
}

std::string xml_outer(const XmlNode &node) {
    auto sink = std::make_shared<framework::NiniStringWriter>();
    framework::NativeXmlTextWriter writer(framework::native_xml_string_writer_io(sink), true);
    framework::native_xml_node_write_to(node, writer);
    writer.close();
    return framework::utf8(*sink->to_string());
}

script::LslValue read_xml_argument(const XmlNode &node, std::size_t &entries, unsigned depth = 0) {
    if (!entries-- || depth >= 64)
        throw std::length_error("YEngine XML argument bounds exceeded");
    auto type = xml_attribute(node, u"type");
    if (type == u"list") {
        auto list = std::make_shared<script::LslList>();
        for (auto child = node->first_child(); child; child = child->next_sibling())
            if (child->node_type() == 1 && xml_name(child) == u"item")
                list->items.push_back(read_xml_argument(child, entries, depth + 1));
        return std::shared_ptr<const script::LslList>(std::move(list));
    }
    return script::parse_yengine_xml_scalar(framework::utf8(type), xml_inner_text(node));
}

std::vector<script::LslEvent> read_xml_events(const XmlNode &state) {
    std::vector<script::LslEvent> events;
    const auto queue = xml_child(state, u"EventQueue");
    if (!queue)
        return events;
    std::size_t entries = 65536;
    for (auto node = queue->first_child(); node; node = node->next_sibling()) {
        if (node->node_type() != 1 || xml_name(node) != u"Event")
            continue;
        script::LslEvent event{framework::utf8(xml_attribute(node, u"Name")), {}, {}};
        for (auto child = node->first_child(); child; child = child->next_sibling()) {
            if (child->node_type() != 1)
                continue;
            if (xml_name(child) == u"param")
                event.arguments.push_back(read_xml_argument(child, entries));
            else if (xml_name(child) == u"DetectParams") {
                // Detector numeric semantics are shared by the existing reader;
                // preserve the length-aware SDK name independently of pugixml.
                pugi::xml_document temp;
                const auto encoded = xml_outer(child);
                if (temp.load_buffer(encoded.data(), encoded.size()))
                    if (auto detector = read_event_detector(temp.document_element())) {
                        detector->sdk_name = xml_attribute(child, u"name");
                        detector->name = framework::utf8(*detector->sdk_name);
                        event.detectors.push_back(std::move(*detector));
                    }
            }
        }
        events.push_back(std::move(event));
    }
    return events;
}

std::optional<script::LslDetector> read_event_detector(pugi::xml_node element) {
    script::LslDetector detector;
    const auto vector = [&](const char *name, framework::Vector3d &value) {
        value = std::get<framework::Vector3d>(
            script::parse_yengine_xml_scalar("OpenSim.Region.ScriptEngine.Shared.LSL_Types+Vector3",
                                             framework::utf16_replacing(element.attribute(name).value())));
        return true;
    };
    if (!framework::try_parse_sdk_uuid(element.attribute("key").value(), detector.key) ||
        !framework::try_parse_sdk_uuid(element.attribute("owner").value(), detector.owner) ||
        !framework::try_parse_sdk_uuid(element.attribute("group").value(), detector.group) ||
        !read_script_number(element.attribute("type").value(), detector.type) ||
        !read_script_number(element.attribute("linkNum").value(), detector.link_number) ||
        !vector("pos", detector.grab) || !vector("position", detector.position) ||
        !vector("velocity", detector.velocity) ||
        !read_script_rotation(element.attribute("rotation").value(), detector.rotation))
        return std::nullopt;
    detector.name = element.attribute("name").value();
    return detector;
}

std::optional<script::LslEvent> read_queued_script_event(pugi::xml_node element) {
    script::LslEvent event{element.attribute("Name").value(), {}, {}};
    if (event.name.empty())
        return std::nullopt;
    for (const auto argument : element.children("param")) {
        auto value = read_event_argument(argument);
        if (!value)
            return std::nullopt;
        event.arguments.push_back(std::move(*value));
    }
    for (const auto detector : element.children("DetectParams"))
        if (auto value = read_event_detector(detector))
            event.detectors.push_back(std::move(*value));
    return event;
}
} // namespace

ManagedScriptState read_managed_script_state(const pugi::xml_node &state) {
    std::vector<pugi::xml_node> fields;
    for (const auto field : state.child("Plugins").children("plugin"))
        fields.push_back(field);
    const auto has_type = [](pugi::xml_node field, std::string_view type) {
        return field.attribute("type").value() == type;
    };
    const auto number = [](pugi::xml_node field, auto &value) {
        return read_script_number(field.child_value(), value);
    };

    ManagedScriptState saved;
    pugi::xml_document retained;
    auto retained_state = retained.append_copy(state);
    auto retained_engine = retained_state.attribute("Engine");
    if (!retained_engine)
        retained_engine = retained_state.append_attribute("Engine");
    retained_engine.set_value("YEngine");
    std::ostringstream original;
    retained_state.print(original, "", pugi::format_raw, pugi::encoding_utf8);
    saved.original_xml = original.str();
    const std::string_view running = state.child_value("Running");
    if (running == "True" || running == "true" || running == "1")
        saved.running = true;
    else if (running == "False" || running == "false" || running == "0")
        saved.running = false;
    saved.source_hash = state.attribute("SourceHash").value();
    const std::string_view snapshot = state.child_value("Snapshot");
    saved.has_snapshot_data = !snapshot.empty();
    if (saved.source_hash.size() == 32 && !snapshot.empty()) {
        network::Value binary;
        binary.kind = network::Value::Kind::Binary;
        binary.text = snapshot;
        try {
            saved.snapshot = script::decode_yengine_snapshot(*network::osd_binary(binary));
            if (saved.snapshot && saved.snapshot->event_code >= 0)
                for (const auto detector : state.child("DetectArray").children("DetectParams"))
                    if (auto value = read_event_detector(detector))
                        saved.snapshot->event_detectors.push_back(std::move(*value));
        } catch (const std::invalid_argument &) {
            // A malformed snapshot can still carry usable XML plugin state.
        }
    }
    double elapsed_ms{};
    if (number(state.child("scrpTime"), elapsed_ms)) {
        const auto elapsed_ticks = elapsed_ms * 10000.;
        if (elapsed_ticks >= 0. && elapsed_ticks < 9223372036854775808.)
            saved.elapsed_ticks = static_cast<std::int64_t>(elapsed_ticks);
    }
    double delay{};
    if (number(state.child("mEvtDly"), delay) && std::isfinite(delay))
        saved.minimum_event_delay = delay;
    for (const auto event : state.child("EventQueue").children("Event"))
        if (auto value = read_queued_script_event(event))
            saved.events.push_back(std::move(*value));

    std::set<std::int32_t> listener_handles;
    for (std::size_t offset = 0; fields.size() - offset >= 2;) {
        const auto label = fields[offset++];
        const auto size = fields[offset++];
        int count{};
        if (!has_type(label, "System.String") || !has_type(size, "System.Int32") || !number(size, count) || count < 0 ||
            static_cast<std::size_t>(count) > fields.size() - offset)
            break;
        const auto end = offset + count;
        const std::string_view plugin_name = label.child_value();
        if (plugin_name == "timer" && count == 2) {
            const auto interval = fields[offset];
            const auto remaining = fields[offset + 1];
            ManagedScriptState::Timer timer{};
            if (has_type(interval, "System.Int64") && has_type(remaining, "System.Int64") &&
                number(interval, timer.interval_ticks) && number(remaining, timer.remaining_ticks) &&
                timer.interval_ticks > 0)
                saved.timer = timer;
        } else if (plugin_name == "listener") {
            while (end - offset >= 6) {
                const auto *record = fields.data() + offset;
                const bool has_regex = end - offset >= 7 && has_type(record[6], "System.Int32");
                offset += has_regex ? 7 : 6;

                script::LslListenerCheckpoint listener{};
                const std::string_view active = record[0].child_value();
                if (!has_type(record[0], "System.Boolean") || (active != "True" && active != "False") ||
                    !has_type(record[1], "System.Int32") || !number(record[1], listener.handle) ||
                    listener.handle <= 0 || !has_type(record[2], "System.Int32") ||
                    !number(record[2], listener.channel) || !has_type(record[3], "System.String") ||
                    !has_type(record[4], "OpenMetaverse.UUID") ||
                    !framework::try_parse_sdk_uuid(record[4].child_value(), listener.sender) ||
                    !has_type(record[5], "System.String") || (has_regex && !number(record[6], listener.regex_flags)) ||
                    !listener_handles.insert(listener.handle).second)
                    continue;
                listener.active = active == "True";
                listener.name = record[3].child_value();
                listener.message = record[5].child_value();
                saved.listeners.push_back(std::move(listener));
            }
        } else if (plugin_name == "sensor" && count % 6 == 0) {
            for (; offset < end; offset += 6) {
                const auto *record = fields.data() + offset;
                script::LslSensorQuery sensor{};
                double interval{};
                float range{}, arc{};
                if (!has_type(record[0], "System.Double") || !number(record[0], interval) || !std::isfinite(interval) ||
                    !has_type(record[1], "System.String") || !has_type(record[2], "OpenMetaverse.UUID") ||
                    !framework::try_parse_sdk_uuid(record[2].child_value(), sensor.key) ||
                    !has_type(record[3], "System.Int32") || !number(record[3], sensor.type) ||
                    !has_type(record[4], "System.Single") ||
                    !framework::parse_invariant_float(record[4].child_value(), range) ||
                    !has_type(record[5], "System.Single") ||
                    !framework::parse_invariant_float(record[5].child_value(), arc))
                    continue;
                sensor.name = record[1].child_value();
                sensor.range = range;
                sensor.arc = arc;
                if (interval != 0.)
                    sensor.interval = interval;
                saved.sensors.push_back(std::move(sensor));
            }
        }
        offset = end;
    }
    return saved;
}

bool append_managed_lifecycle_events(ManagedScriptState &state, std::span<const script::LslEvent> events) {
    if (events.empty())
        return true;
    try {
        auto dom = load_xml_dom(
            {reinterpret_cast<const std::uint8_t *>(state.original_xml.data()), state.original_xml.size()});
        auto root = dom.document_element();
        if (!root || xml_name(root) != u"ScriptState")
            return false;
        auto queue = xml_child(root, u"EventQueue");
        if (!queue) {
            queue = dom.create_element(framework::nini_writer_text(u"EventQueue"));
            root->append_child(queue);
        }
        std::map<std::string, std::size_t> counts;
        for (auto node = queue->first_child(); node; node = node->next_sibling())
            if (node->node_type() == 1 && xml_name(node) == u"Event")
                ++counts[framework::utf8(xml_attribute(node, u"Name"))];
        auto appended = state.events;
        const auto attribute = [&](const XmlNode &node, std::u16string_view name, std::u16string value) {
            auto field = dom.create_attribute(framework::nini_writer_text(std::u16string(name)));
            field->value(framework::nini_writer_text(std::move(value)));
            node->append_attribute(field);
        };
        for (const auto &event : events) {
            if (counts[event.name] >= 64)
                continue;
            auto node = dom.create_element(framework::nini_writer_text(u"Event"));
            attribute(node, u"Name", framework::utf16(event.name));
            for (const auto &argument : event.arguments) {
                std::u16string type, text;
                if (const auto value = std::get_if<std::int32_t>(&argument)) {
                    type = u"System.Int32";
                    text = framework::utf16(std::to_string(*value));
                } else if (const auto value = std::get_if<script::LslBoxedInteger>(&argument)) {
                    type = u"OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLInteger";
                    text = framework::utf16(std::to_string(value->value));
                } else if (const auto value = std::get_if<std::string>(&argument)) {
                    type = u"System.String";
                    text = framework::utf16(*value);
                } else if (const auto value = std::get_if<script::LslBoxedString>(&argument)) {
                    type = u"OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString";
                    text = framework::utf16(value->value);
                } else if (const auto value = std::get_if<script::LslBoxedKey>(&argument)) {
                    type = u"OpenSim.Region.ScriptEngine.Shared.LSL_Types+key";
                    text = framework::utf16(value->value);
                } else
                    return false;
                auto param = dom.create_element(framework::nini_writer_text(u"param"));
                attribute(param, u"type", std::move(type));
                param->append_child(dom.create_text(framework::nini_writer_text(std::move(text))));
                node->append_child(param);
            }
            queue->append_child(node);
            ++counts[event.name];
            appended.push_back(event);
        }
        auto text = xml_outer(root);
        state.original_xml = std::move(text);
        state.events = std::move(appended);
        return true;
    } catch (...) {
        return false;
    }
}

namespace {
ManagedScriptState read_xml_managed_state(const XmlNode &node) {
    if (const auto engine = node->attribute(framework::nini_writer_text(u"Engine")))
        engine->value(framework::nini_writer_text(u"YEngine"));
    auto text = xml_outer(node);
    if (!node->attribute(framework::nini_writer_text(u"Engine")))
        text.insert(std::string("<ScriptState").size(), " Engine=\"YEngine\"");
    pugi::xml_document document;
    if (!document.load_buffer(text.data(), text.size()))
        throw std::invalid_argument("Invalid retained YEngine XML");
    // The legacy metadata reader must not interpret params through pugi's
    // null-terminated text interface before the length-aware DOM pass.
    document.document_element().remove_child("EventQueue");
    auto state = read_managed_script_state(document.document_element());
    state.events = read_xml_events(node);
    state.original_xml = text;
    return state;
}

framework::NiniXmlDom load_xml_dom(std::span<const std::uint8_t> bytes) {
    framework::NiniXmlDom document;
    auto reader =
        framework::NativeNiniXmlReader::from_bytes(bytes, framework::NiniXmlReaderSettings::legacy_document());
    framework::native_nini_xml_load(document, reader);
    reader->close();
    return document;
}
} // namespace

std::optional<ManagedScriptState> read_yengine_state_xml(std::span<const std::uint8_t> bytes, framework::Uuid asset) {
    if (bytes.empty())
        return {};
    pugi::xml_document document;
    if (!document.load_buffer(bytes.data(), bytes.size()))
        return {};
    const auto state = document.document_element();
    if (std::string_view(state.name()) != "ScriptState" ||
        std::string_view(state.attribute("Engine").value()) != "YEngine" ||
        std::string_view(state.attribute("SourceHash").value()).size() != 32)
        return {};
    const std::string_view saved_asset = state.attribute("Asset").value();
    if (!saved_asset.empty()) {
        framework::Uuid identity;
        if (!framework::try_parse_sdk_uuid(saved_asset, identity) || identity != asset)
            return {};
    }
    ManagedScriptState saved;
    try {
        auto dom = load_xml_dom(bytes);
        saved = read_xml_managed_state(dom.document_element());
    } catch (...) {
        return {};
    }
    if (!saved.running || !saved.has_snapshot_data)
        return {};
    const auto permissions = state.child("Permissions");
    ManagedScriptState::PermissionGrant grant{};
    if (framework::try_parse_sdk_uuid(permissions.attribute("granter").value(), grant.granter) &&
        read_script_number(permissions.attribute("mask").value(), grant.mask))
        saved.local_permission_grant = grant;
    return saved;
}

std::optional<ManagedScriptState> read_yengine_state_file(const std::filesystem::path &directory, framework::Uuid item,
                                                          framework::Uuid asset) {
    if (directory.empty() || item.is_zero() || asset.is_zero())
        return std::nullopt;
    const auto name = item.str() + ".state";
    auto path = directory / name.substr(0, 1) / name.substr(1);
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        if (error)
            return std::nullopt;
        path = directory / name;
    }
    if (!std::filesystem::is_regular_file(path, error))
        return std::nullopt;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto length = file.tellg();
    if (length <= 0 || static_cast<std::uintmax_t>(length) > std::string{}.max_size())
        return std::nullopt;
    std::string xml(static_cast<std::size_t>(length), '\0');
    file.seekg(0);
    if (!file.read(xml.data(), static_cast<std::streamsize>(xml.size())))
        return std::nullopt;

    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size()))
        return std::nullopt;
    const auto state = document.document_element();
    framework::Uuid saved_asset;
    if (std::string_view(state.name()) != "ScriptState" ||
        std::string_view(state.attribute("Engine").value()) != "YEngine" ||
        !framework::try_parse_sdk_uuid(state.attribute("Asset").value(), saved_asset) || saved_asset != asset ||
        std::string_view(state.attribute("SourceHash").value()).size() != 32)
        return std::nullopt;
    ManagedScriptState saved;
    try {
        auto dom = load_xml_dom({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
        saved = read_xml_managed_state(dom.document_element());
    } catch (...) {
        return {};
    }
    if (!saved.running)
        return std::nullopt;
    const auto permissions = state.child("Permissions");
    ManagedScriptState::PermissionGrant grant{};
    if (framework::try_parse_sdk_uuid(permissions.attribute("granter").value(), grant.granter) &&
        read_script_number(permissions.attribute("mask").value(), grant.mask))
        saved.local_permission_grant = grant;
    return saved;
}

namespace {
void start(framework::NativeXmlTextWriter &writer, const char16_t *name) {
    writer.write_start_element({}, std::u16string(name), {});
}

void element(framework::NativeXmlTextWriter &writer, const char16_t *name, const std::string &value) {
    start(writer, name);
    writer.write_string(framework::utf16(value));
    writer.write_end_element();
}

void binary_element(framework::NativeXmlTextWriter &writer, const char16_t *name, std::span<const std::uint8_t> data) {
    start(writer, name);
    writer.write_base64(data, 0, static_cast<std::int32_t>(data.size()));
    writer.write_end_element();
}

std::string read_scalar_text(pugi::xml_node field) {
    std::string text;
    for (const auto child : field.children()) {
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
            text += child.value();
    }
    return text;
}

bool read_binary_element(pugi::xml_node field, std::vector<std::uint8_t> &bytes,
                         std::size_t max_text_size = std::string::npos) {
    if (!field)
        return true;
    network::Value value;
    value.kind = network::Value::Kind::Binary;
    for (const auto child : field.children()) {
        if (child.type() == pugi::node_element)
            return false;
        if (child.type() != pugi::node_pcdata && child.type() != pugi::node_cdata)
            continue;
        const std::string_view text = child.value();
        if (text.size() > max_text_size - value.text.size())
            return false;
        value.text += text;
    }
    try {
        bytes = *network::osd_binary(value);
    } catch (const std::invalid_argument &) {
        return false;
    }
    return true;
}

void attribute(framework::NativeXmlTextWriter &writer, const char16_t *name, const std::string &value) {
    writer.write_start_attribute({}, std::u16string(name), {});
    writer.write_string(framework::utf16(value));
    writer.write_end_attribute();
}

void write_inventory_script_states(framework::NativeXmlTextWriter &writer, const ObjectAssetPart &root,
                                   std::span<const ObjectAssetPart> children) {
    bool started = false;
    const auto write_part_states = [&](const ObjectAssetPart &part) {
        for (const auto &item : part.inventory) {
            const auto checkpoint = part.native_script_checkpoints.find(item.item_id);
            if (checkpoint == part.native_script_checkpoints.end()) {
                const auto retained = part.managed_script_states.find(item.item_id);
                if (retained == part.managed_script_states.end() || retained->second.original_xml.empty())
                    continue;
                pugi::xml_document document;
                if (!document.load_buffer(retained->second.original_xml.data(), retained->second.original_xml.size()) ||
                    std::string_view(document.document_element().name()) != "ScriptState")
                    continue;
                if (!started) {
                    start(writer, u"GroupScriptStates");
                    started = true;
                }
                start(writer, u"SavedScriptState");
                attribute(writer, u"UUID", item.item_id.str());
                start(writer, u"State");
                attribute(writer, u"Engine", "YEngine");
                attribute(writer, u"UUID", item.item_id.str());
                attribute(writer, u"Asset", item.asset_id.str());
                writer.write_raw(framework::utf16(retained->second.original_xml));
                writer.write_end_element();
                writer.write_end_element();
                continue;
            }

            std::vector<std::uint8_t> encoded;
            try {
                encoded = script::encode_persistent_lsl_checkpoint(checkpoint->second);
            } catch (const std::length_error &) {
                // Preserve the running flag even when the checkpoint exceeds the format limit.
            }
            if (!started) {
                start(writer, u"GroupScriptStates");
                started = true;
            }
            start(writer, u"SavedScriptState");
            attribute(writer, u"UUID", item.item_id.str());
            start(writer, u"State");
            attribute(writer, u"Engine", "NativeLSL");
            attribute(writer, u"UUID", item.item_id.str());
            attribute(writer, u"Asset", item.asset_id.str());
            start(writer, u"ScriptState");
            element(writer, u"Running", checkpoint->second.running ? "True" : "False");
            if (!encoded.empty())
                binary_element(writer, u"NativeCheckpoint", encoded);
            writer.write_end_element();
            writer.write_end_element();
            writer.write_end_element();
        }
    };
    write_part_states(root);
    for (const auto &child : children)
        write_part_states(child);
    if (started)
        writer.write_end_element();
}

void write_object_asset_shape(framework::NativeXmlTextWriter &writer, const ObjectAssetPart &snapshot) {
    const auto number = [&](const char16_t *name, int value) { element(writer, name, std::to_string(value)); };
    const auto &part = snapshot.object;
    const auto &shape = part.shape;
    writer.write_start_element({}, std::u16string(u"Shape"), {});
    number(u"ProfileCurve", shape.profile_curve);
    binary_element(writer, u"TextureEntry", part.texture_entry);
    binary_element(writer, u"ExtraParams", part.extra_params);
    number(u"PathBegin", shape.path_begin);
    number(u"PathCurve", shape.path_curve);
    number(u"PathEnd", shape.path_end);
    number(u"PathRadiusOffset", shape.path_radius_offset);
    number(u"PathRevolutions", shape.path_revolutions);
    number(u"PathScaleX", shape.path_scale_x);
    number(u"PathScaleY", shape.path_scale_y);
    number(u"PathShearX", shape.path_shear_x);
    number(u"PathShearY", shape.path_shear_y);
    number(u"PathSkew", shape.path_skew);
    number(u"PathTaperX", shape.path_taper_x);
    number(u"PathTaperY", shape.path_taper_y);
    number(u"PathTwist", shape.path_twist);
    number(u"PathTwistBegin", shape.path_twist_begin);
    number(u"PCode", shape.pcode);
    number(u"ProfileBegin", shape.profile_begin);
    number(u"ProfileEnd", shape.profile_end);
    number(u"ProfileHollow", shape.profile_hollow);
    number(u"State", shape.state);
    number(u"LastAttachPoint", snapshot.last_attachment_point);
    number(u"ProfileShape", shape.profile_curve & 0x0f);
    number(u"HollowShape", shape.profile_curve & 0xf0);
    if (!part.face_media_xml.empty())
        element(writer, u"Media", part.face_media_xml);
    if (!part.material_overrides.empty())
        binary_element(writer, u"MatOvrd", part.material_overrides);
    writer.write_end_element();
}

void write_part(framework::NativeXmlTextWriter &writer, const ObjectAssetPart &snapshot, const SceneObject &root,
                std::size_t link_number) {
    const auto &part = snapshot.object;
    const auto number = [&](const char16_t *name, auto value) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{})
            throw std::runtime_error("Cannot format object asset number");
        element(writer, name, std::string(buffer, result.ptr));
    };
    const auto uuid = [&](const char16_t *name, framework::Uuid value) {
        start(writer, name);
        element(writer, u"UUID", value.str());
        writer.write_end_element();
    };
    const auto vector = [&](const char16_t *name, framework::Vector3 value) {
        start(writer, name);
        number(u"X", value.x);
        number(u"Y", value.y);
        number(u"Z", value.z);
        writer.write_end_element();
    };
    const auto quaternion = [&](const char16_t *name, framework::Quaternion value) {
        start(writer, name);
        number(u"X", value.x);
        number(u"Y", value.y);
        number(u"Z", value.z);
        number(u"W", value.w);
        writer.write_end_element();
    };
    start(writer, u"SceneObjectPart");
    uuid(u"CreatorID", part.creator);
    if (!part.creator_data.empty())
        element(writer, u"CreatorData", part.creator_data);
    number(u"InventorySerial", snapshot.inventory_serial);
    if (!snapshot.inventory.empty()) {
        start(writer, u"TaskInventory");
        for (const auto &item : snapshot.inventory) {
            start(writer, u"TaskInventoryItem");
            uuid(u"AssetID", item.asset_id);
            number(u"BasePermissions", item.base_permissions);
            number(u"CreationDate", item.creation_date);
            uuid(u"CreatorID", item.creator_id);
            if (item.creator_data && !item.creator_data->empty())
                element(writer, u"CreatorData", *item.creator_data);
            element(writer, u"Description", item.description.value_or(""));
            number(u"EveryonePermissions", item.everyone_permissions);
            number(u"Flags", item.flags);
            uuid(u"GroupID", item.group_id);
            number(u"GroupPermissions", item.group_permissions);
            number(u"InvType", item.inventory_type);
            uuid(u"ItemID", item.item_id);
            uuid(u"OldItemID", item.old_item_id);
            uuid(u"LastOwnerID", item.last_owner_id);
            element(writer, u"Name", item.name.value_or(""));
            number(u"NextPermissions", item.next_permissions);
            uuid(u"OwnerID", item.owner_id);
            number(u"CurrentPermissions", item.current_permissions);
            uuid(u"ParentID", item.parent_id);
            uuid(u"ParentPartID", item.parent_part_id);
            uuid(u"PermsGranter", item.permissions_granter);
            number(u"PermsMask", item.permissions_mask);
            number(u"Type", item.type);
            element(writer, u"OwnerChanged", item.owner_changed ? "true" : "false");
            writer.write_end_element();
        }
        writer.write_end_element();
    }
    uuid(u"UUID", part.id);
    number(u"LocalId", part.local_id);
    element(writer, u"Name", part.name);
    number(u"Material", part.material);
    element(writer, u"PassTouches", part.pass_touches ? "true" : "false");
    element(writer, u"PassCollisions", part.pass_collisions ? "true" : "false");
    // Native attachment position is its attachment offset. The managed group
    // position is independent and is also the keyframed motion origin.
    const bool attachment_root = root.is_attachment || (root.shape.pcode == 9 && root.shape.state != 0);
    vector(u"GroupPosition", attachment_root ? root.attached_group_position.value_or(root.position) : root.position);
    vector(u"OffsetPosition",
           part.id == root.id ? root.root_offset_position.value_or(framework::Vector3{}) : part.position);
    quaternion(u"RotationOffset", part.rotation);
    vector(u"Velocity", part.velocity);
    vector(u"AngularVelocity", part.angular_velocity);
    vector(u"Acceleration", part.acceleration);
    element(writer, u"Description", part.description);
    start(writer, u"Color");
    number(u"R", (part.text_argb >> 16) & 255u);
    number(u"G", (part.text_argb >> 8) & 255u);
    number(u"B", part.text_argb & 255u);
    number(u"A", part.text_argb >> 24);
    writer.write_end_element();
    element(writer, u"Text", part.floating_text);
    element(writer, u"SitName", part.sit_name);
    element(writer, u"TouchName", part.touch_name);
    number(u"LinkNum", link_number);
    number(u"ClickAction", part.click_action);
    write_object_asset_shape(writer, snapshot);
    vector(u"Scale", part.scale);
    quaternion(u"SitTargetOrientation", part.sit_target_orientation);
    vector(u"SitTargetPosition", part.sit_target_position);
    vector(u"SitTargetPositionLL", part.sit_target_position);
    if (std::abs(part.sit_active_range) > 1e-5f)
        number(u"SitActRange", part.sit_active_range);
    quaternion(u"SitTargetOrientationLL", part.sit_target_orientation);
    if (part.stand_target != framework::Vector3{})
        vector(u"StandTarget", part.stand_target);
    vector(u"CameraEyeOffset", part.camera_eye_offset);
    vector(u"CameraAtOffset", part.camera_at_offset);
    number(u"ParentID", part.parent_id);
    number(u"CreationDate", part.creation_date);
    number(u"Category", part.category);
    number(u"SalePrice", part.sale_price);
    number(u"ObjectSaleType", part.sale_type);
    uuid(u"GroupID", part.group);
    uuid(u"OwnerID", part.owner);
    uuid(u"LastOwnerID", part.last_owner);
    uuid(u"RezzerID", part.rezzer);
    number(u"BaseMask", part.base_mask);
    number(u"OwnerMask", part.owner_mask);
    number(u"GroupMask", part.group_mask);
    number(u"EveryoneMask", part.everyone_mask);
    number(u"NextOwnerMask", part.next_mask);
    number(u"ScriptAccessPin", part.script_access_pin);
    element(writer, u"AllowedDrop", (part.flags & 0x10000u) ? "true" : "false");
    number(u"Flags", part.flags & ~0x10000u);
    element(writer, u"VolumeDetectActive", (part.flags & prim_flag_volume_detect) ? "true" : "false");
    if (const auto &inertia = part.inertia_override) {
        start(writer, u"PhysicsInertia");
        number(u"MASS", inertia->mass);
        vector(u"CM", inertia->center);
        vector(u"INERTIA", inertia->diagonal);
        quaternion(u"IROT", inertia->rotation);
        writer.write_end_element();
    }
    if (part.id == root.id && part.rotation_axis_locks)
        number(u"RotationAxisLocks", part.rotation_axis_locks);
    uuid(u"CollisionSound", part.collision_sound);
    number(u"CollisionSoundVolume", part.collision_sound_volume);
    if (!part.media_url.empty())
        element(writer, u"MediaUrl", part.media_url);
    vector(u"AttachedPos", snapshot.attachment_position);
    constexpr std::array pay_names{u"PayPrice0", u"PayPrice1", u"PayPrice2", u"PayPrice3", u"PayPrice4"};
    for (std::size_t i = 0; i < pay_names.size(); ++i)
        number(pay_names[i], part.pay_prices[i]);
    number(u"Buoyancy", root.buoyancy);
    vector(u"Force", root.force);
    vector(u"Torque", root.torque);
    number(u"PhysicsShapeType", part.physics.shape);
    if (part.physics.density != 1000.f)
        number(u"Density", part.physics.density);
    if (part.physics.friction != .6f)
        number(u"Friction", part.physics.friction);
    if (part.physics.restitution != .5f)
        number(u"Bounce", part.physics.restitution);
    if (part.physics.gravity != 1.f)
        number(u"GravityModifier", part.physics.gravity);
    uuid(u"SoundID", part.looped_sound);
    number(u"SoundGain", part.looped_sound_gain);
    number(u"SoundFlags", part.sound_flags);
    number(u"SoundRadius", part.sound_radius);
    if (!part.texture_animation.empty())
        binary_element(writer, u"TextureAnimation", part.texture_animation);
    if (!part.particle_system.empty())
        binary_element(writer, u"ParticleSystem", part.particle_system);
    if (!part.dynamic_attributes_xml.empty()) {
        pugi::xml_document document;
        if (document.load_string(part.dynamic_attributes_xml.c_str()) && document.child("llsd")) {
            std::ostringstream xml;
            document.child("llsd").print(xml, "", pugi::format_raw);
            start(writer, u"DynAttrs");
            writer.write_raw(framework::utf16(xml.str()));
            writer.write_end_element();
        }
    }
    element(writer, u"SoundQueueing", part.sound_queueing ? "true" : "false");
    if (!part.object_animations.empty())
        binary_element(writer, u"SOPAnims", part.object_animations);
    writer.write_end_element();
}
} // namespace

void write_object_asset_xml(framework::NativeXmlTextWriter &writer, const ObjectAssetPart &root,
                            std::span<const ObjectAssetPart> children) {
    start(writer, u"SceneObjectGroup");
    start(writer, u"RootPart");
    write_part(writer, root, root.object, children.empty() ? 0 : 1);
    writer.write_end_element();
    start(writer, u"OtherParts");
    std::size_t link_number = 2;
    for (const auto &child : children) {
        start(writer, u"Part");
        write_part(writer, child, root.object, link_number++);
        writer.write_end_element();
    }
    writer.write_end_element();
    // The managed OriginalXml reader consumes linkset data before the rez string.
    if (!root.object.keyframed_motion.empty())
        binary_element(writer, u"KeyframeMotion", root.object.keyframed_motion);
    if (!root.object.linkset_data.empty()) {
        start(writer, u"lnkstdt");
        writer.write_base64(root.object.linkset_data, 0, static_cast<std::int32_t>(root.object.linkset_data.size()));
        writer.write_end_element();
    }
    if (root.object.rez_start_string)
        element(writer, u"StartStr", *root.object.rez_start_string);
    write_inventory_script_states(writer, root, children);
    writer.write_end_element();
}

void write_crossing_object_xml(framework::NativeXmlTextWriter &writer, const ObjectAssetPart &root,
                               std::span<const ObjectAssetPart> children) {
    start(writer, u"SceneObjectGroup");
    write_part(writer, root, root.object, children.empty() ? 0 : 1);
    start(writer, u"OtherParts");
    std::size_t link_number = 2;
    for (const auto &child : children)
        write_part(writer, child, root.object, link_number++);
    writer.write_end_element();
    if (!root.object.keyframed_motion.empty())
        binary_element(writer, u"KeyframeMotion", root.object.keyframed_motion);
    if (root.object.rez_start_string)
        element(writer, u"StartStr", *root.object.rez_start_string);
    if (!root.object.linkset_data.empty())
        binary_element(writer, u"lnkstdt", root.object.linkset_data);
    writer.write_end_element();
}

void write_coalesced_object_asset_xml(framework::NativeXmlTextWriter &writer,
                                      std::span<const ObjectAssetLinkset> linksets) {
    const auto limit = std::numeric_limits<float>::max();
    framework::Vector3 minimum{limit, limit, limit}, maximum{-limit, -limit, -limit};
    for (const auto &linkset : linksets) {
        const auto &root = linkset.parts.front().object;
        for (const auto &snapshot : linkset.parts) {
            const auto &part = snapshot.object;
            auto center = root.position;
            auto rotation = root.rotation;
            if (part.id != root.id) {
                const auto offset = framework::sdk_rotate_vector(part.position, root.rotation);
                center = {center.x + offset.x, center.y + offset.y, center.z + offset.z};
                rotation = framework::sdk_multiply_quaternion(root.rotation, part.rotation);
            }
            const auto x = framework::sdk_rotate_vector({part.scale.x * .5f, 0, 0}, rotation);
            const auto y = framework::sdk_rotate_vector({0, part.scale.y * .5f, 0}, rotation);
            const auto z = framework::sdk_rotate_vector({0, 0, part.scale.z * .5f}, rotation);
            const framework::Vector3 half_extent{std::abs(x.x) + std::abs(y.x) + std::abs(z.x),
                                                 std::abs(x.y) + std::abs(y.y) + std::abs(z.y),
                                                 std::abs(x.z) + std::abs(y.z) + std::abs(z.z)};
            minimum.x = std::min(minimum.x, center.x - half_extent.x);
            minimum.y = std::min(minimum.y, center.y - half_extent.y);
            minimum.z = std::min(minimum.z, center.z - half_extent.z);
            maximum.x = std::max(maximum.x, center.x + half_extent.x);
            maximum.y = std::max(maximum.y, center.y + half_extent.y);
            maximum.z = std::max(maximum.z, center.z + half_extent.z);
        }
    }
    const auto attribute = [&](const char16_t *name, float value) {
        char buffer[64];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{})
            throw std::runtime_error("Cannot format coalesced object asset bounds");
        writer.write_start_attribute({}, std::u16string(name), {});
        writer.write_string(framework::utf16(std::string(buffer, result.ptr)));
        writer.write_end_attribute();
    };
    start(writer, u"CoalescedObject");
    attribute(u"x", maximum.x - minimum.x);
    attribute(u"y", maximum.y - minimum.y);
    attribute(u"z", maximum.z - minimum.z);
    for (const auto &linkset : linksets) {
        const auto &root = linkset.parts.front();
        start(writer, u"SceneObjectGroup");
        attribute(u"offsetx", root.object.position.x - minimum.x);
        attribute(u"offsety", root.object.position.y - minimum.y);
        attribute(u"offsetz", root.object.position.z - minimum.z);
        write_object_asset_xml(writer, root, std::span(linkset.parts).subspan(1));
        writer.write_end_element();
    }
    writer.write_end_element();
}

namespace {
template <class T> void read_number(pugi::xml_node node, const char *name, T &value) {
    const std::string_view text = node.child_value(name);
    if (text.empty())
        return;
    T parsed{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error == std::errc{} && end == text.data() + text.size())
        value = parsed;
}

void read_uuid_field(pugi::xml_node field, framework::Uuid &value) {
    auto content = field;
    if (const auto wrapper = field.first_child(); wrapper.type() == pugi::node_element) {
        const std::string_view tag = wrapper.name();
        if (tag != "UUID" && tag != "Guid")
            return;
        content = wrapper;
    }
    std::string text;
    bool nested_element = false;
    for (const auto child : content.children()) {
        if (child.type() == pugi::node_element) {
            nested_element = true;
            break;
        }
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
            text += child.value();
    }
    if (!nested_element)
        framework::try_parse_sdk_uuid(text, value);
}

void read_uuid(pugi::xml_node node, const char *name, framework::Uuid &value) {
    for (const auto field : node.children(name))
        read_uuid_field(field, value);
}

void read_prim_flags(pugi::xml_node field, std::uint32_t &flags) {
    constexpr std::array<std::string_view, 32> names{"Physics",
                                                     "CreateSelected",
                                                     "ObjectModify",
                                                     "ObjectCopy",
                                                     "ObjectAnyOwner",
                                                     "ObjectYouOwner",
                                                     "Scripted",
                                                     "Touch",
                                                     "ObjectMove",
                                                     "Money",
                                                     "Phantom",
                                                     "InventoryEmpty",
                                                     "JointHinge",
                                                     "JointP2P",
                                                     "JointLP2P",
                                                     "JointWheel",
                                                     "AllowInventoryDrop",
                                                     "ObjectTransfer",
                                                     "ObjectGroupOwned",
                                                     "ObjectYouOfficer",
                                                     "CameraDecoupled",
                                                     "AnimSource",
                                                     "CameraSource",
                                                     "CastShadows",
                                                     "DieAtEdge",
                                                     "ReturnAtEdge",
                                                     "Sandbox",
                                                     "Flying",
                                                     "ObjectOwnerModify",
                                                     "TemporaryOnRez",
                                                     "Temporary",
                                                     "ZlibCompressed"};
    std::string_view remaining = field.child_value();
    if (remaining.empty())
        return;
    std::uint32_t result = 0;
    const auto [end, error] = std::from_chars(remaining.data(), remaining.data() + remaining.size(), result);
    if (error == std::errc{} && end == remaining.data() + remaining.size()) {
        flags = result;
        return;
    }
    while (!remaining.empty()) {
        const auto begin = remaining.find_first_not_of(", \t\r\n");
        if (begin == std::string_view::npos)
            break;
        remaining.remove_prefix(begin);
        const auto length = remaining.find_first_of(", \t\r\n");
        const auto token = remaining.substr(0, length);
        if (token != "None") {
            const auto found = std::find(names.begin(), names.end(), token);
            if (found == names.end())
                return;
            result |= std::uint32_t{1} << std::distance(names.begin(), found);
        }
        remaining.remove_prefix(token.size());
    }
    flags = result;
}

template <class T> void read_xyz(pugi::xml_node node, T &value) {
    read_number(node, node.child("X") ? "X" : "x", value.x);
    read_number(node, node.child("Y") ? "Y" : "y", value.y);
    read_number(node, node.child("Z") ? "Z" : "z", value.z);
}

bool read_xml_int32(pugi::xml_node field, std::int32_t &result);
bool read_xml_boolean(pugi::xml_node field);

bool read_shape(pugi::xml_node node, SceneObject &part) {
    auto &shape = part.shape;
    read_number(node, "ProfileCurve", shape.profile_curve);
    read_number(node, "PathBegin", shape.path_begin);
    read_number(node, "PathCurve", shape.path_curve);
    read_number(node, "PathEnd", shape.path_end);
    read_number(node, "PathRadiusOffset", shape.path_radius_offset);
    read_number(node, "PathRevolutions", shape.path_revolutions);
    read_number(node, "PathScaleX", shape.path_scale_x);
    read_number(node, "PathScaleY", shape.path_scale_y);
    read_number(node, "PathShearX", shape.path_shear_x);
    read_number(node, "PathShearY", shape.path_shear_y);
    read_number(node, "PathSkew", shape.path_skew);
    read_number(node, "PathTaperX", shape.path_taper_x);
    read_number(node, "PathTaperY", shape.path_taper_y);
    read_number(node, "PathTwist", shape.path_twist);
    read_number(node, "PathTwistBegin", shape.path_twist_begin);
    read_number(node, "PCode", shape.pcode);
    read_number(node, "ProfileBegin", shape.profile_begin);
    read_number(node, "ProfileEnd", shape.profile_end);
    read_number(node, "ProfileHollow", shape.profile_hollow);
    read_number(node, "State", shape.state);
    if (!read_binary_element(node.child("TextureEntry"), part.texture_entry))
        return false;
    using Shape = framework::PrimitiveBaseShapeCore;
    using FloatField = std::pair<std::string_view, float Shape::*>;
    constexpr FloatField float_fields[] = {
        {"LightRadius", &Shape::light_radius},   {"LightCutoff", &Shape::light_cutoff},
        {"LightFalloff", &Shape::light_falloff}, {"LightIntensity", &Shape::light_intensity},
        {"FlexiTension", &Shape::flexi_tension}, {"FlexiDrag", &Shape::flexi_drag},
        {"FlexiGravity", &Shape::flexi_gravity}, {"FlexiWind", &Shape::flexi_wind},
        {"FlexiForceX", &Shape::flexi_force_x},  {"FlexiForceY", &Shape::flexi_force_y},
        {"FlexiForceZ", &Shape::flexi_force_z},
    };
    constexpr std::array<std::string_view, 4> light_colors{"LightColorR", "LightColorG", "LightColorB", "LightColorA"};
    const bool has_legacy_fields = std::ranges::any_of(node.children(), [&](pugi::xml_node field) {
        const std::string_view name = field.name();
        return name == "SculptType" || name == "SculptTexture" || name == "SculptEntry" || name == "LightEntry" ||
               name == "FlexiEntry" || name == "FlexiSoftness" ||
               std::ranges::find(light_colors, name) != light_colors.end() ||
               std::ranges::find(float_fields, name, &FloatField::first) != std::end(float_fields);
    });
    if (!has_legacy_fields) {
        if (!read_binary_element(node.child("ExtraParams"), part.extra_params))
            return false;
    } else {
        auto geometry = framework::PrimitiveBaseShapeCore::create([](const std::string &) {});
        for (const auto field : node.children()) {
            const std::string_view name = field.name();
            if (name == "ExtraParams") {
                if (!read_binary_element(field, part.extra_params))
                    return false;
                try {
                    geometry.read_extra_params(std::make_shared<framework::ShapeByteArray>(part.extra_params));
                } catch (const framework::PrimitiveShapeError &) {
                    return false;
                }
            } else if (name == "SculptType") {
                std::int32_t type;
                if (!read_xml_int32(field, type))
                    return false;
                geometry.sculpt_type = static_cast<std::uint8_t>(type);
            } else if (name == "SculptEntry") {
                geometry.sculpt_entry = read_xml_boolean(field);
            } else if (name == "SculptTexture") {
                read_uuid_field(field, geometry.sculpt_texture);
            } else if (name == "LightEntry") {
                geometry.light_entry = read_xml_boolean(field);
            } else if (name == "FlexiEntry") {
                geometry.flexi_entry = read_xml_boolean(field);
            } else if (name == "FlexiSoftness") {
                std::int32_t softness;
                if (!read_xml_int32(field, softness))
                    return false;
                geometry.flexi_softness = softness;
            } else if (const auto color = std::ranges::find(light_colors, name); color != light_colors.end()) {
                float value;
                if (!read_script_number(read_scalar_text(field), value))
                    return false;
                geometry.light_color(static_cast<unsigned>(color - light_colors.begin()), value);
            } else if (const auto parameter = std::ranges::find(float_fields, name, &FloatField::first);
                       parameter != std::end(float_fields)) {
                if (!read_script_number(read_scalar_text(field), geometry.*parameter->second))
                    return false;
            }
        }
        part.extra_params = geometry.extra_params();
    }
    if (!read_binary_element(node.child("MatOvrd"), part.material_overrides))
        return false;
    part.face_media_xml = node.child_value("Media");
    return true;
}

bool read_xml_int32(pugi::xml_node field, std::int32_t &result) {
    constexpr std::string_view whitespace = " \t\r\n";
    std::string content;
    for (const auto child : field.children()) {
        if (child.type() == pugi::node_element)
            return false;
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
            content += child.value();
    }
    std::string_view text = content;
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return false;
    text = text.substr(first, text.find_last_not_of(whitespace) - first + 1);
    if (text.front() == '+') {
        text.remove_prefix(1);
        if (text.empty() || text.front() == '-')
            return false;
    }
    std::int32_t value;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
        return false;
    result = value;
    return true;
}

bool xml_true(std::string_view text) {
    return text == "1" || std::ranges::equal(text, std::string_view("true"), [](char actual, char lower) {
               return actual == lower || actual == lower - ('a' - 'A');
           });
}

bool read_xml_boolean(pugi::xml_node field) {
    return xml_true(read_scalar_text(field));
}

framework::TaskInventoryItem read_task_item(pugi::xml_node node) {
    framework::TaskInventoryItem item;
    read_uuid(node, "AssetID", item.asset_id);
    read_uuid(node, "CreatorID", item.creator_id);
    read_uuid(node, "GroupID", item.group_id);
    read_uuid(node, "ItemID", item.item_id);
    read_uuid(node, "OldItemID", item.old_item_id);
    read_uuid(node, "LastOwnerID", item.last_owner_id);
    read_uuid(node, "OwnerID", item.owner_id);
    read_uuid(node, "ParentID", item.parent_id);
    read_uuid(node, "ParentPartID", item.parent_part_id);
    read_uuid(node, "PermsGranter", item.permissions_granter);
    const auto read_integer = [&](const char *name, auto &target) {
        for (const auto field : node.children(name)) {
            std::int32_t value;
            if (read_xml_int32(field, value))
                target = static_cast<std::remove_reference_t<decltype(target)>>(value);
        }
    };
    read_integer("BasePermissions", item.base_permissions);
    read_integer("CreationDate", item.creation_date);
    read_integer("EveryonePermissions", item.everyone_permissions);
    read_integer("Flags", item.flags);
    read_integer("GroupPermissions", item.group_permissions);
    read_integer("InvType", item.inventory_type);
    read_integer("NextPermissions", item.next_permissions);
    read_integer("CurrentPermissions", item.current_permissions);
    read_integer("PermsMask", item.permissions_mask);
    read_integer("Type", item.type);
    for (const auto field : node.children()) {
        const std::string_view name = field.name();
        if (name != "CreatorData" && name != "Description" && name != "Name" && name != "OwnerChanged")
            continue;
        std::string content;
        for (const auto child : field.children()) {
            if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
                content += child.value();
        }
        if (name == "CreatorData")
            item.creator_data = std::move(content);
        else if (name == "Description")
            item.description = std::move(content);
        else if (name == "Name")
            item.name = std::move(content);
        else
            item.owner_changed = xml_true(content);
    }
    return item;
}

std::optional<framework::PrimInertiaOverride> read_inertia(pugi::xml_node field) {
    if (!field)
        return {};

    const auto read_float = [](pugi::xml_node node, float &result) {
        for (const auto child : node.children())
            if (child.type() == pugi::node_element)
                return false;
        return framework::parse_invariant_float(node.child_value(), result);
    };
    const auto read_components = [&](pugi::xml_node node, std::initializer_list<float *> components) {
        auto target = components.begin();
        for (const auto child : node.children()) {
            if (child.type() != pugi::node_element)
                continue;
            if (target == components.end() || !read_float(child, **target))
                return false;
            ++target;
        }
        return target == components.end();
    };

    framework::PrimInertiaOverride inertia;
    inertia.rotation = {0, 0, 0, 0};
    for (const auto child : field.children()) {
        if (child.type() != pugi::node_element)
            continue;
        const std::string_view name = child.name();
        if (name == "MASS" && !read_float(child, inertia.mass))
            return {};
        if (name == "CM" && !read_components(child, {&inertia.center.x, &inertia.center.y, &inertia.center.z}))
            return {};
        if (name == "INERTIA" &&
            !read_components(child, {&inertia.diagonal.x, &inertia.diagonal.y, &inertia.diagonal.z}))
            return {};
        if (name == "IROT" && !read_components(child, {&inertia.rotation.x, &inertia.rotation.y, &inertia.rotation.z,
                                                       &inertia.rotation.w}))
            return {};
    }
    return inertia;
}

struct SdkObjectXmlText {
    std::optional<std::u16string> name, description;
};

struct SdkObjectXmlTexts : std::map<const void *, SdkObjectXmlText> {
    std::map<const void *, ManagedScriptState> script_states;
};

bool read_sdk_object_xml_text(std::span<const std::uint8_t> xml, pugi::xml_node document, SdkObjectXmlTexts &texts) {
    std::vector<XmlNode> managed_nodes;
    try {
        auto dom = load_xml_dom(xml);
        const auto collect = [&](const auto &self, const XmlNode &node) -> void {
            if (node->node_type() == 1 && xml_name(node) == u"ScriptState")
                managed_nodes.push_back(node);
            for (auto child = node->first_child(); child; child = child->next_sibling())
                self(self, child);
        };
        collect(collect, dom.document_node());
        std::vector<pugi::xml_node> mirrored;
        const auto collect_pugi = [&](const auto &self, pugi::xml_node node) -> void {
            if (std::string_view(node.name()) == "ScriptState")
                mirrored.push_back(node);
            for (auto child : node.children())
                self(self, child);
        };
        collect_pugi(collect_pugi, document);
        if (mirrored.size() != managed_nodes.size())
            return false;
        for (std::size_t i = 0; i < mirrored.size(); ++i)
            if (xml_attribute(managed_nodes[i], u"Engine") == u"YEngine" ||
                (managed_nodes[i]->parent() && xml_attribute(managed_nodes[i]->parent(), u"Engine") == u"YEngine"))
                texts.script_states.emplace(mirrored[i].internal_object(), read_xml_managed_state(managed_nodes[i]));
    } catch (...) {
        return false;
    }
    std::vector<SdkObjectXmlText> values;
    auto settings = framework::NiniXmlReaderSettings::legacy_document();
    auto reader = framework::NativeNiniXmlReader::from_bytes(xml, settings);
    int part_depth = -1, field_depth = -1;
    bool description = false;
    try {
        while (reader->read()) {
            const auto &token = reader->token();
            if (token.node_type == 1) {
                if (token.name == u"SceneObjectPart") {
                    if (part_depth >= 0)
                        return false;
                    values.emplace_back();
                    part_depth = token.is_empty_element ? -1 : token.depth;
                } else if (part_depth >= 0 && token.depth == part_depth + 1 &&
                           (token.name == u"Name" || token.name == u"Description")) {
                    description = token.name == u"Description";
                    (description ? values.back().description : values.back().name).emplace();
                    field_depth = token.is_empty_element ? -1 : token.depth;
                } else if (field_depth >= 0)
                    return false; // ReadElementString rejects child elements.
            } else if (token.node_type == 15) {
                if (token.depth == field_depth)
                    field_depth = -1;
                if (token.depth == part_depth)
                    part_depth = -1;
            } else if (field_depth >= 0) {
                if (token.node_type == 3 || token.node_type == 4 || token.node_type == 13 || token.node_type == 14)
                    *(description ? values.back().description : values.back().name) += token.value;
                else if (token.node_type == 5)
                    reader->resolve_entity();
            }
        }
    } catch (...) {
        return false;
    }
    std::vector<pugi::xml_node> parts;
    const auto collect = [&](const auto &self, pugi::xml_node node) -> void {
        if (std::string_view(node.name()) == "SceneObjectPart")
            parts.push_back(node);
        for (const auto child : node.children())
            self(self, child);
    };
    collect(collect, document);
    if (parts.size() != values.size())
        return false;
    for (std::size_t i = 0; i < parts.size(); ++i)
        texts.emplace(parts[i].internal_object(), std::move(values[i]));
    return true;
}

std::optional<ObjectAssetPart> read_part(pugi::xml_node node, bool root, const SdkObjectXmlTexts &texts) {
    ObjectAssetPart result;
    auto &part = result.object;
    read_uuid(node, "UUID", part.id);
    read_uuid(node, "CreatorID", part.creator);
    for (const auto field : node.children("CreatorData"))
        part.creator_data = field.child_value();
    read_uuid(node, "OwnerID", part.owner);
    read_uuid(node, "GroupID", part.group);
    read_uuid(node, "LastOwnerID", part.last_owner);
    read_uuid(node, "RezzerID", part.rezzer);
    part.inertia_override = read_inertia(node.child("PhysicsInertia"));
    for (const auto field : node.children("RotationAxisLocks")) {
        std::int32_t locks;
        if (!read_xml_int32(field, locks))
            return std::nullopt;
        part.rotation_axis_locks = static_cast<std::uint8_t>(locks);
    }
    read_uuid(node, "CollisionSound", part.collision_sound);
    read_uuid(node, "SoundID", part.looped_sound);
    const auto source_text = texts.find(node.internal_object());
    if (source_text == texts.end())
        return {};
    const auto assign_sdk = [](const std::u16string &text, bool description) {
        const auto bytes =
            framework::sdk_utf8_no_term(text, description ? std::optional<std::size_t>{127} : std::nullopt);
        return std::string(bytes.begin(), bytes.end());
    };
    try {
        if (source_text->second.name)
            part.name = assign_sdk(*source_text->second.name, false);
        if (source_text->second.description)
            part.description = assign_sdk(*source_text->second.description, true);
    } catch (const std::overflow_error &) {
        return {};
    }
    part.touch_name = node.child_value("TouchName");
    part.sit_name = node.child_value("SitName");
    part.floating_text = node.child_value("Text");
    part.media_url = node.child_value("MediaUrl");
    read_number(node, "LocalId", part.local_id);
    read_number(node, "ParentID", part.parent_id);
    read_number(node, "LinkNum", part.link_order);
    read_number(node, "Material", part.material);
    read_number(node, "CreationDate", part.creation_date);
    read_number(node, "Category", part.category);
    read_number(node, "SalePrice", part.sale_price);
    read_number(node, "ObjectSaleType", part.sale_type);
    read_number(node, "ClickAction", part.click_action);
    read_number(node, "BaseMask", part.base_mask);
    read_number(node, "OwnerMask", part.owner_mask);
    read_number(node, "GroupMask", part.group_mask);
    read_number(node, "EveryoneMask", part.everyone_mask);
    read_number(node, "NextOwnerMask", part.next_mask);
    for (const auto field : node.children()) {
        const std::string_view name = field.name();
        if (name == "Flags") {
            read_prim_flags(field, part.flags);
        } else if (name == "VolumeDetectActive") {
            if (read_xml_boolean(field))
                part.flags |= prim_flag_volume_detect;
            else
                part.flags &= ~prim_flag_volume_detect;
        } else if (name == "PassTouches")
            part.pass_touches = read_xml_boolean(field);
        else if (name == "PassCollisions")
            part.pass_collisions = read_xml_boolean(field);
        else if (name == "SoundQueueing")
            part.sound_queueing = read_xml_boolean(field);
    }
    read_number(node, "ScriptAccessPin", part.script_access_pin);
    // The original keeps AllowedDrop separate from its Flags property.
    part.flags &= ~0x10000u;
    for (const auto allowed_drop : node.children("AllowedDrop"))
        part.flags = read_xml_boolean(allowed_drop) ? part.flags | 0x10000u : part.flags & ~0x10000u;
    read_number(node, "CollisionSoundVolume", part.collision_sound_volume);
    read_number(node, "SoundGain", part.looped_sound_gain);
    if (!read_binary_element(node.child("TextureAnimation"), part.texture_animation) ||
        !read_binary_element(node.child("ParticleSystem"), part.particle_system))
        return std::nullopt;
    read_number(node, "SoundFlags", part.sound_flags);
    read_number(node, "SoundRadius", part.sound_radius);
    constexpr std::array pay_names{"PayPrice0", "PayPrice1", "PayPrice2", "PayPrice3", "PayPrice4"};
    for (std::size_t i = 0; i < pay_names.size(); ++i)
        read_number(node, pay_names[i], part.pay_prices[i]);
    read_number(node, "Density", part.physics.density);
    read_number(node, "Friction", part.physics.friction);
    read_number(node, "Bounce", part.physics.restitution);
    read_number(node, "GravityModifier", part.physics.gravity);
    read_number(node, "Buoyancy", part.buoyancy);
    read_xyz(node.child("Force"), part.force);
    read_xyz(node.child("Torque"), part.torque);
    const auto color = node.child("Color");
    constexpr std::array channels{"A", "R", "G", "B"};
    for (const auto channel : channels) {
        float component = 0;
        read_number(color, channel, component);
        part.text_argb = (part.text_argb << 8) |
                         (component >= 0.f && component <= 255.f ? static_cast<std::uint32_t>(component) : 0u);
    }
    read_xyz(node.child(root ? "GroupPosition" : "OffsetPosition"), part.position);
    read_xyz(node.child("AttachedPos"), result.attachment_position);
    read_number(node.child("Shape"), "LastAttachPoint", result.last_attachment_point);
    part.last_attachment_point = result.last_attachment_point;
    part.saved_attachment_position = result.attachment_position;
    const auto rotation = node.child("RotationOffset");
    read_xyz(rotation, part.rotation);
    read_number(rotation, rotation.child("W") ? "W" : "w", part.rotation.w);
    read_xyz(node.child("Scale"), part.scale);
    read_number(node, "SitActRange", part.sit_active_range);
    read_xyz(node.child("StandTarget"), part.stand_target);
    read_xyz(node.child("CameraEyeOffset"), part.camera_eye_offset);
    read_xyz(node.child("CameraAtOffset"), part.camera_at_offset);
    for (const auto field : node.children()) {
        const std::string_view name = field.name();
        if (name == "SitTargetPosition" || name == "SitTargetPositionLL")
            read_xyz(field, part.sit_target_position);
        else if (name == "SitTargetOrientation" || name == "SitTargetOrientationLL") {
            read_xyz(field, part.sit_target_orientation);
            read_number(field, field.child("W") ? "W" : "w", part.sit_target_orientation.w);
        }
    }
    read_xyz(node.child("Velocity"), part.velocity);
    read_xyz(node.child("AngularVelocity"), part.angular_velocity);
    read_xyz(node.child("Acceleration"), part.acceleration);
    for (const auto field : node.children()) {
        const std::string_view name = field.name();
        if (name == "Shape") {
            if (!read_shape(field, part))
                return std::nullopt;
        } else if (name == "PhysicsShapeType") {
            std::int32_t value;
            if (!read_xml_int32(field, value))
                return std::nullopt;
            const auto type = static_cast<std::uint8_t>(value);
            part.physics.shape = type <= 2 ? type : default_physics_shape(part);
        }
    }
    if (const auto attrs = node.child("DynAttrs").child("llsd")) {
        std::ostringstream xml;
        attrs.print(xml, "", pugi::format_raw);
        part.dynamic_attributes_xml = xml.str();
    }
    if (!read_binary_element(node.child("SOPAnims"), part.object_animations))
        return std::nullopt;
    for (const auto field : node.children()) {
        const std::string_view name = field.name();
        if (name == "InventorySerial") {
            std::int32_t serial;
            if (!read_xml_int32(field, serial))
                return std::nullopt;
            result.inventory_serial = static_cast<std::uint32_t>(serial);
        } else if (name == "TaskInventory") {
            result.inventory.clear();
            std::set<framework::Uuid> item_ids;
            for (const auto item_node : field.children("TaskInventoryItem")) {
                auto item = read_task_item(item_node);
                if (!item_ids.insert(item.item_id).second)
                    return std::nullopt;
                result.inventory.push_back(std::move(item));
            }
            // Assigning SceneObjectPart.TaskInventory advances the serial in the original reader.
            ++result.inventory_serial;
        }
    }
    part.inventory_empty = result.inventory.empty();
    // Shape state identifies an attachment asset even before a wearer is bound.
    // ParentID alone can be stale on inventory assets; LastAttachPoint also
    // belongs to objects that have already been dropped on the ground.
    // Keep position as GroupPosition here: ordinary ground rez uses that value
    // to place linksets, while attachment admission chooses its offset later.
    if (root && part.shape.pcode == 9 && part.shape.state != 0 && node.child("GroupPosition"))
        part.attached_group_position = part.position;
    if (root && node.child("OffsetPosition")) {
        framework::Vector3 offset{};
        read_xyz(node.child("OffsetPosition"), offset);
        part.root_offset_position = offset;
    }
    return result;
}

bool read_linkset(pugi::xml_node group, ObjectAssetLinkset &linkset, const SdkObjectXmlTexts &texts) {
    const auto root_wrapper = group.child("RootPart");
    const auto root = root_wrapper ? root_wrapper.child("SceneObjectPart") : group.child("SceneObjectPart");
    if (!root)
        return false;
    auto &parts = linkset.parts;
    auto root_part = read_part(root, true, texts);
    if (!root_part)
        return false;
    parts.push_back(std::move(*root_part));
    if (!read_binary_element(group.child("KeyframeMotion"), parts.front().object.keyframed_motion))
        return false;
    if (!parts.front().object.keyframed_motion.empty() &&
        !decode_keyframed_motion(parts.front().object.keyframed_motion))
        parts.front().object.keyframed_motion.clear();
    if (const auto start_string = group.child("StartStr"))
        parts.front().object.rez_start_string = read_scalar_text(start_string);
    // The original ignores linkset data when its base64 cannot be decoded.
    read_binary_element(group.child("lnkstdt"), parts.front().object.linkset_data);
    for (const auto child : group.child("OtherParts").children(root_wrapper ? "Part" : "SceneObjectPart")) {
        const auto part = root_wrapper ? child.child("SceneObjectPart") : child;
        if (!part)
            return false;
        auto child_part = read_part(part, false, texts);
        if (!child_part)
            return false;
        parts.push_back(std::move(*child_part));
        if (!parts.back().object.link_order)
            parts.back().object.link_order = parts.size();
    }
    for (const auto saved : group.child("GroupScriptStates").children("SavedScriptState")) {
        framework::Uuid item_id;
        if (!framework::try_parse_sdk_uuid(saved.attribute("UUID").value(), item_id) || item_id.is_zero())
            continue;
        const auto wrapper = saved.child("State");
        const auto state = wrapper ? wrapper.child("ScriptState") : saved.child("ScriptState");
        const auto metadata = wrapper ? wrapper : state;
        const std::string_view engine = metadata.attribute("Engine").value();
        const bool native = engine == "NativeLSL";
        if (wrapper && engine != "YEngine" && engine != "XEngine" && !native)
            continue;
        const auto running = state.child("Running");
        if (!running)
            continue;
        const std::string_view value = running.child_value();
        const bool stopped = value == "False" || value == "false" || value == "0";
        if (native && !stopped && value != "True" && value != "true" && value != "1")
            continue;
        framework::Uuid asset;
        const bool managed = engine == "YEngine";
        if ((managed || native) &&
            (!framework::try_parse_sdk_uuid(metadata.attribute("Asset").value(), asset) || asset.is_zero()))
            continue;
        for (auto &part : parts) {
            for (auto &item : part.inventory) {
                if (item.item_id != item_id || item.inventory_type != 10)
                    continue;
                if (native) {
                    if (item.asset_id != asset)
                        continue;
                    item.script_running = !stopped;
                    const auto encoded = state.child("NativeCheckpoint");
                    std::vector<std::uint8_t> bytes;
                    if (!read_binary_element(encoded, bytes))
                        continue;
                    if (auto checkpoint = script::decode_lsl_checkpoint(bytes);
                        checkpoint && checkpoint->running == item.script_running) {
                        // Inventory permissions come from the task item and rez ownership rules.
                        checkpoint->permissions_granter = {};
                        checkpoint->permissions_mask = 0;
                        part.native_script_checkpoints.insert_or_assign(item_id, std::move(*checkpoint));
                    }
                    continue;
                }
                if (!managed && stopped)
                    item.script_running = false;
                if (managed && item.asset_id == asset) {
                    const auto retained = texts.script_states.find(state.internal_object());
                    auto plugins =
                        retained == texts.script_states.end() ? read_managed_script_state(state) : retained->second;
                    if (plugins.source_hash.empty() && stopped)
                        item.script_running = false;
                    if (!plugins.empty())
                        part.managed_script_states.insert_or_assign(item_id, std::move(plugins));
                }
            }
        }
    }
    std::stable_sort(parts.begin() + 1, parts.end(), [](const auto &left, const auto &right) {
        return left.object.link_order < right.object.link_order;
    });
    // The world derives link numbers from relative order. Give ties a stable
    // ordering as well, so missing or repeated numbers never collide at rez.
    for (std::size_t i = 1; i < parts.size(); ++i)
        parts[i].object.link_order = i + 1;
    return true;
}
} // namespace

std::string write_prim_inertia_xml(const framework::PrimInertiaOverride &inertia) {
    pugi::xml_document document;
    auto node = document.append_child("PhysicsInertia");
    node.append_child("MASS").text().set(inertia.mass);
    const auto append_vector = [&](const char *name, const auto &value) {
        auto field = node.append_child(name);
        field.append_child("X").text().set(value.x);
        field.append_child("Y").text().set(value.y);
        field.append_child("Z").text().set(value.z);
        return field;
    };
    append_vector("CM", inertia.center);
    append_vector("INERTIA", inertia.diagonal);
    append_vector("IROT", inertia.rotation).append_child("W").text().set(inertia.rotation.w);
    std::ostringstream xml;
    node.print(xml, "", pugi::format_raw);
    return xml.str();
}

std::optional<framework::PrimInertiaOverride> read_prim_inertia_xml(std::string_view xml) {
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size()))
        return {};
    return read_inertia(document.child("PhysicsInertia"));
}

std::optional<ObjectAsset> read_object_asset_xml(std::span<const std::uint8_t> xml) {
    pugi::xml_document document;
    if (!document.load_buffer(xml.data(), xml.size()))
        return std::nullopt;
    SdkObjectXmlTexts texts;
    if (!read_sdk_object_xml_text(xml, document, texts))
        return std::nullopt;
    ObjectAsset asset;
    if (const auto collection = document.child("CoalescedObject")) {
        asset.coalesced = true;
        asset.bounds = {collection.attribute("x").as_float(), collection.attribute("y").as_float(),
                        collection.attribute("z").as_float()};
        for (const auto wrapper : collection.children("SceneObjectGroup")) {
            auto &linkset = asset.linksets.emplace_back();
            linkset.offset = {wrapper.attribute("offsetx").as_float(), wrapper.attribute("offsety").as_float(),
                              wrapper.attribute("offsetz").as_float()};
            const auto inner = wrapper.child("SceneObjectGroup");
            if (!read_linkset(inner ? inner : wrapper, linkset, texts))
                return std::nullopt;
        }
    } else {
        if (!read_linkset(document.child("SceneObjectGroup"), asset.linksets.emplace_back(), texts))
            return std::nullopt;
    }
    if (asset.linksets.empty())
        return std::nullopt;
    return asset;
}
} // namespace opensim::scene
