// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "yengine_utf16_test_support.hpp"
#include "opensim/scene/object_asset_xml.hpp"
#include <array>
#include <iostream>
#include <pugixml.hpp>
#include <unordered_set>
#include <vector>

using namespace utf16_test;

namespace {
constexpr std::size_t expected_lists = 1048575;
constexpr std::size_t expected_leaves = 1048576;

std::string verified_original_xml(const std::filesystem::path &root) {
    const auto proof_text = read(root / "verification.json");
    check(framework::sha256_hex(proof_text) == "07f256c4339f153c84e8a487603b7d88b50baccdbd4228db60519d11def14b6d",
          "Original DAG capacity proof changed");
    const auto proof = network::parse_osd_json(proof_text);
    check(proof.get("passed").boolean && network::osd_integer(proof.get("expectedListOccurrences")) == expected_lists &&
              network::osd_integer(proof.get("expectedInt32Leaves")) == expected_leaves,
          "Original DAG proof shape differs");
    check(framework::sha256_hex(read(root / "generator.cs")) == proof.get("generatorSha256").text,
          "Original DAG generator changed");
    check(framework::sha256_hex(read(root / "verify.py")) == proof.get("verifierSha256").text,
          "Original DAG independent verifier changed");

    std::string canonical;
    for (const auto &[platform, metadata_hash] : std::array<std::pair<const char *, const char *>, 2>{{
             {"windows", "59d46b4688d943cbf8b0d54b3d6e3327ab3040298da3fc3c0f89970dac9ddb91"},
             {"linux", "fd24b20c242feeb466f5fb42e625d139beb8e86804dc42d9707fb44d504bd83a"},
         }}) {
        const auto metadata_text = read(root / platform / "metadata.json");
        check(framework::sha256_hex(metadata_text) == metadata_hash, "Original DAG case metadata changed");
        const auto metadata = network::parse_osd_json(metadata_text);
        const auto &observed = metadata.get("after");
        check(metadata.get("postAccepted").boolean && network::osd_integer(metadata.get("queuedCount")) == 1 &&
                  network::osd_integer(metadata.get("restoredCount")) == 1 &&
                  network::osd_integer(observed.get("listOccurrences")) == expected_lists &&
                  network::osd_integer(observed.get("uniqueLists")) == expected_lists &&
                  network::osd_integer(observed.get("leaves")) == expected_leaves &&
                  observed.get("allLeavesOne").boolean && !observed.get("rootChildrenAlias").boolean &&
                  observed.get("cellsSha256").text == proof.get("expectedLeafCellsSha256").text,
              "Original DAG admission or restored graph differs");
        auto xml = read(root / platform / "event-queue.xml");
        const auto &platform_proof = proof.get("platforms").get(platform);
        check(xml.size() == 61866020 && framework::sha256_hex(xml) == platform_proof.get("xmlSha256").text &&
                  platform_proof.get("everyXmlListAndLeafVerified").boolean,
              "Original DAG XML bytes changed");
        if (canonical.empty())
            canonical = std::move(xml);
        else
            check(xml == canonical, "Original Windows/Linux DAG XML differs");
    }
    return canonical;
}

void verify_native_import(std::string xml, std::string_view expected_hash) {
    pugi::xml_document document;
    check(bool(document.load_buffer(xml.data(), xml.size())), "Original DAG XML did not parse");
    xml.clear();
    xml.shrink_to_fit();
    const auto queue = document.document_element();
    check(std::string_view(queue.name()) == "EventQueue", "Original DAG XML queue root differs");
    auto state = document.append_child("ScriptState");
    state.append_attribute("Engine").set_value("YEngine");
    state.append_child("Running").text().set("True");
    check(bool(state.append_move(queue)), "Could not wrap original queue without changing its contents");
    const auto imported = scene::read_managed_script_state(state);
    check(imported.running && *imported.running && imported.events.size() == 1 &&
              imported.events[0].name == "touch_start" && imported.events[0].arguments.size() == 1,
          "Direct original DAG import changed event envelope");

    std::vector<const LslValue *> pending{&imported.events[0].arguments[0]};
    std::unordered_set<const LslList *> identities;
    identities.reserve(expected_lists);
    std::size_t leaves = 0;
    std::string cells;
    cells.reserve(expected_leaves * 4);
    while (!pending.empty()) {
        const auto *value = pending.back();
        pending.pop_back();
        if (const auto *list = std::get_if<std::shared_ptr<const LslList>>(value)) {
            check(*list && (*list)->items.size() == 2 && identities.insert(list->get()).second,
                  "Direct DAG import changed List arity or retained a reference alias");
            for (auto item = (*list)->items.rbegin(); item != (*list)->items.rend(); ++item)
                pending.push_back(&*item);
        } else {
            const auto *integer = std::get_if<std::int32_t>(value);
            check(integer && *integer == 1, "Direct DAG import changed original System.Int32 leaf");
            const auto bits = std::bit_cast<std::uint32_t>(*integer);
            for (unsigned index = 0; index < 4; ++index)
                cells.push_back(static_cast<char>((bits >> (index * 8)) & 255));
            ++leaves;
        }
    }
    check(identities.size() == expected_lists && leaves == expected_leaves,
          "Direct DAG import lost original Lists or leaves");
    check(framework::sha256_hex(cells) == expected_hash,
          "Direct DAG import changed complete original leaf fingerprint");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "Expected original DAG capacity fixture directory");
        const std::filesystem::path root = argv[1];
        auto xml = verified_original_xml(root);
        const auto proof = network::parse_osd_json(read(root / "verification.json"));
        verify_native_import(std::move(xml), proof.get("expectedLeafCellsSha256").text);
        std::cout << "PASS direct original Windows/Linux DAG XML import: 1048575 unique Lists, "
                     "1048576 Int32 leaves, all-cell hash and XML alias splitting; identical input deduplicated\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
