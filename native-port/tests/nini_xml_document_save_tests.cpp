#include "opensim/framework/nini_xml_document_save.hpp"
#include "opensim/framework/nini_config_invariant.hpp"

#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_xml_native_document.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
std::string hex(const NiniWriterText& s) {
    if (!s)
        return "~";
    if (s->empty())
        return "-";
    std::string out;
    for (auto c : *s)
        for (int shift = 12; shift >= 0; shift -= 4)
            out += "0123456789abcdef"[(c >> shift) & 15];
    return out;
}
NiniWriterText decode(const std::string& s) {
    if (s == "~")
        return {};
    std::u16string out;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            out += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return nini_writer_text(out);
}
std::string error(const std::string& type, unsigned code, const std::u16string& message,
                  const std::optional<std::u16string>& parameter = {}) {
    std::ostringstream h;
    h << std::hex << code;
    return type + ":" + h.str() + ":" + hex(nini_writer_text(message)) + ":" +
           hex(nini_writer_text(parameter));
}
std::string current_error() {
    try {
        throw;
    } catch (const NiniXmlProviderError& e) {
        return error(e.source_type, e.hresult, e.message, e.parameter);
    } catch (const NiniConfigError& e) {
        return error(e.source_type(), e.hresult(), e.message, e.parameter);
    } catch (const XmlTextWriterError& e) {
        return error(e.source_type, e.hresult, e.message, e.parameter);
    } catch (const XmlEncoderError& e) {
        return error(e.source_type, e.hresult, e.message);
    } catch (const XmlEncoderArrayError& e) {
        return error(e.source_type, e.hresult, e.message, e.parameter);
    } catch (const NiniStreamError& e) {
        return error(e.source_type, static_cast<unsigned>(e.hresult), e.message, e.parameter);
    } catch (const NiniWriterError& e) {
        return error(e.source_type(), static_cast<unsigned>(e.hresult()), e.message, e.parameter);
    }
}
int main(int argc, char**) {
    if (argc == 1) {
        auto uri = std::make_shared<NiniXmlUriReaderBackend>();
        uri->get_temp_resolver = []() -> std::shared_ptr<NiniXmlUriReaderResolver> {
            throw std::logic_error("No URI read in this string Save unit");
        };
        auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
        auto source =
            NiniXmlConfigSource::create(arena, native_nini_xml_config_backend(native_xml_document_io(uri)));
        source->add_config(nini_writer_text(u"s"))->set_text(nini_writer_text(u"k"), nini_writer_text(u"<&"));
        auto text = source->to_string();
        if (!text || text->find(u"Value=\"&lt;&amp;\"") == text->npos)
            throw std::runtime_error("Concrete Nini XML Save/escaping unit");
        NiniXmlDom empty;
        try {
            native_xml_document_save_to_path(empty, {}, false, native_nini_file_stream_provider(),
                                             native_xml_intrinsic_encoding);
            throw std::runtime_error("Missing root check before path validation");
        } catch (const NiniXmlProviderError& e) {
            if (e.source_type != "System.Xml.XmlException" || e.line_number != 0 || e.line_position != 0)
                throw;
        }
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, preserve, closed;
        std::string newline, xml;
        in >> mode >> preserve >> closed >> newline >> xml;
        auto sink = std::make_shared<NiniStringWriter>();
        sink->new_line(decode(newline));
        if (closed == 1)
            sink->close();
        NiniXmlDom document;
        std::string failure = "-";
        if (mode >= 6) {
            NiniWriterText output;
            try {
                auto uri = std::make_shared<NiniXmlUriReaderBackend>();
                uri->get_temp_resolver = []() -> std::shared_ptr<NiniXmlUriReaderResolver> {
                    throw std::logic_error("No filename load is requested in this reader-based fixture");
                };
                auto io = native_xml_document_io(uri);
                auto backend = native_nini_xml_config_backend(io);
                auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
                auto reader = NativeNiniXmlReader::from_text(decode(xml));
                auto source = NiniXmlConfigSource::create(arena, backend, reader);
                if (mode == 7) {
                    auto name = nini_writer_text(u"Added");
                    auto config = source->configs()->at(name);
                    if (!config)
                        config = source->add_config(name);
                    config->set_text(nini_writer_text(u"Escaped"), nini_writer_text(u"<&>\"'\r\n"));
                }
                if (mode == 8)
                    source->configs()->clear();
                if (mode == 9) {
                    auto other = NiniXmlConfigSource::create(arena, backend);
                    other->add_config(nini_writer_text(u"Merge"))
                        ->set_text(nini_writer_text(u"from"), nini_writer_text(u"other"));
                    source->merge(other);
                }
                output = source->to_string();
            } catch (...) {
                failure = current_error();
            }
            std::cout << hex(output) << ';' << failure << ";open\n";
            continue;
        }
        try {
            if (xml == "decl1" || xml == "declbad") {
                document.document_node()->append_child(document.create_declaration(
                    nini_writer_text(u"1.1"),
                    nini_writer_text(xml == "decl1" ? u"utf-8" : u"not-an-encoding"),
                    nini_writer_text(u"yes")));
            } else if (xml != "empty") {
                auto settings = NiniXmlReaderSettings::legacy_document();
                settings.dtd_processing = NiniXmlDtdProcessing::Parse;
                auto reader = NativeNiniXmlReader::from_text(decode(xml), settings);
                try {
                    native_nini_xml_load(document, reader, preserve != 0);
                } catch (...) {
                    reader->close();
                    throw;
                }
                reader->close();
            }
            auto io = native_xml_string_writer_io(closed < 0 ? nullptr : sink);
            if (mode == 0)
                native_xml_document_save_to_text_io(document, io, preserve != 0);
            else {
                NativeXmlTextWriter writer(io, true);
                if (!preserve)
                    writer.formatting(1);
                if (mode == 2)
                    writer.write_comment(u"before");
                if (mode == 3)
                    writer.write_start_document();
                if (mode == 4)
                    writer.close();
                if (mode == 1) {
                    native_xml_document_write_to(document, writer);
                    writer.flush();
                } else
                    native_xml_document_save_to_writer(document, writer);
            }
        } catch (...) {
            failure = current_error();
        }
        auto before = sink->to_string();
        std::string after = "open";
        try {
            sink->write(nini_writer_text(u"!"));
        } catch (...) {
            after = current_error();
        }
        std::cout << hex(before) << ';' << failure << ';' << after << '\n';
    }
    return 0;
}
