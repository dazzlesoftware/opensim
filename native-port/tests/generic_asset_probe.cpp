// Actual production HTTP uploads coordinated with a live viewer and task Contents.
#include "user_inventory_probe.hpp"
#include "task_inventory_probe.hpp"
#include "opensim/network/osd_json.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
namespace opensim::tests {
void run_generic_asset_probe(framework::Uuid agent, const std::filesystem::path &metadata,
                             const LegacyInventoryProbeIo &io) {
    using framework::Uuid;
    auto require = [](bool b, const char *s) {
        if (!b)
            throw std::runtime_error(std::string("Generic upload probe: ") + s);
    };
    std::ifstream file(metadata, std::ios::binary);
    std::string raw((std::istreambuf_iterator<char>(file)), {});
    auto info = network::parse_osd_json(raw).get("inventory_rez").get("asset_update_items");
    if (info.kind != network::Value::Kind::Array)
        return;
    std::cout << "Generic agent uploads" << std::endl;
    std::string reply;
    require(bool(std::getline(std::cin, reply)) && reply == "OK",
            "actual agent HTTP uploads and persistence");
    auto before = io.snapshot();
    std::set<Uuid> previous;
    for (const auto &p : before)
        if (p.spec->name == "ObjectUpdate")
            for (const auto &b : p.blocks.at("ObjectData"))
                previous.insert(std::get<Uuid>(b.at("FullID")));
    auto add = io.make("ObjectAdd");
    auto &shape = add.block("ObjectData");
    shape["PCode"] = std::uint64_t(9);
    shape["PathCurve"] = std::uint64_t(16);
    shape["ProfileCurve"] = std::uint64_t(1);
    shape["PathScaleX"] = shape["PathScaleY"] = std::uint64_t(100);
    shape["Scale"] = std::vector<double>{.5, .5, .5};
    shape["Rotation"] = std::vector<double>{0, 0, 0, 1};
    shape["RayStart"] = std::vector<double>{128, 128, 70};
    shape["RayEnd"] = std::vector<double>{128, 128, 21};
    shape["RayEndIsIntersection"] = std::uint64_t(1);
    io.send(add);
    Uuid object;
    std::uint32_t local = 0;
    io.wait([&](const auto &packets) {
        for (auto n = before.size(); n < packets.size(); ++n)
            if (packets[n].spec->name == "ObjectUpdate")
                for (const auto &b : packets[n].blocks.at("ObjectData"))
                    if (std::get<std::uint64_t>(b.at("PCode")) == 9 &&
                        !previous.contains(std::get<Uuid>(b.at("FullID")))) {
                        object = std::get<Uuid>(b.at("FullID"));
                        local = static_cast<std::uint32_t>(std::get<std::uint64_t>(b.at("ID")));
                        return true;
                    }
        return false;
    });
    auto update = io.make("UpdateTaskInventory");
    update.block("UpdateData")["LocalID"] = std::uint64_t(local);
    update.block("InventoryData")["ItemID"] = network::osd_uuid(info.array.at(0));
    before = io.snapshot();
    io.send(update);
    io.wait([&](const auto &packets) {
        for (auto n = before.size(); n < packets.size(); ++n)
            if (packets[n].spec->name == "ObjectProperties")
                for (const auto &b : packets[n].blocks.at("ObjectData"))
                    if (std::get<Uuid>(b.at("ObjectID")) == object &&
                        std::get<std::int64_t>(b.at("InventorySerial")) == 1)
                        return true;
        return false;
    });
    auto first = read_task_inventory_file(object, local, 1, io);
    auto field = [&](const std::string &text, const char *name) {
        std::string needle = std::string("\t\t") + name + "\t";
        auto item_start = text.find("\tinv_item\t0\n");
        require(item_start != text.npos, "actual task item block");
        auto at = text.find(needle, item_start);
        require(at != text.npos, "task field");
        at += needle.size();
        return text.substr(at, text.find('\n', at) - at);
    };
    auto item = Uuid(field(first.text, "item_id"));
    require(field(first.text, "name") == "Upload note|", "actual source user item drop");
    std::cout << "Generic task upload " << object.str() << ' ' << item.str() << std::endl;
    require(bool(std::getline(std::cin, reply)) && reply.starts_with("OK "), "actual task HTTP update");
    auto asset = Uuid(reply.substr(3));
    auto second = read_task_inventory_file(object, local, 2, io);
    require(field(second.text, "item_id") == item.str() && field(second.text, "asset_id") == asset.str() &&
                field(second.text, "name") == "Upload note|",
            "real task Contents now references uploaded asset without renaming");
    auto snapshot = io.snapshot();
    for (auto n = before.size(); n < snapshot.size(); ++n)
        if (snapshot[n].spec->name == "ObjectProperties")
            for (const auto &b : snapshot[n].blocks.at("ObjectData"))
                require(std::get<Uuid>(b.at("ObjectID")) != object ||
                            std::get<std::int64_t>(b.at("InventorySerial")) != 2,
                        "generic source update does not invent an ObjectProperties reply");
    std::cout
        << "Verified production generic inventory upload aliases, raw asset persistence and actual task Contents/Xfer cache mutation\n";
}
}
