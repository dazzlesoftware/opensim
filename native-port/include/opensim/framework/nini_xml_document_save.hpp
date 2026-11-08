// Native consumed XmlDocument.WriteTo/Save and Nini writer composition, runtime8.0.31.
// MIT runtime license: native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/nini_xml_native_document.hpp"
#include "opensim/framework/nini_xml_stream_writer.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/nini_xml_uri_reader.hpp"
namespace opensim::framework {
void native_xml_node_write_to(const std::shared_ptr<NiniXmlDomNode>&, NativeXmlTextWriter&);
void native_xml_document_write_to(NiniXmlDom&, NativeXmlTextWriter&);
void native_xml_document_save_to_writer(NiniXmlDom&, NativeXmlTextWriter&);
void native_xml_document_save_to_text_io(NiniXmlDom&, XmlTextWriterIo, bool preserve_whitespace);
XmlTextWriterIo native_xml_string_writer_io(const std::shared_ptr<NiniStringWriter>&);
using XmlStreamEncodingResolver = std::function<std::shared_ptr<XmlStreamEncoding>(NiniWriterText)>;
void native_xml_document_save_to_path(NiniXmlDom&, NiniWriterText, bool, const NiniFileStreamProvider&,
                                      const XmlStreamEncodingResolver&);
void native_xml_document_save_to_stream(NiniXmlDom&, std::shared_ptr<NiniByteStream>, bool,
                                        const XmlStreamEncodingResolver&);
using NiniXmlForeignWriterIo = std::function<XmlTextWriterIo(std::shared_ptr<NiniTextWriter>)>;
// Requires the real URI/resolver operation graph; never replaces reads or Save
// with permissive/no-op callbacks. Native string/byte writers are concrete.
std::shared_ptr<NiniXmlNativeDocumentIo> native_xml_document_io(
    std::shared_ptr<NiniXmlUriReaderBackend>, NiniFileStreamProvider = native_nini_file_stream_provider(),
    XmlStreamEncodingResolver = native_xml_intrinsic_encoding, NiniXmlForeignWriterIo = {});
} // namespace opensim::framework
