#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_xml_reader_dtd.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
static std::u16string D(std::string s) {
    std::u16string r;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            r += static_cast<char16_t>(std::stoul(s.substr(i, 4), nullptr, 16));
    return r;
}
static std::string E(std::u16string_view s) {
    if (s.empty())
        return "-";
    std::string r;
    for (auto c : s)
        for (int b = 12; b >= 0; b -= 4)
            r += "0123456789abcdef"[(c >> b) & 15];
    return r;
}
static std::string E(const NiniWriterText& s) {
    return s ? E(std::u16string_view(*s)) : "~";
}
static std::string N(const NiniXmlDtdName& n) {
    return E(std::u16string_view(n.local_name)) + "," + E(std::u16string_view(n.prefix));
}
static const char* B(bool v) {
    return v ? "True" : "False";
}
static std::string T(const NiniXmlDtdTypedValue& v) {
    if (auto p = std::get_if<std::u16string>(&v))
        return "s" + E(std::u16string_view(*p));
    if (auto p = std::get_if<std::vector<std::u16string>>(&v)) {
        std::string r = "a";
        for (auto& s : *p) {
            if (r != "a")
                r += ",";
            r += E(std::u16string_view(s));
        }
        return r;
    }
    if (auto p = std::get_if<NiniXmlDtdName>(&v))
        return "q" + N(*p);
    return "~";
}
static std::string Info(const NiniXmlDtdInfo& info) {
    std::ostringstream b;
    b << N(info.name) << '|' << E(std::u16string_view(info.internal_subset)) << '|'
      << B(info.has_default_attributes) << ':' << B(info.has_non_cdata_attributes);
    constexpr const char* types[] = {"CDATA",   "ID",       "IDREF",    "IDREFS",      "ENTITY", "ENTITIES",
                                     "NMTOKEN", "NMTOKENS", "NOTATION", "ENUMERATION", "None"};
    for (auto* list : {&info.element_declarations, &info.undeclared_element_declarations}) {
        b << '|';
        for (auto& e : *list) {
            b << '[' << N(e->name) << ':' << B(e->has_non_cdata_attributes);
            for (auto& a : e->attributes) {
                b << ';' << N(a->name) << ':' << types[static_cast<int>(a->tokenized_type)] << ':'
                  << static_cast<int>(a->presence) << ':' << B(a->declared_in_external) << ':'
                  << B(a->is_xml_attribute) << ':' << a->line.number << ':' << a->line.position << ':'
                  << a->value_line.number << ':' << a->value_line.position << ':'
                  << E(std::u16string_view(a->default_value_expanded)) << ':' << T(a->default_value_typed)
                  << ':';
                for (std::size_t i = 0; i < a->values.size(); ++i) {
                    if (i)
                        b << ',';
                    b << E(std::u16string_view(a->values[i]));
                }
            }
            b << ']';
        }
    }
    for (auto* list : {&info.general_entities, &info.parameter_entities}) {
        b << '|';
        for (auto& e : *list)
            b << '[' << N(e->name) << ':' << N(e->ndata) << ':' << B(e->external) << ':'
              << B(e->declared_in_external) << ':' << B(e->parameter) << ':' << B(e->parsing_in_progress)
              << ':' << e->line.number << ':' << e->line.position << ':' << E(e->base_uri) << ':'
              << E(e->declared_uri) << ':' << E(e->system_id) << ':' << E(e->public_id) << ':' << E(e->text)
              << ']';
    }
    b << '|';
    for (auto& n : info.notations)
        b << '[' << N(n.name) << ':' << E(n.system_id) << ':' << E(n.public_id) << ']';
    return b.str();
}
#include <utility>
class TraceAdapter final : public NiniXmlDtdAdapter {
    std::unique_ptr<NiniXmlDtdAdapter> a_;
    std::string flags_;
    int returned_ = 0;
    int fail_;
    mutable int count_ = 0;
    std::optional<std::u16string> external_;

  public:
    mutable std::u16string trace;
    TraceAdapter(std::unique_ptr<NiniXmlDtdAdapter> a, std::string f, int n, std::optional<std::u16string> ex)
        : a_(std::move(a)), flags_(std::move(f)), fail_(n), external_(std::move(ex)) {
    }
    void mark(std::u16string name) const {
        if (!trace.empty())
            trace += u';';
        trace += name;
        if (++count_ == fail_) {
            auto text = u"fixture-" + name + u"-" + DNumber(count_);
            if (flags_.size() > 7 && flags_[7] == '1') {
                NiniXmlProviderError e("System.Xml.XmlException", 0x80131940u, std::move(text));
                e.line_number = 0;
                e.line_position = 0;
                throw e;
            }
            throw NiniXmlProviderError("System.InvalidOperationException", 0x80131509u, std::move(text));
        }
    }
    static std::u16string DUnsigned(std::uint32_t n) {
        auto s = std::to_string(n);
        return {s.begin(), s.end()};
    }
    static std::u16string DNumber(int n) {
        auto s = std::to_string(n);
        return {s.begin(), s.end()};
    }
    NiniWriterText intern(std::u16string_view s) override {
        mark(u"Intern");
        auto r = a_->intern(s);
        return flags_.size() > 6 && flags_[6] == '1' ? NiniWriterText{} : r;
    }
    NiniWriterText lookup_namespace(std::u16string_view s) override {
        mark(u"LookupNamespace");
        return a_->lookup_namespace(s);
    }
    NiniWriterText base_uri() const override {
        mark(u"get_BaseUri");
        return a_->base_uri();
    }
    std::span<char16_t> parsing_buffer() override {
        mark(u"get_ParsingBuffer");
        return a_->parsing_buffer();
    }
    int parsing_buffer_length() const override {
        mark(u"get_ParsingBufferLength");
        return a_->parsing_buffer_length();
    }
    int current_position() const override {
        mark(u"get_CurrentPosition");
        return a_->current_position();
    }
    void current_position(int n) override {
        mark(u"set_CurrentPosition");
        a_->current_position(n);
    }
    int line_number() const override {
        mark(u"get_LineNo");
        return a_->line_number();
    }
    int line_start_position() const override {
        mark(u"get_LineStartPosition");
        return a_->line_start_position();
    }
    bool is_eof() const override {
        mark(u"get_IsEof");
        return a_->is_eof();
    }
    int entity_stack_length() const override {
        mark(u"get_EntityStackLength");
        return a_->entity_stack_length();
    }
    bool entity_eol_normalized() const override {
        mark(u"get_IsEntityEolNormalized");
        return a_->entity_eol_normalized();
    }
    bool v1_compatibility() const override {
        mark(u"get_V1CompatibilityMode");
        return flags_[0] == '1';
    }
    bool normalization() const override {
        mark(u"get_Normalization");
        return flags_[1] == '1';
    }
    bool namespaces() const override {
        mark(u"get_Namespaces");
        return flags_[2] == '1';
    }
    bool validation_enabled() const override {
        mark(u"get_DtdValidation");
        return flags_[3] == '1';
    }
    bool has_validation_handler() const override {
        mark(u"get_ValidationEventHandling");
        return flags_[4] == '1';
    }
    int read_data() override {
        mark(u"ReadData");
        return a_->read_data();
    }
    void on_new_line(int p) override {
        mark(u"OnNewLine");
        a_->on_new_line(p);
        if (flags_.size() > 5 && flags_[5] == '1') {
            auto chars = a_->parsing_buffer();
            if (p < a_->parsing_buffer_length())
                chars[p] = u'X';
        }
    }
    int parse_numeric_reference(std::u16string* b) override {
        mark(u"ParseNumericCharRef");
        auto r = a_->parse_numeric_reference(b);
        if (b && flags_.size() > 5 && flags_[5] == '1')
            *b += u"!";
        return r;
    }
    int parse_named_reference(bool e, std::u16string* b) override {
        mark(u"ParseNamedCharRef");
        auto r = a_->parse_named_reference(e, b);
        if (b && flags_.size() > 5 && flags_[5] == '1')
            *b += u"!";
        return r;
    }
    void parse_processing_instruction(std::u16string* b) override {
        mark(u"ParsePI");
        a_->parse_processing_instruction(b);
        if (b && flags_.size() > 5 && flags_[5] == '1')
            *b += u"!";
    }
    void parse_comment(std::u16string* b) override {
        mark(u"ParseComment");
        a_->parse_comment(b);
        if (b && flags_.size() > 5 && flags_[5] == '1')
            *b += u"!";
    }
    bool push_entity(const std::shared_ptr<NiniXmlDtdEntity>& e, int& id) override {
        mark(u"PushEntity");
        if (external_ && e->external) {
            e->text = nini_writer_text(*external_);
            e->external = false;
            try {
                bool r = a_->push_entity(e, id);
                e->external = true;
                return r;
            } catch (...) {
                e->external = true;
                throw;
            }
        }
        return a_->push_entity(e, id);
    }
    bool pop_entity(std::shared_ptr<NiniXmlDtdEntity>& e, int& id) override {
        mark(u"PopEntity");
        return a_->pop_entity(e, id);
    }
    bool push_external_subset(NiniWriterText s, NiniWriterText p) override {
        mark(u"PushExternalSubset");
        if (external_) {
            a_->push_internal_dtd(nini_writer_text(u"urn:external"), nini_writer_text(*external_));
            return true;
        }
        return a_->push_external_subset(s, p);
    }
    void push_internal_dtd(NiniWriterText b, NiniWriterText s) override {
        mark(u"PushInternalDtd");
        a_->push_internal_dtd(b, s);
    }
    void on_system_id(NiniWriterText s, NiniXmlDtdLine k, NiniXmlDtdLine l) override {
        mark(u"OnSystemId");
        a_->on_system_id(s, k, l);
    }
    void on_public_id(NiniWriterText s, NiniXmlDtdLine k, NiniXmlDtdLine l) override {
        mark(u"OnPublicId");
        a_->on_public_id(s, k, l);
    }
    void validation_event(const NiniXmlProviderError& e, int severity) override {
        auto uri = e.source_uri ? E(std::u16string_view(*e.source_uri)) : "~";
        if (uri == "-")
            uri = "";
        mark(u"Validation:" + std::u16string(e.source_type.begin(), e.source_type.end()) + u":" +
             DUnsigned(e.hresult) + u":" + DNumber(e.line_number.value_or(0)) + u":" +
             DNumber(e.line_position.value_or(0)) + u":" + std::u16string(uri.begin(), uri.end()) + u":" +
             DNumber(severity) + u":" + e.message);
    }
    void throw_error(NiniXmlDtdError e) override {
        mark(u"Throw");
        if (flags_.size() > 8 && flags_[8] == '1' && returned_++ < (flags_.size() > 9 ? flags_[9] - '0' : 1))
            return;
        a_->throw_error(std::move(e));
    }
};
int main(int argc, char**) {
    if (argc == 1) {
        auto parse = [](std::u16string text, std::string flags = "01100") {
            auto reader = NativeNiniXmlReader::from_text(nini_writer_text(std::move(text)));
            TraceAdapter adapter(native_nini_xml_dtd_adapter(reader), std::move(flags), 0, {});
            return native_nini_xml_parse_dtd(adapter);
        };
        auto check = [](bool condition) {
            if (!condition)
                throw std::runtime_error("DTD public API unit failed");
        };
        auto info = parse(u"root [<!ELEMENT root EMPTY><!ATTLIST root n CDATA 'value'>]>");
        check(info->name.local_name == u"root" && info->element_declarations.size() == 1 &&
              info->element_declarations[0]->attributes[0]->default_value_expanded == u"value" &&
              info->has_default_attributes);
        info = parse(u"root [<!ATTLIST root n IDREFS 'a b' n CDATA 'ignored' m (a|b) 'b'>]>");
        auto element = info->undeclared_element_declarations[0];
        check(element->attributes.size() == 2 &&
              element->attributes[0]->tokenized_type == NiniXmlDtdTokenizedType::IdRef &&
              std::get<std::vector<std::u16string>>(element->attributes[0]->default_value_typed) ==
                  std::vector<std::u16string>{u"a", u"b"});
        info = parse(u"root [<!ENTITY x 'expanded'><!ATTLIST root a CDATA '&x;'>]>");
        check(info->general_entities.size() == 1 &&
              info->undeclared_element_declarations[0]->attributes[0]->default_value_expanded == u"expanded");
        info = parse(u"root [<!ATTLIST root a CDATA '&#32;'>]>", "011001");
        check(info->internal_subset.find(u'!') != std::u16string::npos &&
              info->undeclared_element_declarations[0]->attributes[0]->default_value_expanded == u" ");
        auto reader = NativeNiniXmlReader::from_text(nini_writer_text(u""));
        auto adapter = native_nini_xml_dtd_adapter(reader);
        info = native_nini_xml_parse_free_floating_dtd(*adapter, nini_writer_text(u""),
                                                       nini_writer_text(u"prefix:local"), {}, {}, {});
        check(info->name.local_name == u"prefix" && info->name.prefix == u"local");
        bool failed = false;
        try {
            parse(u"root [<!ATTLIST root xml:space (bad) 'bad'>]>", "01110");
        } catch (const NiniXmlProviderError& e) {
            failed = e.source_type == "System.NullReferenceException" && e.hresult == 0x80004003u;
        }
        check(failed);
        failed = false;
        try {
            parse(u"root [<!ENTITY x '&x;'><!ATTLIST root a CDATA '&x;'>]>");
        } catch (const NiniXmlProviderError& e) {
            failed = e.source_type == "System.Xml.XmlException" && e.hresult == 0x80131940u;
        }
        check(failed);
        std::cout << "7 DTD public API units passed\n";
        return 0;
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string> parts;
        std::stringstream in(line);
        std::string p;
        while (std::getline(in, p, '|'))
            parts.push_back(p);
        auto reader = NativeNiniXmlReader::from_text(nini_writer_text(parts[0] == "G" ? u"" : D(parts[1])));
        auto adapter = native_nini_xml_dtd_adapter(reader);
        auto* raw = adapter.get();
        TraceAdapter* tracing = nullptr;
        if (parts.size() >= 4) {
            auto wrapper = std::make_unique<TraceAdapter>(
                std::move(adapter), parts[3], parts.size() > 4 ? std::stoi(parts[4]) : 0,
                parts.size() > 5 && parts[5] != "~" ? std::optional(D(parts[5])) : std::nullopt);
            tracing = wrapper.get();
            adapter = std::move(wrapper);
        }
        try {
            auto nullable = [&](std::size_t i) {
                return parts[i] == "~" ? NiniWriterText{} : nini_writer_text(D(parts[i]));
            };
            auto info =
                parts[0].starts_with("I")
                    ? native_nini_xml_parse_dtd(*adapter)
                    : (parts[0] == "G"
                           ? native_nini_xml_parse_free_floating_dtd(*adapter, nullable(6), nullable(2),
                                                                     nullable(7), nullable(8), nullable(1))
                           : native_nini_xml_parse_free_floating_dtd(*adapter, nini_writer_text(u""),
                                                                     nini_writer_text(D(parts[2])), {}, {},
                                                                     nini_writer_text(D(parts[1]))));
            std::cout << "OK|" << Info(*info);
            if (parts[0] == "G")
                std::cout << "|IDS:" << E(info->public_id) << ':' << E(info->system_id);
        } catch (const NiniXmlProviderError& e) {
            std::cout << "E|" << e.source_type << '|' << e.hresult << '|' << E(std::u16string_view(e.message))
                      << '|';
            if (e.source_type == "System.Xml.XmlException")
                std::cout << e.line_number.value_or(0) << ':' << e.line_position.value_or(0) << ':'
                          << (e.source_uri ? E(std::u16string_view(*e.source_uri)) : "~");
            else
                std::cout << '-';
        } catch (const std::exception& e) {
            std::cout << "NATIVE|" << e.what();
        }
        if (tracing) {
            std::cout << "|TRACE:" << E(std::u16string_view(tracing->trace));
            auto buffer = raw->parsing_buffer();
            int used = raw->parsing_buffer_length();
            std::cout << "|STATE:" << raw->current_position() << ':' << raw->line_number() << ':'
                      << raw->line_start_position() << ':' << B(raw->is_eof()) << ':'
                      << raw->entity_stack_length() << ':' << static_cast<int>(reader->read_state()) << ':'
                      << E(std::u16string_view(buffer.data(), used));
        }
        std::cout << '\n';
        std::cout.flush();
    }
}
