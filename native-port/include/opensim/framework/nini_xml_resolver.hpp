// Consumed XmlResolver.ResolveUri and XmlDownloadManager dispatch, runtime8.0.31.
// Copyright .NET Foundation and Contributors; MIT .NET license.
#pragma once
#include "opensim/framework/nini_xml_uri_reader.hpp"
#include "opensim/framework/config_uri_object.hpp"
namespace opensim::framework {
class NiniXmlResolverPolicy;
ConfigUriObjectPtr native_nini_xml_resolve_uri(ConfigUriObjectPtr, NiniWriterText,
                                               const std::shared_ptr<ConfigUriObjectBackend>&);
// The common native URI operations and non-file HTTP transport are required;
// file entities use the actual synchronous FileStream1 implementation.
std::shared_ptr<NiniXmlUriReaderBackend> native_nini_xml_url_reader_backend(
    std::shared_ptr<ConfigUriObjectBackend>,
    std::function<std::shared_ptr<NativeNiniXmlByteInput>(ConfigUriObjectPtr)> nonfile_entity,
    std::optional<bool> disable_file_locking = {});
// One policy owner is shared across readers. The optional explicit resolver
// getter models the reader field; legacy filename construction starts null.
std::shared_ptr<NiniXmlUriReaderBackend> native_nini_xml_default_uri_reader_backend(
    std::shared_ptr<ConfigUriObjectBackend>, std::shared_ptr<NiniXmlResolverPolicy>,
    std::function<std::shared_ptr<NativeNiniXmlByteInput>(ConfigUriObjectPtr)>,
    std::optional<bool> disable_file_locking = {},
    std::function<std::shared_ptr<NiniXmlUriReaderResolver>()> explicit_reader_resolver = {});
}
