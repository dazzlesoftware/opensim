// Port of Nini.Ini.IniWriter, copyright (c) 2006 Brent R. Matzelle.
// MIT license: ../third_party/nini/LICENSE.
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/nini_writer.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
namespace opensim::framework {
namespace {
NiniWriterText empty() {
    return runtime_empty_text();
}
NiniWriterText eol() {
    static const auto value = std::make_shared<const std::u16string>(u"\r\n");
    return value;
}
NiniWriterText platform_line() {
#ifdef _WIN32
    return eol();
#else
    static const auto value = std::make_shared<const std::u16string>(u"\n");
    return value;
#endif
}
NiniWriterText text(std::u16string value) {
    return value.empty() ? empty() : std::make_shared<const std::u16string>(std::move(value));
}
const std::u16string& value(const NiniWriterText& text) {
    static const std::u16string none;
    return text ? *text : none;
}
NiniWriterText concat(const NiniWriterText& first, const NiniWriterText& second) {
    if (!first || first->empty())
        return second ? second : empty();
    if (!second || second->empty())
        return first;
    return text(*first + *second);
}
[[noreturn]] void null_reference() {
    throw NiniWriterError(NiniWriterError::Kind::NullReference,
                          u"Object reference not set to an instance of an object.");
}
std::shared_ptr<NiniStreamWriter> make_stream(std::shared_ptr<NiniWriterStream> stream,
                                              const NiniWriterFactory& factory) {
    if (!factory.stream_writer)
        throw std::invalid_argument("Missing Nini StreamWriter constructor provider");
    auto writer = factory.stream_writer(std::move(stream));
    if (!writer)
        throw std::logic_error("Nini StreamWriter constructor provider returned null");
    return writer;
}
std::shared_ptr<NiniWriterStream> make_file(const NiniWriterText& path, const NiniWriterFactory& factory) {
    if (!factory.open_file)
        throw std::invalid_argument("Missing Nini FileStream constructor provider");
    return factory.open_file(path, 2, 2, 0);
}
}
NiniWriterText nini_writer_text(std::optional<std::u16string> value) {
    return value ? text(std::move(*value)) : nullptr;
}
NiniWriterError::NiniWriterError(Kind kind, std::u16string message, std::optional<std::u16string> parameter)
    : runtime_error("Nini writer error"), kind(kind), message(std::move(message)),
      parameter(std::move(parameter)) {
}
const char* NiniWriterError::what() const noexcept {
    try {
        rendered_ = utf8(message);
        return rendered_.c_str();
    } catch (...) {
        return runtime_error::what();
    }
}
const char* NiniWriterError::source_type() const noexcept {
    switch (kind) {
    case Kind::Argument:
        return "System.ArgumentException";
    case Kind::ArgumentNull:
        return "System.ArgumentNullException";
    case Kind::InvalidOperation:
        return "System.InvalidOperationException";
    case Kind::NullReference:
        return "System.NullReferenceException";
    case Kind::DisposedWriter:
        return "System.ObjectDisposedException";
    default:
        return "System.Exception";
    }
}
std::int32_t NiniWriterError::hresult() const noexcept {
    std::uint32_t value = 0x80131500u;
    switch (kind) {
    case Kind::Argument:
        value = 0x80070057u;
        break;
    case Kind::ArgumentNull:
    case Kind::NullReference:
        value = 0x80004003u;
        break;
    case Kind::InvalidOperation:
        value = 0x80131509u;
        break;
    case Kind::DisposedWriter:
        value = 0x80131622u;
        break;
    default:
        break;
    }
    return std::bit_cast<std::int32_t>(value);
}
NiniStringWriter::NiniStringWriter() : NiniStringWriter(std::make_shared<std::u16string>()) {
}
NiniStringWriter::NiniStringWriter(std::shared_ptr<std::u16string> buffer)
    : buffer_(std::move(buffer)), newline_(platform_line()) {
    if (!buffer_)
        throw NiniWriterError(NiniWriterError::Kind::ArgumentNull, u"Value cannot be null. (Parameter 'sb')",
                              u"sb");
}
void NiniStringWriter::write(const NiniWriterText& text) {
    if (closed_)
        throw NiniWriterError(NiniWriterError::Kind::DisposedWriter, u"Cannot write to a closed TextWriter.");
    if (text)
        buffer_->append(*text);
}
void NiniStringWriter::flush() {
}
void NiniStringWriter::close() {
    closed_ = true;
}
NiniWriterText NiniStringWriter::to_string() {
    return text(*buffer_);
}
void NiniStringWriter::new_line(NiniWriterText text) {
    newline_ = text ? std::move(text) : platform_line();
}
struct NiniWriter::State {
    std::int32_t indentation = 0;
    bool quotes = false, disposed = false;
    NiniWriteState state = NiniWriteState::Start;
    char16_t comment = u';', assign = u'=';
    std::shared_ptr<NiniTextWriter> writer;
    std::shared_ptr<NiniWriterStream> stream;
    std::u16string indentation_buffer;
    explicit State(std::shared_ptr<NiniTextWriter> input) : writer(std::move(input)) {
        if (auto typed = std::dynamic_pointer_cast<NiniStreamWriter>(writer))
            stream = typed->base_stream();
    }
    ~State() {
        if (!disposed)
            try {
                dispose();
            } catch (...) {
            }
    }
    void validate() {
        if (state == NiniWriteState::Closed)
            throw NiniWriterError(NiniWriterError::Kind::InvalidOperation, u"The writer is closed");
    }
    void validate_key() {
        validate();
        if (state == NiniWriteState::Start || state == NiniWriteState::BeforeFirstSection)
            throw NiniWriterError(NiniWriterError::Kind::InvalidOperation, u"The WriteState is not Section");
        if (state == NiniWriteState::Closed)
            throw NiniWriterError(NiniWriterError::Kind::InvalidOperation, u"The writer is closed");
    }
    NiniWriterText comment_text(const NiniWriterText& input) {
        return input ? text(std::u16string(u" ") + comment + u" " + *input) : empty();
    }
    NiniWriterText key_value(const NiniWriterText& input) {
        auto result = input;
        if (quotes)
            result = text(u"\"" + value(input) + u"\"");
        if (!result)
            null_reference();
        if (result->find(u'\n') == result->npos)
            return result;
        std::u16string copy;
        copy.reserve(result->size());
        for (auto ch : *result)
            if (ch != u'\n')
                copy.push_back(ch);
        return text(std::move(copy));
    }
    void line(const NiniWriterText& input) {
        auto payload = concat(text(indentation_buffer), concat(input, eol()));
        if (!writer)
            null_reference();
        auto held = writer;
        held->write(payload);
    }
    void close() {
        if (!writer)
            null_reference();
        auto held = writer;
        held->close();
        state = NiniWriteState::Closed;
    }
    void dispose() {
        if (disposed)
            return;
        if (writer) {
            auto held = writer;
            held->close();
        }
        if (stream) {
            auto held = stream;
            held->close();
        }
        disposed = true;
    }
};
NiniWriter::NiniWriter(std::shared_ptr<NiniTextWriter> writer)
    : state_(std::make_shared<State>(std::move(writer))) {
}
NiniWriter::NiniWriter(std::shared_ptr<NiniWriterStream> stream, const NiniWriterFactory& factory)
    : NiniWriter(make_stream(std::move(stream), factory)) {
}
NiniWriter::NiniWriter(NiniWriterText path, const NiniWriterFactory& factory)
    : NiniWriter(make_file(path, factory), factory) {
}
std::int32_t NiniWriter::indentation() const {
    return state_->indentation;
}
void NiniWriter::indentation(std::int32_t count) {
    auto s = state_;
    if (count < 0)
        throw NiniWriterError(NiniWriterError::Kind::Argument, u"Negative values are illegal");
    s->indentation = count;
    s->indentation_buffer.erase();
    for (std::int32_t index = 0; index < count; ++index)
        s->indentation_buffer.push_back(u' ');
}
bool NiniWriter::use_value_quotes() const {
    return state_->quotes;
}
void NiniWriter::use_value_quotes(bool value) {
    state_->quotes = value;
}
char16_t NiniWriter::comment_delimiter() const {
    return state_->comment;
}
void NiniWriter::comment_delimiter(char16_t value) {
    state_->comment = value;
}
char16_t NiniWriter::assign_delimiter() const {
    return state_->assign;
}
void NiniWriter::assign_delimiter(char16_t value) {
    state_->assign = value;
}
NiniWriteState NiniWriter::state() const {
    return state_->state;
}
std::shared_ptr<NiniWriterStream> NiniWriter::base_stream() const {
    return state_->stream;
}
void NiniWriter::close() {
    auto s = state_;
    s->close();
}
void NiniWriter::flush() {
    auto s = state_;
    if (!s->writer)
        null_reference();
    auto held = s->writer;
    held->flush();
}
NiniWriterText NiniWriter::to_string() {
    auto s = state_;
    if (!s->writer)
        null_reference();
    auto held = s->writer;
    return held->to_string();
}
void NiniWriter::write_section(const NiniWriterText& section) {
    auto s = state_;
    s->validate();
    s->state = NiniWriteState::Section;
    s->line(text(u"[" + value(section) + u"]"));
}
void NiniWriter::write_section(const NiniWriterText& section, const NiniWriterText& comment) {
    auto s = state_;
    s->validate();
    s->state = NiniWriteState::Section;
    s->line(text(u"[" + value(section) + u"]" + value(s->comment_text(comment))));
}
void NiniWriter::write_key(const NiniWriterText& key, const NiniWriterText& input) {
    auto s = state_;
    s->validate_key();
    auto prepared = s->key_value(input);
    s->line(text(value(key) + u" " + s->assign + u" " + value(prepared)));
}
void NiniWriter::write_key(const NiniWriterText& key, const NiniWriterText& input,
                           const NiniWriterText& comment) {
    auto s = state_;
    s->validate_key();
    auto prepared = s->key_value(input);
    s->line(text(value(key) + u" " + s->assign + u" " + value(prepared) + value(s->comment_text(comment))));
}
void NiniWriter::write_empty() {
    auto s = state_;
    s->validate();
    if (s->state == NiniWriteState::Start)
        s->state = NiniWriteState::BeforeFirstSection;
    s->line(empty());
}
void NiniWriter::write_empty(const NiniWriterText& comment) {
    auto s = state_;
    s->validate();
    if (s->state == NiniWriteState::Start)
        s->state = NiniWriteState::BeforeFirstSection;
    s->line(comment ? text(std::u16string(1, s->comment) + u" " + *comment) : empty());
}
void NiniWriter::dispose() {
    auto s = state_;
    dispose(true);
}
void NiniWriter::finalize() {
    auto s = state_;
    dispose(false);
}
void NiniWriter::dispose(bool) {
    auto s = state_;
    s->dispose();
}
bool NiniWriter::disposed() const {
    return state_->disposed;
}
}
