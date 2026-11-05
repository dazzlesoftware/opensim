// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/xml_rpc_requester.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
namespace opensim::network {
namespace {
// XElement.ToString uses XmlWriterSettings.NewLineHandling.Replace. The
// XmlReader has already normalized literal XML line breaks; character
// references can still introduce CR and CRLF into the resulting text node.
void append_text(std::u16string &out, std::u16string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = text[i];
        switch (c) {
        case u'&':
            out += u"&amp;";
            break;
        case u'<':
            out += u"&lt;";
            break;
        case u'>':
            out += u"&gt;";
            break;
        case u'\r':
            if (i + 1 < text.size() && text[i + 1] == u'\n')
                ++i;
            [[fallthrough]];
        case u'\n':
#ifdef _WIN32
            out += u"\r\n";
#else
            out += u'\n';
#endif
            break;
        default:
            out += c;
            break;
        }
    }
}
}
std::u16string compact_nwc_xml_rpc_diagnostic(const std::u16string &xml) {
    framework::NiniXmlReaderSettings settings;
    settings.ignore_whitespace = true;
    settings.dtd_processing = framework::NiniXmlDtdProcessing::Parse;
    auto reader = framework::NativeNiniXmlReader::from_text(framework::nini_writer_text(xml), settings);
    std::u16string output;
    bool root = false, inside = false;
    while (reader->read()) {
        const auto &t = reader->token();
        if (t.node_type == 1) {
            // The bundled Nwc serializers never emit attributes or namespaces. Keep
            // unsupported diagnostic writers explicit instead of corrupting XML.
            if (!t.attributes.empty() || !t.prefix.empty() || !t.namespace_uri.empty())
                throw std::invalid_argument(
                    "Nwc diagnostic XML unexpectedly contains attributes or namespaces");
            if (t.depth == 0) {
                root = true;
                inside = true;
            }
            output += u'<';
            output += t.name;
            if (t.is_empty_element) {
                output += u" />";
                if (t.depth == 0)
                    inside = false;
            } else
                output += u'>';
        } else if (t.node_type == 15) {
            output += u"</";
            output += t.name;
            output += u'>';
            if (t.depth == 0)
                inside = false;
        } else if (inside && (t.node_type == 3 || t.node_type == 13 || t.node_type == 14)) {
            append_text(output, t.value);
        } else if (inside && t.node_type == 4) {
            output += u"<![CDATA[";
            output += t.value;
            output += u"]]>";
        } else if (inside && t.node_type == 8) {
            output += u"<!--";
            output += t.value;
            output += u"-->";
        } else if (inside && t.node_type == 7) {
            output += u"<?";
            output += t.name;
            if (!t.value.empty()) {
                output += u' ';
                output += t.value;
            }
            output += u"?>";
        }
    }
    reader->close();
    if (!root)
        throw NwcXmlRpcError("System.Xml.XmlException", u"Root element is missing.");
    return output;
}
void xml_rpc_log_detail(const std::function<std::int32_t()> &level,
                        const std::function<void(const std::u16string &)> &debug,
                        const std::u16string &context, const std::u16string &output) {
    // The property getter is evaluated even for short output, just as in C#.
    const bool truncate = level() >= 5 && output.size() > 200;
    debug(u"[LOGHTTP]: " + context + u" " +
          xml_rpc_binary_to_ascii(truncate ? output.substr(0, 200) : output));
}
}
