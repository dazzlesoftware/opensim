// Live download Stream -> source reader input bridge. No XML decoding occurs here.
#pragma once
#include "opensim/framework/config_xml_transport.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
namespace opensim::framework {
std::shared_ptr<NativeNiniXmlByteInput> config_xml_reader_stream(std::shared_ptr<ConfigXmlStream>);
}
