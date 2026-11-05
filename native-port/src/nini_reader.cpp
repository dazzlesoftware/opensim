// Port of Nini.Ini.IniReader/IniException, copyright (c) 2006 Brent R. Matzelle.
// MIT license: ../third_party/nini/LICENSE.
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/nini_reader.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
namespace opensim::framework {
namespace {
std::u16string number(std::int32_t value) {
    auto text = std::to_string(value);
    return {text.begin(), text.end()};
}
void increment(std::int32_t& value) {
    value = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) + 1);
}
[[noreturn]] void null_reference() {
    throw NiniReaderError(NiniReaderError::Kind::NullReference,
                          u"Object reference not set to an instance of an object.");
}
std::string replacing_utf8(std::u16string_view value) {
    std::u16string fixed;
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto ch = value[i];
        if (ch >= 0xd800 && ch <= 0xdbff) {
            if (i + 1 < value.size() && value[i + 1] >= 0xdc00 && value[i + 1] <= 0xdfff) {
                fixed.push_back(ch);
                fixed.push_back(value[++i]);
            } else
                fixed.push_back(0xfffd);
        } else if (ch >= 0xdc00 && ch <= 0xdfff)
            fixed.push_back(0xfffd);
        else
            fixed.push_back(ch);
    }
    return utf8(fixed);
}
class StringReader final : public NiniTextReader {
    std::u16string text_;
    std::size_t offset_ = 0;
    bool closed_ = false;
    void check() {
        if (closed_)
            throw NiniReaderError(NiniReaderError::Kind::DisposedReader,
                                  u"Cannot read from a closed TextReader.");
    }

  public:
    explicit StringReader(std::u16string text) : text_(std::move(text)) {
    }
    int peek() override {
        check();
        return offset_ == text_.size() ? -1 : static_cast<int>(text_[offset_]);
    }
    int read() override {
        check();
        return offset_ == text_.size() ? -1 : static_cast<int>(text_[offset_++]);
    }
    void close() override {
        closed_ = true;
    }
};
}
NiniReaderError::NiniReaderError(Kind k, std::u16string text, std::shared_ptr<NiniReaderPosition> p)
    : runtime_error("Nini reader error"), kind(k), reason(std::move(text)), position(std::move(p)) {
}
std::u16string NiniReaderError::message_utf16() const {
    if (kind == Kind::Ini && position)
        return reason + u" - Line: " + number(position->line) + u", Position: " + number(position->column) +
               u".";
    return reason;
}
const char* NiniReaderError::what() const noexcept {
    try {
        rendered_ = replacing_utf8(message_utf16());
        return rendered_.c_str();
    } catch (...) {
        return runtime_error::what();
    }
}
std::int32_t NiniReaderError::hresult() const noexcept {
    return std::bit_cast<std::int32_t>(kind == Kind::Ini             ? 0x80131501u
                                       : kind == Kind::NullReference ? 0x80004003u
                                       : kind == Kind::Argument      ? 0x80070057u
                                                                     : 0x80131622u);
}
const char* NiniReaderError::source_type() const noexcept {
    return kind == Kind::Ini             ? "Nini.Ini.IniException"
           : kind == Kind::NullReference ? "System.NullReferenceException"
           : kind == Kind::Argument      ? "System.ArgumentException"
                                         : "System.ObjectDisposedException";
}
std::shared_ptr<NiniTextReader> nini_string_reader(std::u16string value) {
    return std::make_shared<StringReader>(std::move(value));
}
struct NiniReader::State final : NiniReaderPosition, std::enable_shared_from_this<State> {
    std::shared_ptr<NiniTextReader> reader;
    std::u16string name, value, comment;
    NiniType type = NiniType::Empty;
    NiniReadState state = NiniReadState::Initial;
    bool ignore = false, has_comment = false, disposed = false, continuation = false, accept_comment = true,
         no_assignment = false, consume_all = false;
    std::shared_ptr<std::u16string> comments = std::make_shared<std::u16string>(u";"),
                                    assign = std::make_shared<std::u16string>(u"=");
    explicit State(std::shared_ptr<NiniTextReader> source) : reader(std::move(source)) {
    }
    ~State() {
        if (!disposed && reader)
            try {
                reader->close();
            } catch (...) {
            }
    }
    int peek() {
        if (!reader)
            null_reference();
        return reader->peek();
    }
    int take() {
        if (!reader)
            null_reference();
        auto ch = reader->read();
        if (ch == 10) {
            increment(line);
            column = 1;
        } else
            increment(column);
        return ch;
    }
    static bool eol(int ch) {
        return ch == 10 || ch == -1;
    }
    static bool white(int ch) {
        return ch == 32 || ch == 9 || ch == 13 || ch == 10;
    }
    static bool has(const std::u16string& chars, int ch) {
        for (auto c : chars)
            if (static_cast<int>(c) == ch)
                return true;
        return false;
    }
    bool is_comment(int ch) const {
        return has(*comments, ch);
    }
    bool is_assign(int ch) const {
        return has(*assign, ch);
    }
    void reset() {
        name.clear();
        value.clear();
        comment.clear();
        type = NiniType::Empty;
        has_comment = false;
    }
    static void trim_end(std::u16string& text) {
        while (!text.empty() && unicode_whitespace(text.back()))
            text.pop_back();
    }
    void skip() {
        while (white(peek()) && !eol(peek()))
            take();
    }
    [[noreturn]] void fail(std::u16string text) {
        throw NiniReaderError(NiniReaderError::Kind::Ini, std::move(text), shared_from_this());
    }
    void consume_end() {
        int ch;
        do {
            ch = take();
        } while (!eol(ch));
    }
    void read_comment() {
        skip();
        has_comment = true;
        int ch;
        do {
            ch = take();
            comment.push_back(static_cast<char16_t>(ch));
        } while (!eol(ch));
        trim_end(comment);
    }
    void search_comment() {
        auto ch = take();
        while (!eol(ch)) {
            if (is_comment(ch)) {
                if (ignore)
                    consume_end();
                else
                    read_comment();
                break;
            }
            ch = take();
        }
    }
    void read_value() {
        bool quoted = false;
        std::int32_t nonwhite = 0;
        skip();
        for (;;) {
            auto ch = peek();
            if (!white(ch))
                increment(nonwhite);
            if (!consume_all && ch == 34) {
                take();
                if (!quoted && nonwhite == 1) {
                    quoted = true;
                    continue;
                }
                break;
            }
            if (quoted && eol(ch))
                fail(u"Expected closing quote (\")");
            if (continuation && ch == 92) {
                std::u16string pending;
                pending.push_back(static_cast<char16_t>(take()));
                while (peek() != 10 && white(peek())) {
                    if (peek() != 13)
                        pending.push_back(static_cast<char16_t>(take()));
                    else
                        take();
                }
                if (peek() == 10) {
                    take();
                    continue;
                }
                value += pending;
            }
            if ((!consume_all && accept_comment && is_comment(ch) && !quoted) || eol(ch))
                break;
            value.push_back(static_cast<char16_t>(take()));
        }
        if (!quoted)
            trim_end(value);
    }
    void read_key() {
        type = NiniType::Key;
        for (;;) {
            auto ch = peek();
            if (is_assign(ch)) {
                take();
                break;
            }
            if (eol(ch)) {
                if (no_assignment)
                    break;
                std::u16string text = u"Expected assignment operator (";
                text.push_back((*assign)[0]);
                fail(text + u")");
            }
            name.push_back(static_cast<char16_t>(take()));
        }
        read_value();
        search_comment();
        trim_end(name);
    }
    void read_section() {
        type = NiniType::Section;
        take();
        for (;;) {
            auto ch = peek();
            if (ch == 93)
                break;
            if (eol(ch))
                fail(u"Expected section end (])");
            name.push_back(static_cast<char16_t>(take()));
        }
        consume_end();
        trim_end(name);
    }
    bool next() {
        auto ch = peek();
        reset();
        if (is_comment(ch)) {
            type = NiniType::Empty;
            take();
            read_comment();
            return true;
        }
        switch (ch) {
        case 9:
        case 13:
        case 32:
            skip();
            next();
            break;
        case 10:
            take();
            break;
        case 91:
            read_section();
            break;
        case -1:
            state = NiniReadState::EndOfFile;
            return false;
        default:
            read_key();
            break;
        }
        return true;
    }
    bool read() {
        state = NiniReadState::Interactive;
        return next();
    }
};
NiniReader::NiniReader(std::shared_ptr<NiniTextReader> reader)
    : state_(std::make_shared<State>(std::move(reader))) {
}
NiniReader::NiniReader(std::u16string text) : NiniReader(nini_string_reader(std::move(text))) {
}
bool NiniReader::read() {
    return state_->read();
}
bool NiniReader::move_to_next_section() {
    bool found;
    do {
        found = read();
    } while (state_->type != NiniType::Section && found);
    return found;
}
bool NiniReader::move_to_next_key() {
    bool found;
    do {
        found = read();
        if (state_->type == NiniType::Section) {
            found = false;
            break;
        }
    } while (state_->type != NiniType::Key && found);
    return found;
}
void NiniReader::close() {
    state_->reset();
    state_->state = NiniReadState::Closed;
    if (state_->reader)
        state_->reader->close();
}
void NiniReader::dispose() {
    if (!state_->disposed) {
        if (!state_->reader)
            null_reference();
        state_->reader->close();
        state_->disposed = true;
    }
}
const std::u16string& NiniReader::name() const {
    return state_->name;
}
const std::u16string& NiniReader::value() const {
    return state_->value;
}
std::optional<std::u16string> NiniReader::comment() const {
    return state_->has_comment ? std::optional(state_->comment) : std::nullopt;
}
NiniType NiniReader::type() const {
    return state_->type;
}
NiniReadState NiniReader::state() const {
    return state_->state;
}
std::int32_t NiniReader::line() const {
    return state_->line;
}
std::int32_t NiniReader::column() const {
    return state_->column;
}
bool NiniReader::ignore_comments() const {
    return state_->ignore;
}
void NiniReader::ignore_comments(bool v) {
    state_->ignore = v;
}
bool NiniReader::line_continuation() const {
    return state_->continuation;
}
void NiniReader::line_continuation(bool v) {
    state_->continuation = v;
}
bool NiniReader::accept_comment_after_key() const {
    return state_->accept_comment;
}
void NiniReader::accept_comment_after_key(bool v) {
    state_->accept_comment = v;
}
bool NiniReader::accept_no_assignment() const {
    return state_->no_assignment;
}
void NiniReader::accept_no_assignment(bool v) {
    state_->no_assignment = v;
}
bool NiniReader::consume_all_key_text() const {
    return state_->consume_all;
}
void NiniReader::consume_all_key_text(bool v) {
    state_->consume_all = v;
}
std::u16string NiniReader::comment_delimiters() const {
    return *state_->comments;
}
std::u16string NiniReader::assign_delimiters() const {
    return *state_->assign;
}
void NiniReader::comment_delimiters(std::shared_ptr<std::u16string> v) {
    if (!v)
        null_reference();
    if (v->empty())
        throw NiniReaderError(NiniReaderError::Kind::Argument, u"Must supply at least one delimiter");
    state_->comments = std::move(v);
}
void NiniReader::assign_delimiters(std::shared_ptr<std::u16string> v) {
    if (!v)
        null_reference();
    if (v->empty())
        throw NiniReaderError(NiniReaderError::Kind::Argument, u"Must supply at least one delimiter");
    state_->assign = std::move(v);
}
}
