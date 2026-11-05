#include "opensim/framework/nini_xml_stream_writer.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <limits>
namespace opensim::framework {
namespace {
std::u16string line() {
#ifdef _WIN32
    return u"\r\n";
#else
    return u"\n";
#endif
}
[[noreturn]] void fail(std::string type, unsigned code, std::u16string message,
                       std::optional<std::u16string> param = {}, std::optional<int> actual = {}) {
    throw NiniStreamWriterError(std::move(type), std::bit_cast<int>(code), std::move(message),
                                std::move(param), actual);
}
[[noreturn]] void nullref() {
    fail("System.NullReferenceException", 0x80004003,
         u"Object reference not set to an instance of an object.");
}
[[noreturn]] void negative(int value, std::u16string param, bool zero = false) {
    auto t = std::to_string(value);
    std::u16string n(t.begin(), t.end());
    fail("System.ArgumentOutOfRangeException", 0x80131502,
         param + u" ('" + n + u"') must be a " + (zero ? u"non-negative and non-zero" : u"non-negative") +
             u" value. (Parameter '" + param + u"')" + line() + u"Actual value was " + n + u".",
         param, value);
}
char16_t& at(std::vector<char16_t>& a, int i) {
    if (i < 0 || static_cast<std::size_t>(i) >= a.size())
        fail("System.IndexOutOfRangeException", 0x80131508, u"Index was outside the bounds of the array.");
    return a[i];
}
struct Utf8State {
    char16_t pending = 0;
    [[noreturn]] void bad(char16_t ch, int index) {
        pending = 0;
        auto original = ch;
        std::u16string code(4, u'0');
        for (int i = 3; i >= 0; --i) {
            code[i] = u"0123456789ABCDEF"[ch & 15];
            ch >>= 4;
        }
        auto n = std::to_string(index);
        NiniStreamError e("System.Text.EncoderFallbackException", std::bit_cast<int>(0x80070057u),
                          u"Unable to translate Unicode character \\\\u" + code + u" at index " +
                              std::u16string(n.begin(), n.end()) + u" to specified code page.");
        e.character_index = index;
        e.unknown_character = original;
        throw e;
    }
    int encode(std::span<const char16_t> input, std::span<std::uint8_t> output, bool flush) {
        std::u16string valid;
        auto held = pending;
        std::size_t index = 0;
        if (held) {
            if (!input.empty() && input[0] >= 0xdc00 && input[0] <= 0xdfff) {
                valid += held;
                valid += input[0];
                index = 1;
                held = 0;
            } else if (!input.empty() || flush)
                bad(held, -1);
        }
        for (; index < input.size(); ++index) {
            auto ch = input[index];
            if (ch >= 0xd800 && ch <= 0xdbff) {
                if (index + 1 < input.size()) {
                    if (input[index + 1] < 0xdc00 || input[index + 1] > 0xdfff)
                        bad(ch, static_cast<int>(index));
                    valid += ch;
                    valid += input[++index];
                } else if (flush)
                    bad(ch, static_cast<int>(index));
                else
                    held = ch;
            } else if (ch >= 0xdc00 && ch <= 0xdfff)
                bad(ch, static_cast<int>(index));
            else
                valid += ch;
        }
        auto bytes = utf8(valid);
        if (bytes.size() > output.size())
            fail("System.ArgumentException", 0x80070057,
                 u"The output byte buffer is too small to contain the encoded data.");
        std::copy(bytes.begin(), bytes.end(), output.begin());
        pending = held;
        return static_cast<int>(bytes.size());
    }
};
}
std::shared_ptr<XmlStreamEncoding> native_xml_utf8_no_bom() {
    static auto value = [] {
        auto e = std::make_shared<XmlStreamEncoding>();
        e->xml_encoding = std::make_shared<NativeXmlTextEncoding>();
        e->xml_encoding->web_name = [] { return XmlWriterText(u"utf-8"); };
        e->get_encoder = [] {
            auto state = std::make_shared<Utf8State>();
            auto encoder = std::make_shared<XmlStreamEncoder>();
            encoder->get_bytes = [state](auto in, auto out, bool flush) {
                return state->encode(in, out, flush);
            };
            return encoder;
        };
        e->preamble = [] { return std::vector<std::uint8_t>{}; };
        e->max_byte_count = [](int n) {
            if (n < 0)
                negative(n, u"charCount");
            if (n > 715827881)
                fail(
                    "System.ArgumentOutOfRangeException", 0x80131502,
                    u"Too many characters. The resulting number of bytes is larger than what can be returned as an int. (Parameter 'charCount')",
                    u"charCount");
            return (n + 1) * 3;
        };
        return e;
    }();
    return value;
}
struct NativeXmlStreamWriter::State {
    std::shared_ptr<NiniByteStream> stream;
    std::shared_ptr<XmlStreamEncoding> encoding;
    std::shared_ptr<XmlStreamEncoder> encoder;
    std::vector<char16_t> chars;
    std::shared_ptr<std::vector<std::uint8_t>> bytes;
    int pos = 0, len = 0;
    bool automatic = false, preamble_written = false, closable = true, disposed = false;
    NiniWriterText newline = nini_writer_text(line());
    State(std::shared_ptr<NiniByteStream> s, std::shared_ptr<XmlStreamEncoding> e, int size, bool leave)
        : stream(std::move(s)) {
        if (!stream)
            fail("System.ArgumentNullException", 0x80004003, u"Value cannot be null. (Parameter 'stream')",
                 u"stream");
        if (!stream->can_write())
            fail("System.ArgumentException", 0x80070057, u"Stream was not writable.");
        if (size == -1)
            size = 1024;
        if (size <= 0)
            negative(size, u"bufferSize", true);
        encoding = e ? std::move(e) : native_xml_utf8_no_bom();
        encoder = encoding->get_encoder();
        size = std::max(size, 128);
        chars.resize(size);
        len = size;
        if (stream->can_seek() && stream->position() > 0)
            preamble_written = true;
        closable = !leave;
    }
    void check() {
        if (disposed)
            fail("System.ObjectDisposedException", 0x80131622,
                 u"Cannot write to a closed TextWriter." + line() + u"Object name: 'StreamWriter'.");
    }
    void flush(bool flush_stream, bool flush_encoder) {
        check();
        if (pos == 0 && !flush_stream && !flush_encoder)
            return;
        if (!preamble_written) {
            preamble_written = true;
            auto pre = encoding->preamble();
            if (!pre.empty())
                stream->write(pre);
        }
        std::array<std::uint8_t, 1024> stack;
        std::shared_ptr<std::vector<std::uint8_t>> owner = bytes;
        std::span<std::uint8_t> destination;
        if (owner)
            destination = *owner;
        else {
            auto max = encoding->max_byte_count(pos);
            if (max > 1024) {
                auto capacity = encoding->max_byte_count(static_cast<int>(chars.size()));
                if (capacity < 0)
                    fail("System.OverflowException", 0x80131516,
                         u"Arithmetic operation resulted in an overflow.");
                owner = std::make_shared<std::vector<std::uint8_t>>(capacity);
                bytes = owner;
                destination = *owner;
            } else
                destination = stack;
        }
        if (!encoder)
            nullref();
        if (pos < 0 || static_cast<std::size_t>(pos) > chars.size())
            fail("System.ArgumentOutOfRangeException", 0x80131502,
                 u"Specified argument was out of the range of valid values.");
        auto count = encoder->get_bytes(std::span(chars).first(pos), destination, flush_encoder);
        pos = 0;
        if (count > 0) {
            if (static_cast<std::size_t>(count) > destination.size())
                fail("System.ArgumentOutOfRangeException", 0x80131502,
                     u"Specified argument was out of the range of valid values.");
            stream->write(destination.first(count));
        }
        if (flush_stream)
            stream->flush();
    }
    void write_span(std::u16string_view input, bool append_line = false) {
        auto count = static_cast<int>(input.size());
        if (count <= 4 && count <= len - pos) {
            for (int i = 0; i < count; ++i)
                at(chars, pos++) = input[i];
        } else {
            check();
            int source = 0, remaining = count, target = pos;
            while (remaining > 0) {
                if (target == static_cast<int>(chars.size())) {
                    flush(false, false);
                    target = 0;
                }
                auto n = std::min(static_cast<int>(chars.size()) - target, remaining);
                if (n < 0)
                    fail("System.OverflowException", 0x80131516,
                         u"Arithmetic operation resulted in an overflow.");
                if (target < 0)
                    fail("System.IndexOutOfRangeException", 0x80131508,
                         u"Index was outside the bounds of the array.");
                std::copy_n(input.data() + source, n, chars.data() + target);
                pos += n;
                target += n;
                source += n;
                remaining -= n;
            }
        }
        if (append_line) {
            auto held = newline;
            for (auto c : *held) {
                if (pos == len)
                    flush(false, false);
                at(chars, pos) = c;
                ++pos;
            }
        }
        if (automatic)
            flush(true, false);
    }
    void character(char16_t c) {
        if (pos == len)
            flush(false, false);
        at(chars, pos) = c;
        ++pos;
        if (automatic)
            flush(true, false);
    }
    void close() {
        std::exception_ptr error;
        try {
            if (!disposed)
                flush(true, true);
        } catch (...) {
            error = std::current_exception();
        }
        if (closable && !disposed) {
            try {
                stream->close();
            } catch (...) {
                error = std::current_exception();
            }
            disposed = true;
            len = 0;
        }
        if (error)
            std::rethrow_exception(error);
    }
};
NativeXmlStreamWriter::NativeXmlStreamWriter(std::shared_ptr<NiniByteStream> s,
                                             std::shared_ptr<XmlStreamEncoding> e, int size, bool leave)
    : NiniStreamWriter(s), state_(std::make_shared<State>(std::move(s), std::move(e), size, leave)) {
}
void NativeXmlStreamWriter::write(const NiniWriterText& text) {
    auto s = state_;
    s->write_span(text ? std::u16string_view(*text) : std::u16string_view{});
}
void NativeXmlStreamWriter::write_span(std::u16string_view text) {
    auto s = state_;
    s->write_span(text);
}
void NativeXmlStreamWriter::write_char(char16_t c) {
    auto s = state_;
    s->character(c);
}
void NativeXmlStreamWriter::flush() {
    auto s = state_;
    s->flush(true, true);
}
void NativeXmlStreamWriter::close() {
    auto s = state_;
    s->close();
}
NiniWriterText NativeXmlStreamWriter::to_string() {
    static const auto name = nini_writer_text(u"System.IO.StreamWriter");
    return name;
}
void NativeXmlStreamWriter::write_array(XmlWriterCharArrayHandle a, int index, int count) {
    auto s = state_;
    if (!a)
        fail("System.ArgumentNullException", 0x80004003, u"Value cannot be null. (Parameter 'buffer')",
             u"buffer");
    if (index < 0)
        negative(index, u"index");
    if (count < 0)
        negative(count, u"count");
    if (static_cast<std::int64_t>(a->size()) - index < count)
        fail(
            "System.ArgumentException", 0x80070057,
            u"Offset and length were out of bounds for the array or count is greater than the number of elements from index to the end of the source collection.");
    s->write_span(a->view().substr(index, count));
}
void NativeXmlStreamWriter::write_array(XmlWriterCharArrayHandle a) {
    auto s = state_;
    s->write_span(a ? a->view() : std::u16string_view{});
}
void NativeXmlStreamWriter::write_line() {
    auto s = state_;
    auto held = s->newline;
    s->write_span(*held);
}
bool NativeXmlStreamWriter::auto_flush() const {
    return state_->automatic;
}
void NativeXmlStreamWriter::auto_flush(bool value) {
    auto s = state_;
    s->automatic = value;
    if (value)
        s->flush(true, false);
}
void NativeXmlStreamWriter::new_line(NiniWriterText text) {
    state_->newline = text ? std::move(text) : nini_writer_text(line());
}
NiniWriterText NativeXmlStreamWriter::new_line() const {
    return state_->newline;
}
std::shared_ptr<XmlStreamEncoding> NativeXmlStreamWriter::encoding() const {
    return state_->encoding;
}
XmlTextWriterIo native_xml_stream_text_io(std::shared_ptr<NativeXmlStreamWriter> writer) {
    XmlTextWriterIo io;
    auto require = [writer] {
        if (!writer)
            nullref();
    };
    io.encoding = [writer, require] {
        require();
        return writer->encoding()->xml_encoding;
    };
    io.write = [writer, require](auto kind, auto text) {
        require();
        if (kind == XmlEncoderWriteKind::Character) {
            if (!text || text->empty())
                nullref();
            writer->write_char((*text)[0]);
        } else
            writer->write_span(text ? *text : std::u16string_view{});
    };
    io.write_array = [writer, require](auto array, int index, int count) {
        require();
        writer->write_array(array, index, count);
    };
    io.write_whole_array = [writer, require](auto array) {
        require();
        writer->write_array(array);
    };
    io.write_line = [writer, require] {
        require();
        writer->write_line();
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
XmlTextWriterIo native_xml_stream_io(std::shared_ptr<NiniByteStream> stream,
                                     std::shared_ptr<XmlStreamEncoding> encoding) {
    auto declared = encoding ? encoding->xml_encoding : nullptr;
    auto writer = std::make_shared<NativeXmlStreamWriter>(std::move(stream), std::move(encoding));
    auto io = native_xml_stream_text_io(writer);
    io.encoding = [declared] { return declared; };
    return io;
}
}
