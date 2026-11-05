#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_xml_reader_dtd.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
std::u16string decode(std::string s) {
    std::u16string v;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            v += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return v;
}
std::string encode(std::u16string_view s) {
    if (s.empty())
        return "-";
    constexpr char h[] = "0123456789abcdef";
    std::string out;
    for (auto c : s)
        for (int b = 12; b >= 0; b -= 4)
            out += h[(c >> b) & 15];
    return out;
}
int main(int argc, char**) {
    if (argc == 1) {
        for (auto input : {u"&#65;", u"&amp;"}) {
            std::u16string raw(input);
            auto reader = NativeNiniXmlReader::from_text(nini_writer_text(raw));
            auto adapter = native_nini_xml_dtd_adapter(reader);
            (void)adapter->parsing_buffer();
            adapter->current_position(0);
            std::u16string builder;
            bool numeric = raw == u"&#65;";
            auto end = numeric ? adapter->parse_numeric_reference(&builder)
                               : adapter->parse_named_reference(true, &builder);
            auto buffer = adapter->parsing_buffer();
            if (end != 5 || builder != raw || adapter->current_position() != 4 ||
                buffer[4] != (numeric ? u'A' : u'&'))
                throw std::runtime_error("Native XML adapter reference/buffer unit failed");
        }
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        auto first = line.find('|'), last = line.rfind('|');
        auto input = decode(line.substr(0, first));
        auto op = line.substr(first + 1, last - first - 1);
        NiniXmlReaderSettings settings;
        if (op == "J")
            settings.max_characters_in_document = input.size() + 1;
        if (op == "L")
            settings.max_characters_from_entities =
                std::max<std::int64_t>(1, static_cast<std::int64_t>(input.size()) - 1);
        auto reader = NativeNiniXmlReader::from_text(nini_writer_text(input), settings);
        auto a = native_nini_xml_dtd_adapter(reader);
        auto position = std::stoi(line.substr(last + 1));
        while (position > 0 && a->parsing_buffer_length() < position + 2) {
            if (a->read_data() == 0)
                break;
        }
        a->current_position(position);
        (void)a->parsing_buffer();
        std::u16string builder;
        std::string result;
        try {
            std::string value;
            if (op == "Q")
                a->push_internal_dtd(nini_writer_text(u"urn:dtd"), {});
            else if (op == "H" || op == "J")
                a->push_internal_dtd(nini_writer_text(u"urn:dtd"), nini_writer_text(input));
            else if (op == "S" || op == "T" || op == "X" || op == "F" || op == "L" || op == "B" ||
                     op == "K") {
                auto e = std::make_shared<NiniXmlDtdEntity>();
                e->name.local_name = u"entity";
                e->text = nini_writer_text(input);
                e->line = op == "B" ? NiniXmlDtdLine{2147483647, 2147483647} : NiniXmlDtdLine{2, 3};
                e->declared_uri = nini_writer_text(u"urn:entity");
                e->external = op == "F";
                std::int32_t id = 0;
                auto pushed = a->push_entity(e, id);
                value = std::string(pushed ? "True" : "False") + ":" + std::to_string(id);
                if (op == "B")
                    a->parse_comment(&builder);
                if (op == "K")
                    reader->close();
                if (op == "X")
                    a->push_entity(e, id);
                if (op == "T") {
                    std::shared_ptr<NiniXmlDtdEntity> old;
                    auto popped = a->pop_entity(old, id);
                    value += "/" + std::string(popped ? "True" : "False") + ":" + std::to_string(id) + ":" +
                             (old == e ? "same" : "other");
                }
            } else if (op == "Z") {
                std::shared_ptr<NiniXmlDtdEntity> old;
                std::int32_t id;
                auto popped = a->pop_entity(old, id);
                value = std::string(popped ? "True" : "False") + ":" + std::to_string(id) + ":" +
                        (old ? "entity" : "null");
            } else if (op == "N")
                value = std::to_string(a->parse_numeric_reference(&builder));
            else if (op == "E" || op == "U")
                value = std::to_string(a->parse_named_reference(op == "E", &builder));
            else if (op == "C")
                a->parse_comment(&builder);
            else if (op == "P")
                a->parse_processing_instruction(&builder);
            else if (op == "R")
                value = std::to_string(a->read_data());
            result = "OK:" + value;
        } catch (const NiniXmlProviderError& e) {
            result = "E:" + e.source_type + ":" + std::to_string(e.hresult) + ":" + encode(e.message);
        }
        auto buffer = a->parsing_buffer();
        std::cout << result << '|' << encode(builder) << '|' << a->current_position() << ':'
                  << a->line_number() << ':' << a->line_start_position() << ':'
                  << (a->is_eof() ? "True" : "False") << ':'
                  << (a->entity_eol_normalized() ? "True" : "False") << ':' << a->entity_stack_length() << '|'
                  << (buffer.data() == nullptr
                          ? "~"
                          : encode(std::u16string_view(buffer.data(), a->parsing_buffer_length())))
                  << '\n';
        std::cout.flush();
    }
}
