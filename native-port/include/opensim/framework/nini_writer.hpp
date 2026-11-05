// Port of Nini.Ini.IniWriter, copyright (c) 2006 Brent R. Matzelle.
// MIT license: native-port/third_party/nini/LICENSE.
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
namespace opensim::framework {
using NiniWriterText = std::shared_ptr<const std::u16string>;
NiniWriterText nini_writer_text(std::optional<std::u16string>);
enum class NiniWriteState { Start, BeforeFirstSection, Section, Closed };
class NiniWriterError : public std::runtime_error {
public:
    enum class Kind { Argument, ArgumentNull, InvalidOperation, NullReference, DisposedWriter, Other };
    NiniWriterError(Kind, std::u16string, std::optional<std::u16string> parameter = {});
    Kind kind;
    std::u16string message;
    std::optional<std::u16string> parameter;
    const char* what() const noexcept override;
    const char* source_type() const noexcept;
    std::int32_t hresult() const noexcept;

private:
    mutable std::string rendered_;
};
class NiniWriterStream {
public:
    virtual ~NiniWriterStream() = default;
    virtual void close() = 0;
};
// Immutable nullable UTF16 handles retain payload/reference identity for a
// writer that saves its inputs. IniWriter never consults NewLine/WriteLine.
class NiniTextWriter {
public:
    virtual ~NiniTextWriter() = default;
    virtual void write(const NiniWriterText&) = 0;
    virtual void flush() = 0;
    virtual void close() = 0;
    virtual NiniWriterText to_string() = 0;
};
// Models the source `writer is StreamWriter` test and its captured BaseStream.
class NiniStreamWriter : public NiniTextWriter {
    std::shared_ptr<NiniWriterStream> stream_;

public:
    explicit NiniStreamWriter(std::shared_ptr<NiniWriterStream> stream) : stream_(std::move(stream)) {}
    std::shared_ptr<NiniWriterStream> base_stream() const {
        return stream_;
    }
};
class NiniStringWriter final : public NiniTextWriter {
    std::shared_ptr<std::u16string> buffer_;
    NiniWriterText newline_;
    bool closed_ = false;

public:
    NiniStringWriter();
    explicit NiniStringWriter(std::shared_ptr<std::u16string>);
    void write(const NiniWriterText&) override;
    void flush() override;
    void close() override;
    NiniWriterText to_string() override;
    std::shared_ptr<std::u16string> buffer() const {
        return buffer_;
    }
    NiniWriterText new_line() const {
        return newline_;
    }
    void new_line(NiniWriterText);
};
// File/byte-stream construction is an explicit provider boundary. Providers
// implement source FileStream/StreamWriter constructors, including their faults;
// the core never substitutes another encoding or silently omits file output.
struct NiniWriterFactory {
    std::function<std::shared_ptr<NiniWriterStream>(const NiniWriterText&, std::int32_t mode,
                                                    std::int32_t access, std::int32_t share)>
        open_file;
    std::function<std::shared_ptr<NiniStreamWriter>(std::shared_ptr<NiniWriterStream>)> stream_writer;
};
class NiniWriter {
public:
    explicit NiniWriter(std::shared_ptr<NiniTextWriter>);
    NiniWriter(std::shared_ptr<NiniWriterStream>, const NiniWriterFactory&);
    NiniWriter(NiniWriterText file_path, const NiniWriterFactory&);
    virtual ~NiniWriter() = default;
    std::int32_t indentation() const;
    void indentation(std::int32_t);
    bool use_value_quotes() const;
    void use_value_quotes(bool);
    char16_t comment_delimiter() const;
    void comment_delimiter(char16_t);
    char16_t assign_delimiter() const;
    void assign_delimiter(char16_t);
    NiniWriteState state() const;
    std::shared_ptr<NiniWriterStream> base_stream() const;
    void close();
    void flush();
    NiniWriterText to_string();
    void write_section(const NiniWriterText&);
    void write_section(const NiniWriterText&, const NiniWriterText& comment);
    void write_key(const NiniWriterText& key, const NiniWriterText& value);
    void write_key(const NiniWriterText& key, const NiniWriterText& value, const NiniWriterText& comment);
    void write_empty();
    void write_empty(const NiniWriterText& comment);
    void dispose();
    // Controlled finalizer dispatch for hosts/tests; arbitrary CLR GC timing is
    // not inferred from C++ lifetime. Last-owner cleanup suppresses close faults.
    void finalize();
    bool disposed() const;

protected:
    virtual void dispose(bool disposing);

private:
    struct State;
    std::shared_ptr<State> state_;
};
}
