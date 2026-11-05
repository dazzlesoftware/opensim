// .NET8.0.31 XmlDownloadManager file input; MIT .NET license.
#pragma once
#include "opensim/framework/nini_xml_reader.hpp"
namespace opensim::framework {
// Already resolved Uri.LocalPath, not raw URI text. Opens immediately when
// invoked by the deferred first-Read entity provider. Open/Read/Share.Read,
// bufferSize1 and live CanSeek/Length; synchronous only.
std::shared_ptr<NativeNiniXmlByteInput> native_nini_xml_file_input(NiniWriterText,std::optional<bool> disable_file_locking={});
NiniWriterText native_nini_xml_full_path(NiniWriterText);
}
