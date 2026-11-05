// Source port of System.Xml.XmlTextWriter / XmlDOMTextWriter, .NET8.0.31.
// MIT license: native-port/third_party/dotnet_collation/LICENSE.
// Synchronous writer over explicit retained text/encoding providers.
// File/Stream construction is a separate provider obligation.
#pragma once
#include "opensim/framework/nini_xml_encoder.hpp"
#include <vector>
#include <memory>
#include <array>
#include <span>
#include <algorithm>
namespace opensim::framework {
using XmlWriterText = std::optional<std::u16string>;
struct XmlTextWriterError : std::runtime_error {
    std::string source_type;
    unsigned hresult;
    std::u16string message;
    XmlWriterText parameter;
    XmlTextWriterError(std::string type, unsigned code, std::u16string text, XmlWriterText param = {})
        : std::runtime_error("XML text writer error"), source_type(std::move(type)), hresult(code),
          message(std::move(text)), parameter(std::move(param)) {
    }
};
struct NativeXmlTextEncoding {
    std::function<XmlWriterText()> web_name;
};
class XmlWriterCharArray {
    std::vector<char16_t> values_;
    std::size_t length_;

  public:
    explicit XmlWriterCharArray(std::size_t length, char16_t value = 0)
        : values_(length ? length : 1, value), length_(length) {
    }
    explicit XmlWriterCharArray(std::u16string_view text) : XmlWriterCharArray(text.size()) {
        std::copy(text.begin(), text.end(), values_.begin());
    }
    std::size_t size() const {
        return length_;
    }
    char16_t* data() {
        return values_.data();
    }
    const char16_t* data() const {
        return values_.data();
    }
    std::u16string_view view() const {
        return {values_.data(), length_};
    }
    char16_t& operator[](std::ptrdiff_t i) {
        if (i < 0 || static_cast<std::size_t>(i) >= length_)
            throw XmlTextWriterError("System.IndexOutOfRangeException", 0x80131508,
                                     u"Index was outside the bounds of the array.");
        return values_[i];
    }
};
using XmlWriterCharArrayHandle = std::shared_ptr<XmlWriterCharArray>;
// Managed arrays retain their allocated length across logical stack pops.
// Writer callbacks can reenter and change the current stack index, so each
// subsequent access must reproduce the source bounds failure, never C++ UB.
template <class T> class XmlWriterArray {
    std::vector<T> values_;
    void check(std::ptrdiff_t index) const {
        if (index < 0 || static_cast<std::size_t>(index) >= values_.size())
            throw XmlTextWriterError("System.IndexOutOfRangeException", 0x80131508,
                                     u"Index was outside the bounds of the array.");
    }

  public:
    explicit XmlWriterArray(std::size_t size) : values_(size) {
    }
    std::size_t size() const {
        return values_.size();
    }
    void resize(std::size_t size) {
        values_.resize(size);
    }
    T& operator[](std::ptrdiff_t index) {
        check(index);
        return values_[index];
    }
    const T& operator[](std::ptrdiff_t index) const {
        check(index);
        return values_[index];
    }
};
// The Encoding object is captured at construction; its virtual WebName getter
// runs later, each time a document declaration is written.
// File/Stream construction and arbitrary encoding remain explicit providers.
struct XmlTextWriterIo {
    NativeXmlTextEncoder::Sink write;
    std::function<void(XmlWriterCharArrayHandle, std::int32_t, std::int32_t)> write_array;
    std::function<void(XmlWriterCharArrayHandle)> write_whole_array;
    std::function<void()> write_line, flush, dispose;
    std::function<std::shared_ptr<NativeXmlTextEncoding>()> encoding;
};
class NativeXmlTextWriter {
    enum class State { Start, Prolog, PostDTD, Element, Attribute, Content, AttrOnly, Epilog, Error, Closed };
    enum class Token {
        PI,
        Doctype,
        Comment,
        CData,
        StartElement,
        EndElement,
        LongEndElement,
        StartAttribute,
        EndAttribute,
        Content,
        Base64,
        RawData,
        Whitespace,
        Empty
    };
    enum class NsState {
        Uninitialized,
        NotDeclaredButInScope,
        DeclaredButNotWrittenOut,
        DeclaredAndWrittenOut
    };
    enum class Special { None, XmlSpace, XmlLang, XmlNs };
    struct Tag {
        XmlWriterText name, prefix;
        std::u16string default_ns;
        NsState default_state = NsState::Uninitialized;
        int xml_space = 0;
        XmlWriterText xml_lang;
        int previous_ns = -1, prefix_count = 0;
        bool mixed = false;
        void init(int);
    };
    struct Ns {
        std::u16string prefix, uri;
        bool declared = false;
    };
    XmlTextWriterIo io_;
    std::shared_ptr<NativeXmlTextEncoding> encoding_;
    NativeXmlTextEncoder encoder_;
    XmlWriterArray<Tag> stack_;
    XmlWriterArray<Ns> namespaces_;
    int top_ = 0, ns_top_ = -1;
    State state_ = State::Start;
    Token last_ = Token::Empty;
    Special special_ = Special::None;
    bool document_ = false, use_namespaces_ = true, dom_ = false, indented_ = false;
    int formatting_ = 0, indentation_ = 2;
    XmlWriterCharArrayHandle indent_chars_;
    char16_t quote_ = u'"', current_quote_ = 0;
    XmlWriterText prefix_for_xmlns_;
    bool flush_base64_ = false;
    int leftover_count_ = 0;
    std::array<std::uint8_t, 3> leftover_{};
    XmlWriterCharArrayHandle base64_line_;
    struct EncoderArrayFrame {
        XmlWriterCharArrayHandle array;
        int index;
    };
    std::vector<EncoderArrayFrame> encoder_arrays_;
    template <class F> void guarded(F&& f) {
        try {
            f();
        } catch (...) {
            state_ = State::Error;
            throw;
        }
    }
    void character(char16_t);
    void text(const XmlWriterText&);
    void text(std::u16string_view);
    void text(const char16_t* s) {
        text(std::u16string_view(s));
    }
    void complete(Token);
    void complete_all();
    void end_element(bool);
    void end_start_tag(bool);
    void end_attribute_quote();
    void indent(bool);
    void push_stack();
    void push_namespace(const XmlWriterText&, const std::u16string&, bool);
    int lookup_namespace(const std::u16string&) const;
    int lookup_current(const std::u16string&) const;
    XmlWriterText find_prefix(const std::u16string&) const;
    std::u16string generate_prefix();
    void verify_prefix_xml(const XmlWriterText&, const std::u16string&);
    void validate_name(const XmlWriterText&, bool);
    void internal_name(const XmlWriterText&, bool);
    void internal_pi(const XmlWriterText&, const XmlWriterText&);
    void special_attribute();
    void flush_encoders();
    void encode_base64(std::span<const std::uint8_t>, int, int);
    int base64_chars(std::span<const std::uint8_t>);
    void encoder_array(XmlWriterCharArrayHandle, int, int, bool raw);

  public:
    explicit NativeXmlTextWriter(XmlTextWriterIo, bool dom_mode = false);
    NativeXmlTextWriter(const NativeXmlTextWriter&) = delete;
    NativeXmlTextWriter& operator=(const NativeXmlTextWriter&) = delete;
    bool namespaces() const {
        return use_namespaces_;
    }
    void namespaces(bool);
    int formatting() const {
        return formatting_;
    }
    void formatting(int value) {
        formatting_ = value;
        indented_ = value == 1;
    }
    int indentation() const {
        return indentation_;
    }
    void indentation(int);
    char16_t indent_char() const {
        return (*indent_chars_)[0];
    }
    void indent_char(char16_t);
    char16_t quote_char() const {
        return quote_;
    }
    void quote_char(char16_t);
    int write_state() const;
    int xml_space() const;
    XmlWriterText xml_lang() const;
    void write_start_document(int standalone = -1);
    void write_end_document();
    void write_doctype(XmlWriterText name, XmlWriterText public_id, XmlWriterText system_id,
                       XmlWriterText subset);
    void write_start_element(XmlWriterText prefix, XmlWriterText local, XmlWriterText uri);
    void write_end_element() {
        end_element(false);
    }
    void write_full_end_element() {
        end_element(true);
    }
    void write_start_attribute(XmlWriterText prefix, XmlWriterText local, XmlWriterText uri);
    void write_end_attribute();
    void write_cdata(XmlWriterText);
    void write_comment(XmlWriterText);
    void write_processing_instruction(XmlWriterText name, XmlWriterText text);
    void write_entity_ref(XmlWriterText);
    void write_char_entity(char16_t);
    void write_surrogate_entity(char16_t low, char16_t high);
    void write_whitespace(XmlWriterText);
    void write_string(XmlWriterText);
    void write_raw(XmlWriterText);
    void write_chars(std::optional<std::u16string_view>, std::int32_t offset, std::int32_t count,
                     bool raw = false);
    void write_chars(XmlWriterCharArrayHandle, std::int32_t offset, std::int32_t count, bool raw = false);
    void write_base64(std::optional<std::span<const std::uint8_t>>, std::int32_t offset, std::int32_t count);
    void write_binhex(std::optional<std::span<const std::uint8_t>>, std::int32_t offset, std::int32_t count);
    void write_name(XmlWriterText);
    void write_nmtoken(XmlWriterText);
    void write_qualified_name(XmlWriterText local, XmlWriterText uri);
    XmlWriterText lookup_prefix(XmlWriterText uri) const;
    void flush() {
        io_.flush();
    }
    void close();
};
}
