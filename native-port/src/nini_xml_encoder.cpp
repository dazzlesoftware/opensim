// Source-derived System.Xml.XmlTextEncoder; see header for MIT notice.
#include "opensim/framework/nini_xml_encoder.hpp"
#include "opensim/framework/nini_xml_character.hpp"
namespace opensim::framework {
namespace {
bool high(char16_t c) { return c >= 0xd800 && c <= 0xdbff; }
bool low(char16_t c) { return c >= 0xdc00 && c <= 0xdfff; }
bool attribute_char(char16_t c) { return (nini_xml_character_properties(c) & 0x80) != 0; }
std::u16string upper_hex(unsigned n) {
    char16_t b[8];
    unsigned i = 8;
    do {
        b[--i] = u"0123456789ABCDEF"[n & 15];
        n >>= 4;
    } while (n);
    return {b + i, b + 8};
}
std::u16string error_message(XmlEncoderError::Kind kind, char16_t h, char16_t l) {
    switch (kind) {
    case XmlEncoderError::Kind::InvalidPair:
        return u"The surrogate pair (0x" + upper_hex(h) + u", 0x" + upper_hex(l) +
               u") is invalid. A high surrogate character (0xD800 - 0xDBFF) must always be paired with a low surrogate character (0xDC00 - 0xDFFF).";
    case XmlEncoderError::Kind::InvalidHigh:
        return u"Invalid high surrogate character (0x" + upper_hex(h) +
               u"). A high surrogate character must have a value from range (0xD800 - 0xDBFF).";
    case XmlEncoderError::Kind::MissingLow:
        return u"The surrogate pair is invalid. Missing a low surrogate character.";
    default:
        return u"The second character surrogate pair is not in the input buffer to be written.";
    }
}
}
XmlEncoderError::XmlEncoderError(Kind k, char16_t h, char16_t l)
    : std::runtime_error("XML encoder argument error"), kind(k), high(h), low(l),
      message(error_message(k, h, l)) {}
std::u16string NativeXmlTextEncoder::hex(unsigned n) { return upper_hex(n); }
void NativeXmlTextEncoder::character(char16_t c) {
    sink_(XmlEncoderWriteKind::Character, std::u16string_view(&c, 1));
}
void NativeXmlTextEncoder::text(std::optional<std::u16string_view> s) {
    sink_(XmlEncoderWriteKind::String, s);
}
void NativeXmlTextEncoder::entity(std::optional<std::u16string_view> s) {
    character(u'&');
    text(s);
    character(u';');
}
void NativeXmlTextEncoder::numeric(std::u16string_view s) {
    text(u"&#x");
    text(s);
    character(u';');
}
void NativeXmlTextEncoder::start_attribute(bool cache) {
    in_attribute_ = true;
    cache_ = cache;
    if (cache)
        attribute_.clear();
}
void NativeXmlTextEncoder::end_attribute() {
    if (cache_)
        attribute_.clear();
    in_attribute_ = false;
    cache_ = false;
}
void NativeXmlTextEncoder::write_surrogate_char(char16_t l, char16_t h) {
    if (!low(l) || !high(h))
        throw XmlEncoderError(XmlEncoderError::Kind::InvalidPair, h, l);
    character(h);
    character(l);
}
void NativeXmlTextEncoder::write_surrogate_entity(char16_t l, char16_t h) {
    if (!low(l) || !high(h))
        throw XmlEncoderError(XmlEncoderError::Kind::InvalidPair, h, l);
    if (cache_) {
        attribute_ += h;
        attribute_ += l;
    }
    numeric(hex(0x10000 + ((h - 0xd800) << 10) + (l - 0xdc00)));
}
void NativeXmlTextEncoder::write_char_entity(char16_t c) {
    if (high(c) || low(c))
        throw XmlEncoderError(XmlEncoderError::Kind::MissingLow);
    auto n = hex(c);
    if (cache_) {
        attribute_ += u"&#x";
        attribute_ += n;
        attribute_ += u';';
    }
    numeric(n);
}
void NativeXmlTextEncoder::write_entity_ref(std::optional<std::u16string_view> s) {
    if (cache_) {
        attribute_ += u'&';
        if (s)
            attribute_ += *s;
        attribute_ += u';';
    }
    entity(s);
}
void NativeXmlTextEncoder::write(std::u16string_view s) {
    if (s.empty())
        return;
    if (cache_)
        attribute_ += s;
    std::size_t i = 0, start = 0;
    char16_t c = 0;
    for (;;) {
        if (i < s.size()) {
            c = s[i];
            if (attribute_char(c)) {
                ++i;
                continue;
            }
        }
        if (i == s.size()) {
            sink_(XmlEncoderWriteKind::Span, s);
            return;
        }
        if (in_attribute_ ? c == u'\t' : c == u'\t' || c == u'\n' || c == u'\r' || c == u'"' || c == u'\'') {
            ++i;
            continue;
        }
        break;
    }
    for (;;) {
        if (start < i)
            sink_(XmlEncoderWriteKind::Span, s.substr(start, i - start));
        if (i == s.size())
            break;
        switch (c) {
        case u'\t':
            character(c);
            break;
        case u'\n':
        case u'\r':
            if (in_attribute_)
                numeric(hex(c));
            else
                character(c);
            break;
        case u'<':
            entity(u"lt");
            break;
        case u'>':
            entity(u"gt");
            break;
        case u'&':
            entity(u"amp");
            break;
        case u'\'':
        case u'"':
            if (in_attribute_ && quote_ == c)
                entity(c == u'\'' ? u"apos" : u"quot");
            else
                character(c);
            break;
        default:
            if (high(c)) {
                if (i + 1 >= s.size())
                    throw XmlEncoderError(XmlEncoderError::Kind::InvalidPair, c, s[i]);
                write_surrogate_char(s[++i], c);
            } else {
                if (low(c))
                    throw XmlEncoderError(XmlEncoderError::Kind::InvalidHigh, c);
                numeric(hex(c));
            }
        }
        start = ++i;
        while (i < s.size()) {
            c = s[i];
            if (!attribute_char(c))
                break;
            ++i;
        }
    }
}
void NativeXmlTextEncoder::write_raw_checked(std::optional<std::u16string_view> s) {
    if (!s)
        return;
    if (cache_)
        attribute_ += *s;
    for (std::size_t i = 0; i < s->size();) {
        auto c = (*s)[i];
        if ((nini_xml_character_properties(c) & 0x10) || c < 0x20) {
            ++i;
            continue;
        }
        if (high(c)) {
            if (i + 1 >= s->size())
                throw XmlEncoderError(XmlEncoderError::Kind::MissingLow);
            if (!low((*s)[i + 1]))
                throw XmlEncoderError(XmlEncoderError::Kind::InvalidPair, c, (*s)[i + 1]);
            i += 2;
        } else {
            if (low(c))
                throw XmlEncoderError(XmlEncoderError::Kind::InvalidHigh, c);
            ++i;
        }
    }
    text(s);
}
void NativeXmlTextEncoder::write_raw_array_slice(std::u16string_view s) {
    if (cache_)
        attribute_ += s;
    sink_(XmlEncoderWriteKind::Array, s);
}
void NativeXmlTextEncoder::write_array(std::optional<std::u16string_view> s, std::int32_t offset,
                                       std::int32_t count, bool raw) {
    if (!s)
        throw XmlEncoderArrayError("System.ArgumentNullException", 0x80004003,
                                   u"Value cannot be null. (Parameter 'array')", u"array");
    auto decimal = [](std::int32_t value) {
        auto s = std::to_string(value);
        return std::u16string(s.begin(), s.end());
    };
#ifdef _WIN32
    constexpr auto newline = u"\r\n";
#else
    constexpr auto newline = u"\n";
#endif
    auto negative = [&](std::int32_t value, std::u16string_view parameter) {
        if (value < 0) {
            auto v = decimal(value);
            auto p = std::u16string(parameter);
            throw XmlEncoderArrayError("System.ArgumentOutOfRangeException", 0x80131502,
                                       p + u" ('" + v + u"') must be a non-negative value. (Parameter '" + p +
                                           u"')" + newline + u"Actual value was " + v + u".",
                                       p, value);
        }
    };
    if (raw) {
        negative(count, u"count");
        negative(offset, u"offset");
    } else {
        negative(offset, u"offset");
        negative(count, u"count");
    }
    auto available = static_cast<std::int32_t>(s->size()) - offset;
    if (count > available) {
        auto v = decimal(count);
        throw XmlEncoderArrayError("System.ArgumentOutOfRangeException", 0x80131502,
                                   u"count ('" + v + u"') must be less than or equal to '" +
                                       decimal(available) + u"'. (Parameter 'count')" + newline +
                                       u"Actual value was " + v + u".",
                                   u"count", count);
    }
    auto slice = s->substr(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
    if (raw)
        write_raw_array_slice(slice);
    else
        write_array_slice(slice);
}
void NativeXmlTextEncoder::write_array_slice(std::u16string_view s) {
    if (cache_)
        attribute_ += s;
    std::size_t i = 0;
    for (;;) {
        auto start = i;
        char16_t c = 0;
        while (i < s.size()) {
            c = s[i];
            if (!attribute_char(c))
                break;
            ++i;
        }
        if (start < i)
            sink_(XmlEncoderWriteKind::Array, s.substr(start, i - start));
        if (i == s.size())
            break;
        switch (c) {
        case u'\t':
            character(c);
            break;
        case u'\n':
        case u'\r':
            if (in_attribute_)
                numeric(hex(c));
            else
                character(c);
            break;
        case u'<':
            entity(u"lt");
            break;
        case u'>':
            entity(u"gt");
            break;
        case u'&':
            entity(u"amp");
            break;
        case u'\'':
        case u'"':
            if (in_attribute_ && quote_ == c)
                entity(c == u'\'' ? u"apos" : u"quot");
            else
                character(c);
            break;
        default:
            if (high(c)) {
                if (i + 1 >= s.size())
                    throw XmlEncoderError(XmlEncoderError::Kind::SplitPair);
                write_surrogate_char(s[++i], c);
            } else {
                if (low(c))
                    throw XmlEncoderError(XmlEncoderError::Kind::InvalidHigh, c);
                numeric(hex(c));
            }
        }
        ++i;
    }
}
}
