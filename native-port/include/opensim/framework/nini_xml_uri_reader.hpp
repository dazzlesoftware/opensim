// Consumed synchronous XmlTextReader(filename) / XmlDocument.Load(path).
// .NET runtime v8.0.31; MIT third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/nini_xml_config.hpp"
namespace opensim::framework {
class NativeNiniXmlReader;
class NativeNiniXmlByteInput;
class ConfigUriObject;
// This boundary retains the actual resolved URI object. ToString is a distinct
// required operation: AbsoluteUri or HTTP URL serialization is not equivalent.
struct NiniXmlReaderUri {
    std::shared_ptr<const void> identity;
    std::shared_ptr<const ConfigUriObject> config_uri;
    std::function<NiniWriterText()> to_string;
};
class NiniXmlUriReaderResolver {
public:
    virtual ~NiniXmlUriReaderResolver() = default;
    virtual std::shared_ptr<NiniXmlReaderUri> resolve_uri(NiniWriterText relative) = 0;
    // Returned input supplies real CanSeek/Length and ownership, including the
    // seekable buffered MemoryStream returned by XmlDownloadManager HTTP paths.
    virtual std::shared_ptr<NativeNiniXmlByteInput> get_entity(std::shared_ptr<NiniXmlReaderUri>) = 0;
};
struct NiniXmlUriReaderBackend {
    std::function<std::shared_ptr<NiniXmlUriReaderResolver>()> get_temp_resolver;
};
std::shared_ptr<NativeNiniXmlReader>
native_nini_xml_uri_reader(NiniWriterText, const std::shared_ptr<NiniXmlUriReaderBackend>&);
std::function<std::shared_ptr<NativeNiniXmlReader>(NiniWriterText)>
native_nini_xml_uri_reader_factory(const std::shared_ptr<NiniXmlUriReaderBackend>&);
}
