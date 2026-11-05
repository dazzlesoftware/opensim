// Consumed .NET8 synchronous text stream closure; MIT .NET source notice:
// native-port/third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/nini_document.hpp"
#include "opensim/framework/nini_stream_error.hpp"
#include "opensim/framework/nini_xml_stream_writer.hpp"
#include "opensim/framework/stream_reader_text.hpp"
#include <span>
namespace opensim::framework {
class NiniByteStream : public NiniWriterStream {
public:
    virtual bool can_read() = 0;
    virtual bool can_write() = 0;
    virtual bool can_seek() = 0;
    virtual std::int64_t position() = 0;
    virtual std::size_t read(std::span<std::uint8_t> buffer, std::size_t offset, std::size_t count) = 0;
    virtual void write(std::span<const std::uint8_t>) = 0;
    virtual void flush() = 0;
};
class NiniByteTextReader final : public NiniTextReader {
    struct State;
    std::shared_ptr<State> state_;

public:
    explicit NiniByteTextReader(std::shared_ptr<NiniByteStream>);
    int peek() override;
    int read() override;
    void close() override;
    std::int32_t read_chars(std::span<char16_t>);
    StreamReaderEncoding current_encoding() const;
    std::shared_ptr<NiniByteStream> base_stream() const;
};
class NativeNiniXmlTextInput;
std::shared_ptr<NativeNiniXmlTextInput> native_xml_stream_reader_input(std::shared_ptr<NiniByteStream>);
// Compatibility name for the shared native StreamWriter implementation.
// INI and XML now use the same source buffering, encoding and lifetime rules.
class NiniByteTextWriter final : public NativeXmlStreamWriter {
public:
    explicit NiniByteTextWriter(std::shared_ptr<NiniByteStream>);
};
// Consumed FileStream opening configuration (managed enum values). A filesystem
// backends implement the requested source constructor recipe.
using NiniFileStreamProvider = std::function<std::shared_ptr<NiniByteStream>(
    const NiniWriterText&, std::int32_t mode, std::int32_t access, std::int32_t share,
    std::int32_t buffer_size, std::int32_t options)>;
// Native consumed FileStream recipes: Open/Read/ShareRead (or diagnostic ReadWrite share) and
// Create/Write/ShareNone-or-Read, buffer4096, optionsNone. Other recipes require
// an application byte-stream provider; this API does not pretend to port the
// entire unrelated FileStream public surface.
NiniFileStreamProvider native_nini_file_stream_provider(std::optional<bool> disable_file_locking = {});
NiniWriterFactory native_nini_writer_factory();
NiniDocumentFactory native_nini_document_factory();
NiniWriterFactory nini_byte_writer_factory(NiniFileStreamProvider);
NiniDocumentFactory nini_byte_document_factory(NiniFileStreamProvider);
}
