// Port of .NET8.0.31 System.Xml.XmlTextWriter. MIT runtime license.
#include "opensim/framework/nini_xml_text_writer.hpp"
#include "nini_xml_writer_resources.hpp"
#include "nini_xml_writer_states.hpp"
#include "opensim/framework/nini_xml_character.hpp"
#include <array>
#include <bit>
#include <algorithm>
namespace opensim::framework {
using namespace xml_writer_resources;
namespace {
constexpr auto xmlns_uri = u"http://www.w3.org/2000/xmlns/";
constexpr auto xml_uri = u"http://www.w3.org/XML/1998/namespace";
XmlWriterCharArrayHandle default_indent_array() {
    static auto value = std::make_shared<XmlWriterCharArray>(64, u' ');
    return value;
}
XmlWriterCharArrayHandle close_tag_array() {
    static auto value = std::make_shared<XmlWriterCharArray>(u" />");
    return value;
}
XmlWriterCharArrayHandle end_tag_array() {
    static auto value = std::make_shared<XmlWriterCharArray>(u"</");
    return value;
}
bool empty(const XmlWriterText& s) {
    return !s || s->empty();
}
bool xml(const XmlWriterText& s) {
    return s && s->size() == 3 && ((*s)[0] == u'x' || (*s)[0] == u'X') &&
           ((*s)[1] == u'm' || (*s)[1] == u'M') && ((*s)[2] == u'l' || (*s)[2] == u'L');
}
std::u16string format(std::u16string_view resource, std::u16string_view a, std::u16string_view b = u"") {
    std::u16string out(resource);
    for (auto pair : {std::pair{u"{0}", a}, std::pair{u"{1}", b}}) {
        auto at = out.find(pair.first);
        if (at != out.npos)
            out.replace(at, 3, pair.second);
    }
    return out;
}
[[noreturn]] void argument(std::u16string_view s) {
    throw XmlTextWriterError("System.ArgumentException", 0x80070057, std::u16string(s));
}
[[noreturn]] void operation(std::u16string_view s) {
    throw XmlTextWriterError("System.InvalidOperationException", 0x80131509, std::u16string(s));
}
void nonempty(const XmlWriterText& s, std::u16string_view parameter) {
    auto p = std::u16string(parameter);
    if (!s)
        throw XmlTextWriterError("System.ArgumentNullException", 0x80004003,
                                 u"Value cannot be null. (Parameter '" + p + u"')", p);
    if (s->empty())
        throw XmlTextWriterError("System.ArgumentException", 0x80070057,
                                 u"The value cannot be an empty string. (Parameter '" + p + u"')", p);
}
std::u16string decimal(int n) {
    auto s = std::to_string(n);
    return {s.begin(), s.end()};
}
void validate_bytes(std::optional<std::span<const std::uint8_t>> buffer, int index, int count) {
    if (!buffer)
        throw XmlTextWriterError("System.ArgumentNullException", 0x80004003,
                                 u"Value cannot be null. (Parameter 'buffer')", u"buffer");
#ifdef _WIN32
    constexpr auto newline = u"\r\n";
#else
    constexpr auto newline = u"\n";
#endif
    auto negative = [&](int value, std::u16string p) {
        if (value < 0) {
            auto v = decimal(value);
            throw XmlEncoderArrayError("System.ArgumentOutOfRangeException", 0x80131502,
                                       p + u" ('" + v + u"') must be a non-negative value. (Parameter '" + p +
                                           u"')" + newline + u"Actual value was " + v + u".",
                                       p, value);
        }
    };
    negative(index, u"index");
    negative(count, u"count");
    int available = static_cast<int>(buffer->size()) - index;
    if (count > available) {
        auto v = decimal(count);
        throw XmlEncoderArrayError("System.ArgumentOutOfRangeException", 0x80131502,
                                   u"count ('" + v + u"') must be less than or equal to '" +
                                       decimal(available) + u"'. (Parameter 'count')" + newline +
                                       u"Actual value was " + v + u".",
                                   u"count", count);
    }
}
constexpr std::array<std::u16string_view, 10> state_names = {
    u"Start",   u"Prolog",   u"PostDTD", u"Element", u"Attribute",
    u"Content", u"AttrOnly", u"Epilog",  u"Error",   u"Closed"};
constexpr std::array<std::u16string_view, 14> token_names = {
    u"PI",         u"Doctype",        u"Comment",        u"CData",        u"StartElement",
    u"EndElement", u"LongEndElement", u"StartAttribute", u"EndAttribute", u"Content",
    u"Base64",     u"RawData",        u"Whitespace",     u"Empty"};
}
void NativeXmlTextWriter::Tag::init(int ns) {
    name.reset();
    default_ns.clear();
    default_state = NsState::Uninitialized;
    xml_space = 0;
    xml_lang.reset();
    previous_ns = ns;
    prefix_count = 0;
    mixed = false;
}
NativeXmlTextWriter::NativeXmlTextWriter(XmlTextWriterIo io, bool dom)
    : io_(std::move(io)), encoding_(io_.encoding()),
      encoder_([this](XmlEncoderWriteKind k, std::optional<std::u16string_view> s) {
          if (k != XmlEncoderWriteKind::Array) {
              io_.write(k, s);
              return;
          }
          if (encoder_arrays_.empty())
              throw std::logic_error("XML encoder array owner frame missing");
          auto frame = encoder_arrays_.back();
          auto index = static_cast<int>(s->data() - frame.array->data());
          io_.write_array(frame.array, index, static_cast<int>(s->size()));
      }),
      stack_(10), namespaces_(8), dom_(dom), indent_chars_(default_indent_array()) {
    stack_[0].init(-1);
}
void NativeXmlTextWriter::character(char16_t c) {
    io_.write(XmlEncoderWriteKind::Character, std::u16string_view(&c, 1));
}
void NativeXmlTextWriter::text(const XmlWriterText& s) {
    auto stable = s;
    if (stable)
        io_.write(XmlEncoderWriteKind::String, *stable);
    else
        io_.write(XmlEncoderWriteKind::String, {});
}
void NativeXmlTextWriter::text(std::u16string_view s) {
    std::u16string stable(s);
    io_.write(XmlEncoderWriteKind::String, stable);
}
void NativeXmlTextWriter::namespaces(bool value) {
    if (state_ != State::Start)
        operation(Xml_NotInWriteState);
    use_namespaces_ = value;
}
void NativeXmlTextWriter::indentation(int value) {
    if (value < 0) {
#ifdef _WIN32
        constexpr auto newline = u"\r\n";
#else
        constexpr auto newline = u"\n";
#endif
        auto v = decimal(value);
        throw XmlEncoderArrayError("System.ArgumentOutOfRangeException", 0x80131502,
                                   u"value ('" + v + u"') must be a non-negative value. (Parameter 'value')" +
                                       newline + u"Actual value was " + v + u".",
                                   u"value", value);
    }
    indentation_ = value;
}
void NativeXmlTextWriter::quote_char(char16_t value) {
    if (value != u'"' && value != u'\'')
        argument(Xml_InvalidQuote);
    quote_ = value;
    encoder_.quote_char(value);
}
void NativeXmlTextWriter::indent_char(char16_t value) {
    if (value == u' ') {
        indent_chars_ = default_indent_array();
        return;
    }
    if (indent_chars_ == default_indent_array())
        indent_chars_ = std::make_shared<XmlWriterCharArray>(64);
    std::fill_n(indent_chars_->data(), 64, value);
}
int NativeXmlTextWriter::write_state() const {
    switch (state_) {
    case State::Start:
        return 0;
    case State::Prolog:
    case State::PostDTD:
        return 1;
    case State::Element:
        return 2;
    case State::Attribute:
    case State::AttrOnly:
        return 3;
    case State::Content:
    case State::Epilog:
        return 4;
    case State::Closed:
        return 5;
    default:
        return 6;
    }
}
int NativeXmlTextWriter::xml_space() const {
    for (int i = top_; i > 0; --i)
        if (stack_[i].xml_space)
            return stack_[i].xml_space;
    return 0;
}
XmlWriterText NativeXmlTextWriter::xml_lang() const {
    for (int i = top_; i > 0; --i)
        if (stack_[i].xml_lang)
            return stack_[i].xml_lang;
    return {};
}
void NativeXmlTextWriter::push_stack() {
    if (top_ == static_cast<int>(stack_.size()) - 1)
        stack_.resize(stack_.size() + 10);
    stack_[++top_].init(ns_top_);
}
int NativeXmlTextWriter::lookup_namespace(const std::u16string& p) const {
    for (int i = ns_top_; i >= 0; --i)
        if (namespaces_[i].prefix == p)
            return i;
    return -1;
}
int NativeXmlTextWriter::lookup_current(const std::u16string& p) const {
    for (int i = ns_top_; i > stack_[top_].previous_ns; --i)
        if (namespaces_[i].prefix == p)
            return i;
    return -1;
}
XmlWriterText NativeXmlTextWriter::find_prefix(const std::u16string& uri) const {
    for (int i = ns_top_; i >= 0; --i)
        if (namespaces_[i].uri == uri && lookup_namespace(namespaces_[i].prefix) == i)
            return namespaces_[i].prefix;
    return {};
}
std::u16string NativeXmlTextWriter::generate_prefix() {
    auto n = stack_[top_].prefix_count++;
    return u"d" + decimal(top_) + u"p" + decimal(n + 1);
}
void NativeXmlTextWriter::verify_prefix_xml(const XmlWriterText& prefix, const std::u16string& uri) {
    if (xml(prefix) && uri != xml_uri)
        argument(Xml_InvalidPrefix);
}
void NativeXmlTextWriter::push_namespace(const XmlWriterText& prefix, const std::u16string& uri,
                                         bool declared) {
    if (uri == xmlns_uri)
        argument(Xml_CanNotBindToReservedNamespace);
    if (!prefix) {
        auto& t = stack_[top_];
        switch (t.default_state) {
        case NsState::Uninitialized:
        case NsState::NotDeclaredButInScope:
            t.default_ns = uri;
            break;
        case NsState::DeclaredButNotWrittenOut:
            break;
        default:
            return;
        }
        t.default_state = declared ? NsState::DeclaredAndWrittenOut : NsState::DeclaredButNotWrittenOut;
        return;
    }
    if (!prefix->empty() && uri.empty())
        argument(Xml_PrefixForEmptyNs);
    auto old = lookup_namespace(*prefix);
    if (old != -1 && namespaces_[old].uri == uri) {
        if (declared)
            namespaces_[old].declared = true;
        return;
    }
    if (declared && old != -1 && old > stack_[top_].previous_ns)
        namespaces_[old].declared = true;
    auto index = ++ns_top_;
    if (index == static_cast<int>(namespaces_.size()))
        namespaces_.resize(index * 2);
    namespaces_[index] = {*prefix, uri, declared};
}
void NativeXmlTextWriter::validate_name(const XmlWriterText& name, bool nc) {
    nonempty(name, u"name");
    auto& s = *name;
    if (use_namespaces_) {
        auto n = nini_xml_parse_ncname(s);
        int colon = -1;
        for (;;) {
            if (n == s.size())
                return;
            if (s[n] != u':' || nc || colon != -1 || n == 0 || n + 1 >= s.size())
                break;
            colon = static_cast<int>(n++);
            n += nini_xml_parse_nmtoken(s, n);
        }
    } else if (nini_xml_parse_name(s) == s.size())
        return;
    argument(format(Xml_InvalidNameChars, s));
}
void NativeXmlTextWriter::internal_name(const XmlWriterText& name, bool nc) {
    validate_name(name, nc);
    text(name);
}
void NativeXmlTextWriter::internal_pi(const XmlWriterText& name, const XmlWriterText& value) {
    text(u"<?");
    validate_name(name, false);
    text(name);
    character(u' ');
    if (value)
        encoder_.write_raw_checked(*value);
    text(u"?>");
}
void NativeXmlTextWriter::special_attribute() {
    auto value = encoder_.attribute_value();
    switch (special_) {
    case Special::XmlLang:
        stack_[top_].xml_lang = value;
        break;
    case Special::XmlSpace: {
        auto start = value.find_first_not_of(u" \t\r\n"), end = value.find_last_not_of(u" \t\r\n");
        auto trimmed = start == value.npos ? std::u16string{} : value.substr(start, end - start + 1);
        if (trimmed == u"default")
            stack_[top_].xml_space = 1;
        else if (trimmed == u"preserve")
            stack_[top_].xml_space = 2;
        else
            argument(format(Xml_InvalidXmlSpace, value));
        break;
    }
    case Special::XmlNs:
        verify_prefix_xml(prefix_for_xmlns_, value);
        push_namespace(prefix_for_xmlns_, value, true);
        break;
    default:
        break;
    }
}
void NativeXmlTextWriter::end_attribute_quote() {
    if (special_ != Special::None)
        special_attribute();
    encoder_.end_attribute();
    character(current_quote_);
}
void NativeXmlTextWriter::end_start_tag(bool empty_tag) {
    encoder_.start_attribute(false);
    for (int i = ns_top_; i > stack_[top_].previous_ns; --i)
        if (!namespaces_[i].declared) {
            text(u" xmlns:");
            text(std::u16string_view(namespaces_[i].prefix));
            character(u'=');
            character(quote_);
            encoder_.write(std::u16string(namespaces_[i].uri));
            character(quote_);
        }
    if (top_ == 0)
        throw XmlTextWriterError("System.IndexOutOfRangeException", 0x80131508,
                                 u"Index was outside the bounds of the array.");
    if (stack_[top_].default_ns != stack_[top_ - 1].default_ns &&
        stack_[top_].default_state == NsState::DeclaredButNotWrittenOut) {
        text(u" xmlns=");
        character(quote_);
        encoder_.write(std::u16string(stack_[top_].default_ns));
        character(quote_);
        stack_[top_].default_state = NsState::DeclaredAndWrittenOut;
    }
    encoder_.end_attribute();
    if (empty_tag)
        io_.write_whole_array(close_tag_array());
    else
        character(u'>');
}
void NativeXmlTextWriter::indent(bool before_end) {
    if (top_ == 0) {
        io_.write_line();
        return;
    }
    if (stack_[top_].mixed)
        return;
    io_.write_line();
    auto product =
        static_cast<std::uint32_t>(before_end ? top_ - 1 : top_) * static_cast<std::uint32_t>(indentation_);
    auto n = std::bit_cast<std::int32_t>(product);
    if (n <= static_cast<int>(indent_chars_->size())) {
        io_.write_array(indent_chars_, 0, n);
        return;
    }
    while (n > 0) {
        io_.write_array(indent_chars_, 0, std::min(n, static_cast<int>(indent_chars_->size())));
        n -= static_cast<int>(indent_chars_->size());
    }
}
void NativeXmlTextWriter::complete(Token token) {
    if (state_ == State::Closed)
        operation(Xml_Closed);
    if (state_ == State::Error)
        operation(format(Xml_WrongToken, token_names[static_cast<int>(token)], state_names[8]));
    auto& table = document_ ? xml_writer_document_states : xml_writer_default_states;
    auto next = static_cast<State>(table[static_cast<int>(token) * 8 + static_cast<int>(state_)]);
    if (next == State::Error)
        operation(format(Xml_WrongToken, token_names[static_cast<int>(token)],
                         state_names[static_cast<int>(state_)]));
    switch (token) {
    case Token::Doctype:
        if (indented_ && state_ != State::Start)
            indent(false);
        break;
    case Token::PI:
    case Token::Comment:
    case Token::CData:
    case Token::StartElement:
        if (state_ == State::Attribute) {
            end_attribute_quote();
            end_start_tag(false);
        } else if (state_ == State::Element)
            end_start_tag(false);
        if (token == Token::CData)
            stack_[top_].mixed = true;
        else if (indented_ && state_ != State::Start)
            indent(false);
        break;
    case Token::EndElement:
    case Token::LongEndElement:
        if (flush_base64_)
            flush_encoders();
        if (state_ == State::Attribute)
            end_attribute_quote();
        if (state_ == State::Content)
            token = Token::LongEndElement;
        else
            end_start_tag(token == Token::EndElement);
        if (document_ && top_ == 1)
            next = State::Epilog;
        break;
    case Token::StartAttribute:
        if (flush_base64_)
            flush_encoders();
        if (state_ == State::Attribute) {
            end_attribute_quote();
            character(u' ');
        } else if (state_ == State::Element)
            character(u' ');
        break;
    case Token::EndAttribute:
        if (flush_base64_)
            flush_encoders();
        end_attribute_quote();
        break;
    case Token::Content:
    case Token::RawData:
    case Token::Whitespace:
        if (flush_base64_)
            flush_encoders();
        [[fallthrough]];
    case Token::Base64:
        if (state_ == State::Element && last_ != Token::Content)
            end_start_tag(false);
        if (next == State::Content)
            stack_[top_].mixed = true;
        break;
    default:
        operation(Xml_InvalidOperation);
    }
    state_ = next;
    last_ = token;
}
void NativeXmlTextWriter::complete_all() {
    if (flush_base64_)
        flush_encoders();
    while (top_ > 0)
        write_end_element();
}
void NativeXmlTextWriter::end_element(bool full) {
    guarded([&] {
        if (top_ <= 0)
            operation(Xml_NoStartTag);
        complete(full ? Token::LongEndElement : Token::EndElement);
        if (last_ == Token::LongEndElement) {
            if (indented_)
                indent(true);
            io_.write_whole_array(end_tag_array());
            if (use_namespaces_ && stack_[top_].prefix) {
                text(stack_[top_].prefix);
                character(u':');
            }
            text(stack_[top_].name);
            character(u'>');
        }
        ns_top_ = stack_[top_].previous_ns;
        --top_;
    });
}
void NativeXmlTextWriter::write_start_document(int standalone) {
    guarded([&] {
        if (state_ != State::Start)
            operation(Xml_NotTheFirst);
        document_ = true;
        state_ = State::Prolog;
        std::u16string value = u"version=";
        value += quote_;
        value += u"1.0";
        value += quote_;
        if (encoding_) {
            value += u" encoding=";
            value += quote_;
            auto name = encoding_->web_name();
            if (name)
                value += *name;
            value += quote_;
        }
        if (standalone >= 0) {
            value += u" standalone=";
            value += quote_;
            value += standalone == 0 ? u"no" : u"yes";
            value += quote_;
        }
        internal_pi(std::u16string(u"xml"), value);
    });
}
void NativeXmlTextWriter::write_end_document() {
    guarded([&] {
        complete_all();
        if (state_ != State::Epilog)
            argument(state_ == State::Closed ? Xml_ClosedOrError : Xml_NoRoot);
        document_ = false;
        state_ = State::Start;
        last_ = Token::Empty;
    });
}
void NativeXmlTextWriter::write_doctype(XmlWriterText name, XmlWriterText public_id, XmlWriterText system_id,
                                        XmlWriterText subset) {
    guarded([&] {
        validate_name(name, false);
        complete(Token::Doctype);
        text(u"<!DOCTYPE ");
        text(name);
        if (public_id) {
            std::u16string value = u" PUBLIC ";
            value += quote_;
            text(std::u16string_view(value));
            text(public_id);
            value = {quote_, u' ', quote_};
            text(std::u16string_view(value));
            text(system_id);
            character(quote_);
        } else if (system_id) {
            std::u16string value = u" SYSTEM ";
            value += quote_;
            text(std::u16string_view(value));
            text(system_id);
            character(quote_);
        }
        if (subset) {
            text(u"[");
            text(subset);
            text(u"]");
        }
        character(u'>');
    });
}
void NativeXmlTextWriter::write_start_element(XmlWriterText prefix, XmlWriterText local, XmlWriterText uri) {
    if (dom_ && empty(uri) && !empty(prefix))
        prefix = std::u16string{};
    guarded([&] {
        complete(Token::StartElement);
        push_stack();
        character(u'<');
        if (use_namespaces_) {
            stack_[top_].default_ns = stack_[top_ - 1].default_ns;
            if (stack_[top_ - 1].default_state != NsState::Uninitialized)
                stack_[top_].default_state = NsState::NotDeclaredButInScope;
            stack_[top_].mixed = stack_[top_ - 1].mixed;
            if (!uri) {
                if (!empty(prefix) && lookup_namespace(*prefix) == -1)
                    argument(Xml_UndefPrefix);
            } else if (!prefix) {
                auto found = find_prefix(*uri);
                if (found)
                    prefix = found;
                else
                    push_namespace({}, *uri, false);
            } else if (prefix->empty())
                push_namespace({}, *uri, false);
            else {
                if (uri->empty())
                    prefix.reset();
                verify_prefix_xml(prefix, *uri);
                push_namespace(prefix, *uri, false);
            }
            stack_[top_].prefix.reset();
            if (!empty(prefix)) {
                stack_[top_].prefix = prefix;
                text(prefix);
                character(u':');
            }
        } else if (!empty(uri) || !empty(prefix))
            argument(Xml_NoNamespaces);
        stack_[top_].name = local;
        text(local);
    });
}
void NativeXmlTextWriter::write_start_attribute(XmlWriterText prefix, XmlWriterText local,
                                                XmlWriterText uri) {
    if (dom_ && empty(uri) && !empty(prefix))
        prefix = std::u16string{};
    guarded([&] {
        complete(Token::StartAttribute);
        special_ = Special::None;
        if (use_namespaces_) {
            if (prefix && prefix->empty())
                prefix.reset();
            if (uri == XmlWriterText(std::u16string(xmlns_uri)) && !prefix &&
                local != XmlWriterText(u"xmlns"))
                prefix = u"xmlns";
            if (prefix == XmlWriterText(u"xml")) {
                if (local == XmlWriterText(u"lang"))
                    special_ = Special::XmlLang;
                else if (local == XmlWriterText(u"space"))
                    special_ = Special::XmlSpace;
            } else if (prefix == XmlWriterText(u"xmlns")) {
                if (uri && *uri != xmlns_uri)
                    argument(Xml_XmlnsBelongsToReservedNs);
                if (empty(local)) {
                    local = prefix;
                    prefix.reset();
                    prefix_for_xmlns_.reset();
                } else
                    prefix_for_xmlns_ = local;
                special_ = Special::XmlNs;
            } else if (!prefix && local == XmlWriterText(u"xmlns")) {
                if (uri && *uri != xmlns_uri)
                    argument(Xml_XmlnsBelongsToReservedNs);
                special_ = Special::XmlNs;
                prefix_for_xmlns_.reset();
            } else if (!uri) {
                if (prefix && lookup_namespace(*prefix) == -1)
                    argument(Xml_UndefPrefix);
            } else if (uri->empty())
                prefix = u"";
            else {
                verify_prefix_xml(prefix, *uri);
                if (prefix && lookup_current(*prefix) != -1)
                    prefix.reset();
                auto found = find_prefix(*uri);
                if (found && (!prefix || prefix == found))
                    prefix = found;
                else {
                    if (!prefix)
                        prefix = generate_prefix();
                    push_namespace(prefix, *uri, false);
                }
            }
            if (!empty(prefix)) {
                text(prefix);
                character(u':');
            }
        } else {
            if (!empty(uri) || !empty(prefix))
                argument(Xml_NoNamespaces);
            if (local == XmlWriterText(u"xml:lang"))
                special_ = Special::XmlLang;
            else if (local == XmlWriterText(u"xml:space"))
                special_ = Special::XmlSpace;
        }
        encoder_.start_attribute(special_ != Special::None);
        text(local);
        character(u'=');
        if (current_quote_ != quote_) {
            current_quote_ = quote_;
            encoder_.quote_char(quote_);
        }
        character(current_quote_);
    });
}
void NativeXmlTextWriter::write_end_attribute() {
    guarded([&] { complete(Token::EndAttribute); });
}
void NativeXmlTextWriter::write_cdata(XmlWriterText value) {
    guarded([&] {
        complete(Token::CData);
        if (value && value->find(u"]]>") != value->npos)
            argument(Xml_InvalidCDataChars);
        text(u"<![CDATA[");
        if (value)
            encoder_.write_raw_checked(*value);
        text(u"]]>");
    });
}
void NativeXmlTextWriter::write_comment(XmlWriterText value) {
    guarded([&] {
        if (value && (value->find(u"--") != value->npos || value->ends_with(u'-')))
            argument(Xml_InvalidCommentChars);
        complete(Token::Comment);
        text(u"<!--");
        if (value)
            encoder_.write_raw_checked(*value);
        text(u"-->");
    });
}
void NativeXmlTextWriter::write_processing_instruction(XmlWriterText name, XmlWriterText value) {
    guarded([&] {
        if (value && value->find(u"?>") != value->npos)
            argument(Xml_InvalidPiChars);
        if (xml(name) && document_)
            argument(Xml_DupXmlDecl);
        complete(Token::PI);
        internal_pi(name, value);
    });
}
void NativeXmlTextWriter::write_entity_ref(XmlWriterText name) {
    guarded([&] {
        validate_name(name, false);
        complete(Token::Content);
        encoder_.write_entity_ref(*name);
    });
}
void NativeXmlTextWriter::write_char_entity(char16_t c) {
    guarded([&] {
        complete(Token::Content);
        encoder_.write_char_entity(c);
    });
}
void NativeXmlTextWriter::write_surrogate_entity(char16_t l, char16_t h) {
    guarded([&] {
        complete(Token::Content);
        encoder_.write_surrogate_entity(l, h);
    });
}
void NativeXmlTextWriter::write_whitespace(XmlWriterText value) {
    guarded([&] {
        if (!value)
            value = u"";
        if (nini_xml_invalid_whitespace(*value) != -1)
            argument(Xml_NonWhitespace);
        complete(Token::Whitespace);
        encoder_.write(*value);
    });
}
void NativeXmlTextWriter::write_string(XmlWriterText value) {
    guarded([&] {
        if (!empty(value)) {
            complete(Token::Content);
            encoder_.write(*value);
        }
    });
}
void NativeXmlTextWriter::write_raw(XmlWriterText value) {
    guarded([&] {
        complete(Token::RawData);
        if (value)
            encoder_.write_raw_checked(*value);
        else
            encoder_.write_raw_checked({});
    });
}
void NativeXmlTextWriter::encoder_array(XmlWriterCharArrayHandle buffer, int index, int count, bool raw) {
    encoder_arrays_.push_back({buffer, index});
    try {
        std::optional<std::u16string_view> view;
        if (buffer)
            view = buffer->view();
        encoder_.write_array(view, index, count, raw);
    } catch (...) {
        encoder_arrays_.pop_back();
        throw;
    }
    encoder_arrays_.pop_back();
}
void NativeXmlTextWriter::write_chars(std::optional<std::u16string_view> buffer, std::int32_t offset,
                                      std::int32_t count, bool raw) {
    XmlWriterCharArrayHandle owner;
    if (buffer)
        owner = std::make_shared<XmlWriterCharArray>(*buffer);
    write_chars(owner, offset, count, raw);
}
void NativeXmlTextWriter::write_chars(XmlWriterCharArrayHandle buffer, std::int32_t offset,
                                      std::int32_t count, bool raw) {
    guarded([&] {
        complete(raw ? Token::RawData : Token::Content);
        encoder_array(buffer, offset, count, raw);
    });
}
int NativeXmlTextWriter::base64_chars(std::span<const std::uint8_t> bytes) {
    constexpr auto alphabet = u"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int count = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        auto left = bytes.size() - i;
        unsigned n = static_cast<unsigned>(bytes[i]) << 16;
        if (left > 1)
            n |= static_cast<unsigned>(bytes[i + 1]) << 8;
        if (left > 2)
            n |= bytes[i + 2];
        (*base64_line_)[count++] = alphabet[(n >> 18) & 63];
        (*base64_line_)[count++] = alphabet[(n >> 12) & 63];
        (*base64_line_)[count++] = left > 1 ? alphabet[(n >> 6) & 63] : u'=';
        (*base64_line_)[count++] = left > 2 ? alphabet[n & 63] : u'=';
    }
    return count;
}
void NativeXmlTextWriter::encode_base64(std::span<const std::uint8_t> buffer, int index, int count) {
    if (leftover_count_ > 0) {
        int n = leftover_count_;
        while (n < 3 && count > 0) {
            leftover_[n++] = buffer[index++];
            --count;
        }
        if (count == 0 && n < 3) {
            leftover_count_ = n;
            return;
        }
        auto chars = base64_chars(leftover_);
        encoder_array(base64_line_, 0, chars, true);
    }
    leftover_count_ = count % 3;
    if (leftover_count_ > 0) {
        count -= leftover_count_;
        for (int i = 0; i < leftover_count_; ++i)
            leftover_[i] = buffer[index + count + i];
    }
    int end = index + count, chunk = 768;
    while (index < end) {
        if (index + chunk > end)
            chunk = end - index;
        auto chars = base64_chars(buffer.subspan(index, chunk));
        encoder_array(base64_line_, 0, chars, true);
        index += chunk;
    }
}
void NativeXmlTextWriter::flush_encoders() {
    if (leftover_count_ > 0) {
        auto chars = base64_chars(std::span<const std::uint8_t>(leftover_).first(leftover_count_));
        encoder_array(base64_line_, 0, chars, true);
        leftover_count_ = 0;
    }
    flush_base64_ = false;
}
void NativeXmlTextWriter::write_base64(std::optional<std::span<const std::uint8_t>> buffer,
                                       std::int32_t index, std::int32_t count) {
    guarded([&] {
        if (!flush_base64_)
            complete(Token::Base64);
        flush_base64_ = true;
        if (!base64_line_)
            base64_line_ = std::make_shared<XmlWriterCharArray>(1024);
        validate_bytes(buffer, index, count);
        encode_base64(*buffer, index, count);
    });
}
void NativeXmlTextWriter::write_binhex(std::optional<std::span<const std::uint8_t>> buffer,
                                       std::int32_t index, std::int32_t count) {
    guarded([&] {
        complete(Token::Content);
        validate_bytes(buffer, index, count);
        auto twice = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(count) * 2);
        auto length = twice < 128 ? twice : 128;
        if (length < 0)
            throw XmlTextWriterError("System.OverflowException", 0x80131516,
                                     u"Arithmetic operation resulted in an overflow.");
        auto chars = std::make_shared<XmlWriterCharArray>(length);
        int end = index + count;
        while (index < end) {
            auto chunk = std::min(count, 64);
            for (int i = 0; i < chunk; ++i) {
                auto value = (*buffer)[index + i];
                (*chars)[i * 2] = u"0123456789ABCDEF"[value >> 4];
                (*chars)[i * 2 + 1] = u"0123456789ABCDEF"[value & 15];
            }
            write_chars(chars, 0, chunk * 2, true);
            index += chunk;
            count -= chunk;
        }
    });
}
void NativeXmlTextWriter::write_name(XmlWriterText value) {
    guarded([&] {
        complete(Token::Content);
        internal_name(value, false);
    });
}
void NativeXmlTextWriter::write_nmtoken(XmlWriterText value) {
    guarded([&] {
        complete(Token::Content);
        nonempty(value, u"name");
        if (nini_xml_parse_nmtoken(*value, 0, false) != value->size())
            argument(format(Xml_InvalidNameChars, *value));
        text(value);
    });
}
void NativeXmlTextWriter::write_qualified_name(XmlWriterText local, XmlWriterText uri) {
    guarded([&] {
        complete(Token::Content);
        if (use_namespaces_) {
            if (!empty(uri) && *uri != stack_[top_].default_ns) {
                auto prefix = find_prefix(*uri);
                if (!prefix) {
                    if (state_ != State::Attribute)
                        argument(format(Xml_UndefNamespace, *uri));
                    prefix = generate_prefix();
                    push_namespace(prefix, *uri, false);
                }
                if (!prefix->empty()) {
                    internal_name(prefix, true);
                    character(u':');
                }
            }
        } else if (!empty(uri))
            argument(Xml_NoNamespaces);
        internal_name(local, true);
    });
}
XmlWriterText NativeXmlTextWriter::lookup_prefix(XmlWriterText uri) const {
    nonempty(uri, u"ns");
    auto prefix = find_prefix(*uri);
    if (!prefix && *uri == stack_[top_].default_ns)
        prefix = u"";
    return prefix;
}
void NativeXmlTextWriter::close() {
    try {
        complete_all();
    } catch (...) {
    }
    state_ = State::Closed;
    io_.dispose();
}
}
