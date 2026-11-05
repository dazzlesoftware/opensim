#define main original_save_probe_main
#include "nini_xml_document_save_tests.cpp"
#undef main
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int action, nth, mode;
        std::string xml;
        in >> action >> nth >> mode >> xml;
        NiniXmlDom doc;
        auto settings = NiniXmlReaderSettings::legacy_document();
        settings.dtd_processing = NiniXmlDtdProcessing::Parse;
        auto reader = NativeNiniXmlReader::from_text(decode(xml), settings);
        native_nini_xml_load(doc, reader);
        auto root = doc.document_element();
        auto sink = std::make_shared<NiniStringWriter>();
        auto io = native_xml_string_writer_io(sink);
        int seen = 0;
        bool fired = false;
        auto hook = [&] {
            if (++seen != nth)
                return;
            fired = true;
            switch (action) {
            case 9:
                if (auto a = root->attribute(nini_writer_text(u"a")))
                    a->append_child(doc.create_text(nini_writer_text(u"appended")));
                break;
            case 10:
                if (auto a =
                        std::dynamic_pointer_cast<NiniXmlDomNode>(root->attribute(nini_writer_text(u"a"))))
                    if (auto c = a->first_child())
                        a->remove_child(c);
                break;
            case 11:
                if (auto a = root->attribute(nini_writer_text(u"a")))
                    a->append_child(doc.create_fragment());
                break;
            case 12:
                if (auto a =
                        std::dynamic_pointer_cast<NiniXmlDomNode>(root->attribute(nini_writer_text(u"a"))))
                    if (auto c = a->first_child())
                        c->value(nini_writer_text(u"child"));
                break;
            case 8:
                throw XmlTextWriterError("System.IO.IOException", 0x80131620, u"writer fault");
            case 0:
                if (auto c = root->first_child())
                    root->remove_child(c);
                break;
            case 1:
                root->append_child(doc.create_element(nini_writer_text(u"added")));
                break;
            case 2:
                doc.clear();
                break;
            case 3:
                root->is_empty_element(true);
                break;
            case 4: {
                auto a = doc.create_attribute(nini_writer_text(u"extra"));
                a->value(nini_writer_text(u"new<&"));
                root->append_attribute(a);
                break;
            }
            case 5:
                if (auto a = root->attribute(nini_writer_text(u"a")))
                    a->value(nini_writer_text(u"changed"));
                break;
            case 6:
                if (auto c = root->last_child())
                    c->value(nini_writer_text(u"changed"));
                break;
            case 7:
                if (auto c = root->first_child())
                    if (c->node_type() == 1)
                        c->append_child(doc.create_text(nini_writer_text(u"new")));
                break;
            }
        };
        auto w = io.write;
        io.write = [&, w](auto kind, auto text) {
            hook();
            w(kind, text);
        };
        auto a = io.write_array;
        io.write_array = [&, a](auto array, int index, int count) {
            hook();
            a(array, index, count);
        };
        auto b = io.write_whole_array;
        io.write_whole_array = [&, b](auto array) {
            hook();
            b(array);
        };
        auto l = io.write_line;
        io.write_line = [&, l] {
            hook();
            l();
        };
        auto f = io.flush;
        io.flush = [&, f] {
            hook();
            f();
        };
        NativeXmlTextWriter writer(io, true);
        writer.formatting(1);
        std::string failure = "-";
        try {
            if (mode == 0)
                native_xml_document_save_to_writer(doc, writer);
            else {
                native_xml_document_write_to(doc, writer);
                writer.flush();
            }
        } catch (...) {
            failure = current_error();
        }
        std::cout << hex(sink->to_string()) << ';' << failure << ';' << writer.write_state() << ';' << seen
                  << ';' << fired << '\n';
    }
}
