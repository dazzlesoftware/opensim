// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_object_test_support.hpp"
#include "opensim/scene/object_asset_xml.hpp"

using namespace object_test;

namespace {
void object_asset_large_checkpoint(const LslCheckpoint &checkpoint) {
    using namespace opensim::scene;
    ObjectAssetPart part;
    const auto owner = framework::Uuid::random();
    part.object = SceneObject::primitive(owner, {128, 128, 25});
    framework::TaskInventoryItem item;
    item.item_id = framework::Uuid::random();
    item.asset_id = framework::Uuid::random();
    item.parent_id = item.parent_part_id = part.object.id;
    item.owner_id = item.creator_id = owner;
    item.type = item.inventory_type = 10;
    item.name = "Large checkpoint script";
    part.inventory.push_back(item);
    const auto expected = std::get<std::shared_ptr<const LslFixedArray>>(checkpoint.globals[0]);
    const auto expected_hash = fixed_array_cells_hash(expected);
    for (const bool running : {false, true}) {
        auto saved = checkpoint;
        saved.running = running;
        part.inventory.front().script_running = running;
        part.native_script_checkpoints.insert_or_assign(item.item_id, std::move(saved));
        std::u16string text;
        framework::XmlTextWriterIo io;
        io.encoding = [] { return std::shared_ptr<framework::NativeXmlTextEncoding>{}; };
        io.write = [&](auto, auto value) {
            if (value)
                text.append(*value);
        };
        io.write_array = [&](auto value, int offset, int count) { text.append(value->data() + offset, count); };
        io.write_whole_array = [&](auto value) { text.append(value->data(), value->size()); };
        framework::NativeXmlTextWriter writer(std::move(io));
        write_object_asset_xml(writer, part, {});
        const auto xml = framework::utf8(text);
        constexpr std::string_view open = "<NativeCheckpoint>", close = "</NativeCheckpoint>";
        const auto start = xml.find(open), end = xml.find(close);
        check(start != std::string::npos && end != std::string::npos && end - start - open.size() > 6 * 1024 * 1024,
              "Object checkpoint did not cross the former Base64 limit: XMLbytes=" + std::to_string(xml.size()) +
                  " start=" + std::to_string(start) + " end=" + std::to_string(end));
        const auto asset = read_object_asset_xml({reinterpret_cast<const std::uint8_t *>(xml.data()), xml.size()});
        check(asset && asset->linksets.size() == 1 && asset->linksets[0].parts.size() == 1,
              "Large-checkpoint object XML rejected");
        const auto &restored_part = asset->linksets[0].parts[0];
        check(restored_part.inventory.size() == 1 && restored_part.inventory[0].item_id == item.item_id &&
                  restored_part.inventory[0].asset_id == item.asset_id &&
                  restored_part.inventory[0].owner_id == owner && restored_part.inventory[0].script_running == running,
              "Large-checkpoint object task inventory identity/running state changed");
        const auto found = restored_part.native_script_checkpoints.find(item.item_id);
        check(found != restored_part.native_script_checkpoints.end(), "Large native checkpoint lost from object XML");
        const auto &restored = found->second;
        check(restored.running == running && restored.program_hash == checkpoint.program_hash &&
                  restored.globals.size() == 2,
              "Large object checkpoint state changed");
        const auto first = std::get<std::shared_ptr<const LslFixedArray>>(restored.globals[0]);
        const auto alias = std::get<std::shared_ptr<const LslFixedArray>>(restored.globals[1]);
        check(first == alias && lsl_fixed_array_size(first) == 600000 && fixed_array_cells_hash(first) == expected_hash,
              "Large object checkpoint cells/alias changed");
    }
}

void native_large_checkpoint() {
    LslCheckpoint checkpoint;
    checkpoint.program_hash = "capacity-double-checkpoint";
    checkpoint.state = "default";
    const auto value = make_lsl_fixed_array(LslFixedArrayKind::Float, 600000);
    const auto array = std::get<std::shared_ptr<const LslFixedArray>>(value);
    lsl_fixed_array_set(array, 0, 1.25);
    lsl_fixed_array_set(array, 300000, -7.5);
    lsl_fixed_array_set(array, 599999, 2.5);
    checkpoint.globals = {value, value};
    object_asset_large_checkpoint(checkpoint);
    const auto wire = encode_lsl_checkpoint(checkpoint);
    check(wire.size() > 4 * 1024 * 1024, "Native capacity control did not cross the former byte limit");
    const auto restored = decode_lsl_checkpoint(wire);
    check(restored && restored->globals.size() == 2, "Large native checkpoint rejected");
    const auto result = std::get<std::shared_ptr<const LslFixedArray>>(restored->globals[0]);
    check(result == std::get<std::shared_ptr<const LslFixedArray>>(restored->globals[1]),
          "Large native checkpoint lost array alias");
    check(fixed_array_cells_hash(result) == fixed_array_cells_hash(array), "Large native checkpoint changed cells");
    auto malformed = wire;
    const std::array<std::uint8_t, 8> header{
        25, 0, static_cast<std::uint8_t>(LslFixedArrayKind::Float), 0, 0xc0, 0x27, 0x09, 0};
    const auto at = std::search(malformed.begin(), malformed.end(), header.begin(), header.end());
    check(at != malformed.end(), "Native capacity length header missing");
    std::fill(at + 4, at + 8, 255);
    check(!decode_lsl_checkpoint(malformed), "Impossible large checkpoint array length accepted");
    malformed = wire;
    malformed.resize(malformed.size() / 2);
    check(!decode_lsl_checkpoint(malformed), "Truncated large native checkpoint accepted");
}

void large_original_xml(const std::filesystem::path &folder, const network::Value &row) {
    const auto snapshot = read(folder / "snapshot.bin");
    check(framework::sha256_hex(snapshot) == row.get("sha256").get("snapshot.bin").text,
          "Large original XML snapshot bytes changed");
    const auto base64 = network::binary_base64(
        network::osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(snapshot.begin(), snapshot.end())));
    const auto asset_id = framework::Uuid::random(), item_id = framework::Uuid::random();
    const auto xml = "<ScriptState Engine=\"YEngine\" Asset=\"" + asset_id.str() + "\" SourceHash=\"" +
                     row.get("source_hash").text + "\"><!--" + std::string(16 * 1024 * 1024, 'x') +
                     "--><Running>True</Running><Snapshot>" + base64 + "</Snapshot>" +
                     read(folder / "detect-array.xml") + read(folder / "event-queue.xml") + "</ScriptState>";
    check(xml.size() > 16 * 1024 * 1024, "Original XML did not cross the former total limit");
    const auto source = read(folder / "source.lsl");
    const auto compiled = compile_lsl(source);
    const auto object = decode_yengine_object_code(bytes(read(folder / "original.yobj")));
    check(compiled && object, "Large original XML source/object control invalid");
    const Declarations declarations(source);
    const auto verify = [&](const std::optional<opensim::scene::ManagedScriptState> &saved) {
        check(saved && saved->running && *saved->running && saved->snapshot &&
                  saved->source_hash == row.get("source_hash").text,
              "Large original XML state was discarded");
        LslHost host;
        host.heap_limit_bytes = 100000000;
        LslInstance instance(compiled.program, {}, host);
        check(instance.restore_yengine_snapshot(*saved->snapshot, row.get("source_hash").text, source, *object),
              "Large original XML snapshot could not restore");
        const auto checkpoint = instance.checkpoint();
        check(checkpoint.has_value(), "Large original XML checkpoint unavailable");
        state(*checkpoint, declarations, row.get("after_load"), row);
    };
    verify(opensim::scene::read_yengine_state_xml(bytes(xml), asset_id));
    check(!opensim::scene::read_yengine_state_xml(bytes(xml), framework::Uuid::random()),
          "Large original XML bypassed asset identity validation");
    const auto directory = std::filesystem::temp_directory_path() / ("opensim-yengine-capacity-" + item_id.str());
    check(std::filesystem::create_directory(directory), "Could not create capacity state-file directory");
    const auto file = directory / (item_id.str() + ".state");

    struct Cleanup {
        std::filesystem::path file, directory;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
            std::filesystem::remove(directory, ignored);
        }
    } cleanup{file, directory};

    {
        std::ofstream output(file, std::ios::binary);
        output.write(xml.data(), static_cast<std::streamsize>(xml.size()));
        check(bool(output), "Could not write large original state file");
    }
    verify(opensim::scene::read_yengine_state_file(directory, item_id, asset_id));
    check(!opensim::scene::read_yengine_state_file(directory, item_id, framework::Uuid::random()),
          "Large original state file bypassed asset identity validation");
}

void invalid_original_length(const std::filesystem::path &folder) {
    const auto source = read(folder / "snapshot.bin");
    std::vector<std::uint8_t> wire(source.begin(), source.end());
    check(wire.size() > 4 * 1024 * 1024, "Original capacity control did not cross the former byte limit");
    const std::array<std::uint8_t, 8> header{9, 2, 'd', 'o', 0xc0, 0x27, 0x09, 0};
    const auto at = std::search(wire.begin(), wire.end(), header.begin(), header.end());
    check(at != wire.end(), "Original capacity length header missing");
    std::fill(at + 4, at + 8, 0x7f);
    check(!decode_yengine_snapshot(wire), "Impossible original capacity array length accepted");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original capacity fixture directory");
        const std::filesystem::path root = argv[1];
        check(framework::sha256_hex(read(root / "verification.json")) ==
                  "05696e21210db5600398cc80638ca8c89bdd4645071d5b0e3d29bdd21a79c24a",
              "Original capacity proof changed");
        check(framework::sha256_hex(read(root.parent_path() / "yengine_capacity_reference.cs")) ==
                  "093af1e2eab67c3d81dc2629c8c4d4d477721e892c2e32d01018659870698aa4",
              "Original capacity generator changed");
        native_large_checkpoint();
        for (const auto &[platform, digest] : std::array<std::pair<const char *, const char *>, 2>{{
                 {"windows", "517eb17ca0ab2052eac9e1f8bb17ffe58258bfb07729498261f9cd9e0dbff9eb"},
                 {"linux", "baf9eafbc9567f10acd7fd88f8a4acc983b9da0c0cea3f7a0350851fba3120c2"},
             }}) {
            const auto metadata = read(root / platform / "metadata.json");
            check(framework::sha256_hex(metadata) == digest, "Original capacity metadata changed");
            const auto rows = network::parse_osd_json(metadata);
            check(rows.array.size() == 1, "Original capacity case inventory changed");
            for (const auto &row : rows.array) {
                check(row.get("accepted").boolean && row.get("suspended").boolean && row.get("migrated").boolean,
                      "Original capacity compile/capture/migration did not succeed");
                const auto folder = root / platform / row.get("name").text;
                invalid_original_length(folder);
                if (std::string_view(platform) == "windows")
                    large_original_xml(folder, row);
                fixture(folder, row, 100000000);
            }
        }
        std::cout << "PASS original Windows/Linux 600000-cell checkpoints: full-cell hashes, aliases, native "
                     "roundtrip, object XML running/stopped states, preemption/live/persistent/original "
                     "resumes and malformed lengths\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
