// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/appearance_notecard.hpp"
#include "opensim/services/event_queue.hpp"
#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/text.hpp"

namespace opensim::services {
std::string appearance_notecard_xml(framework::AvatarAppearance &appearance, bool no_huds) {
    const auto raw = "<llsd>" + event_queue_inner_xml(appearance.pack_for_notecard(no_huds)) + "</llsd>";
    framework::NiniXmlDom document;
    framework::native_nini_xml_load(
        document, framework::NativeNiniXmlReader::from_text(framework::nini_writer_text(framework::utf16(raw)),
                                                            framework::NiniXmlReaderSettings::legacy_document()));
    auto output = std::make_shared<framework::NiniStringWriter>();
    framework::NativeXmlTextWriter writer(framework::native_xml_string_writer_io(output));
    writer.formatting(1);
    framework::native_xml_document_write_to(document, writer);
    writer.close();
    return framework::utf8(*output->to_string());
}

void persist_cached_appearance_bake(framework::Uuid texture, const std::shared_ptr<framework::IAssetCache> &cache,
                                    const std::function<void(const std::shared_ptr<framework::AssetBase> &)> &store) {
    if (!cache)
        return;
    std::shared_ptr<framework::AssetBase> asset;
    cache->get(texture.str(), asset);
    if (!asset || !asset->local())
        return;
    cache->expire(texture.str());
    asset->id(asset->full_id().str());
    asset->description("NPC BAKED");
    asset->temporary(false);
    asset->local(false);
    store(asset);
}
} // namespace opensim::services
