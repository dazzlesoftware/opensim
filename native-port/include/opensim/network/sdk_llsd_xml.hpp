// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
namespace opensim::network {
// Installed SDK OSDParser.DeserializeLLSDXml(Stream), including its catch-to-OSD
// boundary and XmlTextReader cursor behavior. XML encoding belongs to the reader.
Value deserialize_sdk_llsd_xml(std::string_view bytes);
Value deserialize_sdk_llsd_xml_text(std::u16string_view text);
// Raw SDK UTF16 projection. The existing UTF8 facade applies UTF8 replacement.
std::u16string osd_string_utf16(const Value&);
std::string sdk_map_key_bytes(std::u16string_view);
std::u16string sdk_map_key_utf16(std::string_view);
}
