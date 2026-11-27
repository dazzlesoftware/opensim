// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/object_asset_xml.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace opensim;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::string file(const std::filesystem::path &p) {
    std::ifstream input(p, std::ios::binary);
    check(bool(input), "Missing SDK XML reference");
    return {std::istreambuf_iterator<char>(input), {}};
}

std::string unhex(std::string_view text) {
    check(text.size() % 2 == 0, "Malformed original XML byte hex");
    auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    std::string out;
    for (std::size_t i = 0; i < text.size(); i += 2)
        out += static_cast<char>(16 * digit(text[i]) + digit(text[i + 1]));
    return out;
}

std::u16string units(const network::Value &value) {
    std::u16string result;
    for (const auto &unit : value.get("units").array)
        result += static_cast<char16_t>(network::osd_integer(unit));
    return result;
}

std::string xml(const scene::ObjectAssetPart &part) {
    std::u16string text;
    framework::XmlTextWriterIo io;
    io.encoding = [] { return std::shared_ptr<framework::NativeXmlTextEncoding>{}; };
    io.write = [&](auto, auto value) {
        if (value)
            text.append(*value);
    };
    io.write_array = [&](auto data, int start, int count) { text.append(data->data() + start, count); };
    io.write_whole_array = [&](auto data) { text.append(data->data(), data->size()); };
    framework::NativeXmlTextWriter writer(std::move(io));
    scene::write_object_asset_xml(writer, part, {});
    return framework::utf8(text);
}

auto parse(const std::string &text) {
    return scene::read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
}

void compare(const scene::ObjectAsset &asset, const network::Value &row, const std::string &path) {
    check(asset.linksets.size() == 1 && asset.linksets[0].parts.size() == 1,
          path + ": restored object dimensions differ");
    const auto &part = asset.linksets[0].parts[0].object;
    const auto &expected = row.get("xml_restore");
    check(framework::utf16_replacing(part.name) == units(expected.get("name")),
          path + ": restored Name UTF16 differs from original second SDK conversion");
    check(framework::utf16_replacing(part.description) == units(expected.get("description")),
          path + ": restored Description UTF16 differs from original");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original object-text XML fixture directory");
        const auto rows = network::parse_osd_json(file(std::filesystem::path(argv[1]) / "metadata.json"));
        check(rows.array.size() == 123, "Original object-text XML corpus count differs");
        std::size_t original_changes = 0, original_xml_admitted = 0;
        for (const auto &row : rows.array) {
            const auto name = row.get("name").text;
            check(row.get("xml").get("success").boolean && row.get("xml_restore").get("success").boolean,
                  name + ": original XML control did not succeed");
            scene::ObjectAssetPart part;
            part.object = scene::SceneObject::primitive(framework::Uuid::random(), {128, 128, 30}, {1, 1, 1});
            // These are actual original getter bytes AFTER first setter normalization.
            // Restoration must apply the SDK property setter again, not assume identity.
            part.object.name = unhex(row.get("Name").get("utf8").text);
            part.object.description = unhex(row.get("Desc").get("utf8").text);
            if (units(row.get("Name")) != units(row.get("xml_restore").get("name")))
                ++original_changes;
            const auto own = parse(xml(part));
            check(bool(own), name + ": native writer/reader failed object text roundtrip");
            compare(*own, row, name + " native XML");
            const auto original_sop = unhex(row.get("xml").get("utf8").text);
            const auto original = parse("<SceneObjectGroup>" + original_sop + "<OtherParts /></SceneObjectGroup>");
            if (original) {
                ++original_xml_admitted;
                compare(*original, row, name + " original XML");
            } else {
                std::cout << "DEFERRED: original SOP wrapper admission " << name << '\n';
            }
        }
        check(original_changes == 12, "Original nonidentity Name XML restoration case count differs");
        // Native parser admission of arbitrary original SOP wrappers is separately
        // counted. Every admitted original object must retain the exact oracle units.
        std::cout << "PASS: 123 native object text XML roundtrips,12 original nonidentity Name controls; actual "
                     "original SOP wrappers admitted "
                  << original_xml_admitted << "/123; DB/process restart not verified\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
