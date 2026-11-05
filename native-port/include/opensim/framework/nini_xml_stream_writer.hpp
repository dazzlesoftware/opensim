// Consumed synchronous StreamWriter control flow from runtime8.0.31.
// MIT runtime license: native-port/third_party/dotnet_collation/LICENSE.
// Staged: no CLR dependency and no arbitrary encoding substitution.
#pragma once
#include "opensim/framework/nini_writer.hpp"
#include "opensim/framework/nini_stream_error.hpp"
#include "opensim/framework/nini_xml_text_writer.hpp"
namespace opensim::framework {
class NiniByteStream;
class NiniStreamWriterError : public NiniStreamError {
public:
    std::optional<int> actual_value;
    NiniStreamWriterError(std::string type, int code, std::u16string text,
                          std::optional<std::u16string> parameter = {}, std::optional<int> actual = {})
        : NiniStreamError(std::move(type), code, std::move(text), std::move(parameter)),
          actual_value(actual) {}
};
struct XmlStreamEncoder {
    std::function<int(std::span<const char16_t>, std::span<std::uint8_t>, bool)> get_bytes;
};
struct XmlStreamEncoding {
    std::shared_ptr<NativeXmlTextEncoding> xml_encoding;
    std::function<std::shared_ptr<XmlStreamEncoder>()> get_encoder;
    std::function<std::vector<std::uint8_t>()> preamble;
    std::function<int(int)> max_byte_count;
};
std::shared_ptr<XmlStreamEncoding> native_xml_utf8_no_bom();
// Intrinsic Encoding.GetEncoding with the source default UTF7-disabled policy.
// Hosts registering additional encoding providers supply the resolver boundary.
std::shared_ptr<XmlStreamEncoding> native_xml_intrinsic_encoding(NiniWriterText);
class NativeXmlStreamWriter : public NiniStreamWriter {
    struct State;
    std::shared_ptr<State> state_;

public:
    NativeXmlStreamWriter(std::shared_ptr<NiniByteStream>, std::shared_ptr<XmlStreamEncoding> encoding = {},
                          int buffer_size = -1, bool leave_open = false);
    void write(const NiniWriterText&) override;
    void flush() override;
    void close() override;
    NiniWriterText to_string() override;
    void write_char(char16_t);
    void write_span(std::u16string_view);
    void write_array(XmlWriterCharArrayHandle, int index, int count);
    void write_array(XmlWriterCharArrayHandle);
    void write_line();
    bool auto_flush() const;
    void auto_flush(bool);
    void new_line(NiniWriterText);
    NiniWriterText new_line() const;
    std::shared_ptr<XmlStreamEncoding> encoding() const;
};
// Stream constructor retains the caller's nullable encoding for XML declarations;
// the internal StreamWriter separately selects its default UTF8-no-BOM encoder.
XmlTextWriterIo native_xml_stream_io(std::shared_ptr<NiniByteStream>,
                                     std::shared_ptr<XmlStreamEncoding> encoding = {});
XmlTextWriterIo native_xml_stream_text_io(std::shared_ptr<NativeXmlStreamWriter>);
}
