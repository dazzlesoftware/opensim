#include "opensim/framework/nini_xml_document_save.hpp"
namespace opensim::framework {
namespace {
using Node = std::shared_ptr<NiniXmlDomNode>;
[[noreturn]] void null_reference() {
    throw XmlTextWriterError("System.NullReferenceException", 0x80004003,
                             u"Object reference not set to an instance of an object.");
}
XmlWriterText value(const NiniWriterText& text) {
    return text ? XmlWriterText(*text) : std::nullopt;
}
bool same(const Node& a, const Node& b) {
    return !a || !b ? a == b : a->same_owner(*b) && a->identity() == b->identity();
}
Node required(Node node) {
    if (!node)
        null_reference();
    return node;
}
void content(const Node& node, NativeXmlTextWriter& writer) {
    for (auto current = node->first_child(); current; current = current->next_sibling())
        native_xml_node_write_to(current, writer);
}
void start_element(const Node& element, NativeXmlTextWriter& writer) {
    auto prefix = element->prefix(), local = element->local_name(), uri = element->namespace_uri();
    writer.write_start_element(value(prefix), value(local), value(uri));
    // Source holds the live collection, then rereads Count and Item at each step.
    if (!element->attributes().empty())
        for (std::size_t i = 0;; ++i) {
            auto attrs = element->attributes();
            if (i >= attrs.size())
                break;
            native_xml_node_write_to(attrs[i], writer);
        }
}
void element(const Node& root, NativeXmlTextWriter& writer) {
    auto current = root;
    for (;;) {
        required(current);
        if (current->node_type() == 1) {
            start_element(current, writer);
            if (current->is_empty_element())
                writer.write_end_element();
            else {
                auto first = current->first_child();
                if (first) {
                    current = first;
                    continue;
                }
                writer.write_full_end_element();
            }
        } else
            native_xml_node_write_to(current, writer);
        while (!same(current, root) && same(current, required(current->parent())->last_child())) {
            current = current->parent();
            writer.write_full_end_element();
        }
        if (!same(current, root)) {
            current = current->next_sibling();
            continue;
        }
        break;
    }
}
}
void native_xml_node_write_to(const std::shared_ptr<NiniXmlDomNode>& input, NativeXmlTextWriter& writer) {
    auto node = required(input);
    switch (node->node_type()) {
    case 1:
        element(node, writer);
        break;
    case 2: {
        if (!node->specified())
            break;
        auto prefix = node->prefix(), local = node->local_name(), uri = node->namespace_uri();
        writer.write_start_attribute(value(prefix), value(local), value(uri));
        content(node, writer);
        writer.write_end_attribute();
        break;
    }
    case 3:
        writer.write_string(value(node->value()));
        break;
    case 4:
        writer.write_cdata(value(node->value()));
        break;
    case 5:
        writer.write_entity_ref(value(node->name()));
        break;
    case 7:
    case 17:
        writer.write_processing_instruction(value(node->name()), value(node->value()));
        break;
    case 8:
        writer.write_comment(value(node->value()));
        break;
    case 9: {
        auto iterator = node->children();
        while (iterator->move_next())
            native_xml_node_write_to(std::dynamic_pointer_cast<NiniXmlDomNode>(iterator->current()), writer);
        break;
    }
    case 10:
        writer.write_doctype(value(node->name()), value(node->dtd_public_id()), value(node->dtd_system_id()),
                             value(node->dtd_subset()));
        break;
    case 11:
        content(node, writer);
        break;
    case 13:
    case 14:
        writer.write_whitespace(value(node->value()));
        break;
    default:
        throw std::logic_error("Native DOM node kind requires its serialization dependency");
    }
}
void native_xml_document_write_to(NiniXmlDom& document, NativeXmlTextWriter& writer) {
    auto scope = document.defer_collection();
    native_xml_node_write_to(document.document_node(), writer);
}
void native_xml_document_save_to_writer(NiniXmlDom& document, NativeXmlTextWriter& writer) {
    auto scope = document.defer_collection();
    auto current = document.document_node()->first_child();
    if (!current)
        return;
    if (writer.write_state() == 0) {
        if (current->node_type() == 17) {
            auto standalone = current->declaration_standalone();
            if (!standalone || standalone->empty())
                writer.write_start_document();
            else if (*standalone == u"yes")
                writer.write_start_document(1);
            else if (*standalone == u"no")
                writer.write_start_document(0);
            current = current->next_sibling();
        } else
            writer.write_start_document();
    }
    while (current) {
        native_xml_node_write_to(current, writer);
        current = current->next_sibling();
    }
    writer.flush();
}
void native_xml_document_save_to_text_io(NiniXmlDom& document, XmlTextWriterIo io, bool preserve_whitespace) {
    NativeXmlTextWriter writer(std::move(io), true);
    if (!preserve_whitespace)
        writer.formatting(1);
    native_xml_document_save_to_writer(document, writer);
}
XmlTextWriterIo native_xml_string_writer_io(const std::shared_ptr<NiniStringWriter>& writer) {
    XmlTextWriterIo io;
    auto require = [writer] {
        if (!writer)
            null_reference();
    };
    io.encoding = [require] {
        require();
        auto info = std::make_shared<NativeXmlTextEncoding>();
        info->web_name = [] { return XmlWriterText(u"utf-16"); };
        return info;
    };
    io.write = [writer, require](XmlEncoderWriteKind, std::optional<std::u16string_view> text) {
        require();
        writer->write(text ? nini_writer_text(std::u16string(*text)) : NiniWriterText{});
    };
    io.write_array = [writer, require](XmlWriterCharArrayHandle array, int index, int count) {
        require();
        if (!array)
            throw XmlTextWriterError("System.ArgumentNullException", 0x80004003,
                                     u"Value cannot be null. (Parameter 'buffer')", u"buffer");
#ifdef _WIN32
        constexpr auto newline = u"\r\n";
#else
        constexpr auto newline = u"\n";
#endif
        auto negative = [&](int value, std::u16string parameter) {
            if (value < 0) {
                auto decimal = std::to_string(value);
                std::u16string number(decimal.begin(), decimal.end());
                throw XmlEncoderArrayError("System.ArgumentOutOfRangeException", 0x80131502,
                                           parameter + u" ('" + number +
                                               u"') must be a non-negative value. (Parameter '" + parameter +
                                               u"')" + newline + u"Actual value was " + number + u".",
                                           parameter, value);
            }
        };
        negative(index, u"index");
        negative(count, u"count");
        if (static_cast<int>(array->size()) - index < count)
            throw XmlTextWriterError(
                "System.ArgumentException", 0x80070057,
                u"Offset and length were out of bounds for the array or count is greater than the number of elements from index to the end of the source collection.");
        writer->write(nini_writer_text(std::u16string(array->view().substr(index, count))));
    };
    io.write_whole_array = [array = io.write_array, require](XmlWriterCharArrayHandle value) {
        require();
        if (value)
            array(value, 0, static_cast<int>(value->size()));
    };
    io.write_line = [writer, require] {
        require();
        writer->write(writer->new_line());
    };
    io.flush = [writer, require] {
        require();
        writer->flush();
    };
    io.dispose = [writer, require] {
        require();
        writer->close();
    };
    return io;
}
}
namespace opensim::framework {
void native_xml_document_save_to_path(NiniXmlDom& document, NiniWriterText path, bool preserve,
                                      const NiniFileStreamProvider& file,
                                      const XmlStreamEncodingResolver& resolver) {
    if (!document.document_element()) {
        NiniXmlProviderError error("System.Xml.XmlException", 0x80131940,
                                   u"Invalid XML document. The document does not have a root element.");
        error.source_uri = u"";
        error.line_number = 0;
        error.line_position = 0;
        throw error;
    }
    std::shared_ptr<XmlStreamEncoding> encoding;
    auto declaration = document.document_node()->first_child();
    if (declaration && declaration->node_type() == 17) {
        auto name = declaration->declaration_encoding();
        if (name && !name->empty()) {
            if (!resolver)
                throw std::invalid_argument(
                    "Named XML output encoding requires its exact Encoding.GetEncoding provider");
            encoding = resolver(name);
        }
    }
    if (!file)
        throw std::invalid_argument(
            "XML file Save requires the exact native FileStream constructor provider");
    auto stream = file(path, 2, 2, 1, 4096, 0);
    NativeXmlTextWriter writer(native_xml_stream_io(std::move(stream), std::move(encoding)), true);
    try {
        if (!preserve)
            writer.formatting(1);
        native_xml_document_write_to(document, writer);
        writer.flush();
    } catch (...) {
        writer.close();
        throw;
    }
    writer.close();
}
void native_xml_document_save_to_stream(NiniXmlDom& document, std::shared_ptr<NiniByteStream> stream,
                                        bool preserve, const XmlStreamEncodingResolver& resolver) {
    std::shared_ptr<XmlStreamEncoding> encoding;
    auto declaration = document.document_node()->first_child();
    if (declaration && declaration->node_type() == 17) {
        auto name = declaration->declaration_encoding();
        if (name && !name->empty()) {
            if (!resolver)
                throw std::invalid_argument(
                    "Named XML output encoding requires its exact Encoding.GetEncoding provider");
            encoding = resolver(name);
        }
    }
    NativeXmlTextWriter writer(native_xml_stream_io(std::move(stream), std::move(encoding)), true);
    if (!preserve)
        writer.formatting(1);
    native_xml_document_write_to(document, writer);
    writer.flush();
}
}
namespace opensim::framework {
std::shared_ptr<NiniXmlNativeDocumentIo> native_xml_document_io(std::shared_ptr<NiniXmlUriReaderBackend> uri,
                                                                NiniFileStreamProvider file,
                                                                XmlStreamEncodingResolver encodings,
                                                                NiniXmlForeignWriterIo foreign) {
    if (!file || !encodings)
        throw std::invalid_argument("Native XML document requires real file/encoding operations");
    auto io = std::make_shared<NiniXmlNativeDocumentIo>();
    io->open_path = native_nini_xml_uri_reader_factory(uri);
    io->save_path = [file, encodings](NiniXmlDom& dom, NiniWriterText path) {
        native_xml_document_save_to_path(dom, std::move(path), false, file, encodings);
    };
    io->save_stream = [encodings](NiniXmlDom& dom, std::shared_ptr<NiniWriterStream> stream) {
        auto bytes = std::dynamic_pointer_cast<NiniByteStream>(stream);
        if (stream && !bytes)
            throw std::invalid_argument("Native XML stream Save requires full byte-stream operations");
        native_xml_document_save_to_stream(dom, std::move(bytes), false, encodings);
    };
    io->save_writer = [foreign](NiniXmlDom& dom, std::shared_ptr<NiniTextWriter> writer) {
        XmlTextWriterIo output;
        if (!writer)
            output = native_xml_string_writer_io({});
        else if (auto string = std::dynamic_pointer_cast<NiniStringWriter>(writer))
            output = native_xml_string_writer_io(string);
        else if (auto bytes = std::dynamic_pointer_cast<NativeXmlStreamWriter>(writer))
            output = native_xml_stream_text_io(bytes);
        else {
            if (!foreign)
                throw std::invalid_argument(
                    "Foreign TextWriter requires its exact virtual encoding/overload adapter");
            output = foreign(writer);
        }
        native_xml_document_save_to_text_io(dom, std::move(output), false);
    };
    return io;
}
}
