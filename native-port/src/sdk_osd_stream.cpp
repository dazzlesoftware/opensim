// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/sdk_osd_stream.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/network/sdk_llsd_binary.hpp"
#include "opensim/network/sdk_llsd_notation.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
namespace opensim::network {
namespace {
enum class Format { Xml, Notation, Binary, Json };
Format sniff(std::string_view prefix, bool ignore_case) {
    auto starts =
        ignore_case ? framework::invariant_starts_with_ignore_case : framework::invariant_starts_with;
    if (starts(prefix, "<llsd>") || starts(prefix, "<?xml") || starts(prefix, "<? llsd/xml"))
        return Format::Xml;
    if (starts(prefix, "<? llsd/notatio"))
        return Format::Notation;
    if (starts(prefix, "<? llsd/binary"))
        return Format::Binary;
    return Format::Json;
}
std::string ascii_prefix(std::string_view bytes, bool padded) {
    std::string prefix(padded ? 15 : std::min<std::size_t>(15, bytes.size()), '\0');
    for (std::size_t i = 0; i < prefix.size() && i < bytes.size(); ++i) {
        auto c = static_cast<unsigned char>(bytes[i]);
        prefix[i] = c < 128 ? static_cast<char>(c) : '?';
    }
    return prefix;
}
}
Value deserialize_sdk_osd_text(std::u16string_view text) {
    switch (sniff(framework::utf8(text), true)) {
    case Format::Xml:
        return deserialize_sdk_llsd_xml_text(text);
    case Format::Notation:
        return deserialize_sdk_llsd_notation_text(text);
    case Format::Binary:
        return deserialize_sdk_llsd_binary(framework::utf8(text));
    case Format::Json:
        return parse_osd_json_utf16(text);
    }
    return {};
}
Value deserialize_sdk_osd_bytes(std::string_view bytes) {
    switch (sniff(ascii_prefix(bytes, false), true)) {
    case Format::Xml:
        return deserialize_sdk_llsd_xml(bytes);
    case Format::Notation:
        return deserialize_sdk_llsd_notation(bytes);
    case Format::Binary:
        return deserialize_sdk_llsd_binary(bytes);
    case Format::Json:
        return parse_osd_json(bytes);
    }
    return {};
}
Value deserialize_sdk_osd_stream(std::string_view bytes, bool seekable) {
    if (!seekable)
        throw std::runtime_error("Cannot deserialize structured data from unseekable streams");
    switch (sniff(ascii_prefix(bytes, true), false)) {
    case Format::Xml:
        return deserialize_sdk_llsd_xml(bytes);
    case Format::Notation:
        return deserialize_sdk_llsd_notation(bytes);
    case Format::Binary:
        return deserialize_sdk_llsd_binary(bytes);
    case Format::Json:
        break;
    }
    auto decoded = framework::decode_stream_reader_text(
        {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
    return parse_osd_json_utf16(decoded.text);
}
}
