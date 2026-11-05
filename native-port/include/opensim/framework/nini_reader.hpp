// Port of Nini.Ini.IniReader/IniException, copyright (c) 2006 Brent R. Matzelle.
// MIT license: native-port/third_party/nini/LICENSE.
// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
namespace opensim::framework {
enum class NiniType { Section, Key, Empty };
enum class NiniReadState { Closed, EndOfFile, Error, Initial, Interactive };
struct NiniReaderPosition {
    std::int32_t line = 1, column = 1;
    virtual ~NiniReaderPosition() = default;
};
class NiniReaderError : public std::runtime_error {
public:
    enum class Kind { Ini, NullReference, Argument, DisposedReader };
    NiniReaderError(Kind, std::u16string, std::shared_ptr<NiniReaderPosition> position = {});
    Kind kind;
    std::u16string reason;
    std::shared_ptr<NiniReaderPosition> position;
    std::u16string message_utf16() const;
    const char* what() const noexcept override;
    std::int32_t hresult() const noexcept;
    const char* source_type() const noexcept;

private:
    mutable std::string rendered_;
};
class NiniTextReader {
public:
    virtual ~NiniTextReader() = default;
    virtual int peek() = 0;
    virtual int read() = 0;
    virtual void close() = 0;
};
std::shared_ptr<NiniTextReader> nini_string_reader(std::u16string);
// Exact Nini.Ini.IniReader token/state surface over UTF16 units. The supplied
// reader may be null. Delimiter setters retain their mutable array identity;
// getter results are copies, as in the original.
class NiniReader {
public:
    explicit NiniReader(std::shared_ptr<NiniTextReader>);
    explicit NiniReader(std::u16string);
    bool read();
    bool move_to_next_section();
    bool move_to_next_key();
    void close();
    void dispose();
    const std::u16string& name() const;
    const std::u16string& value() const;
    std::optional<std::u16string> comment() const;
    NiniType type() const;
    NiniReadState state() const;
    std::int32_t line() const;
    std::int32_t column() const;
    bool ignore_comments() const;
    void ignore_comments(bool);
    bool line_continuation() const;
    void line_continuation(bool);
    bool accept_comment_after_key() const;
    void accept_comment_after_key(bool);
    bool accept_no_assignment() const;
    void accept_no_assignment(bool);
    bool consume_all_key_text() const;
    void consume_all_key_text(bool);
    std::u16string comment_delimiters() const;
    std::u16string assign_delimiters() const;
    void comment_delimiters(std::shared_ptr<std::u16string>);
    void assign_delimiters(std::shared_ptr<std::u16string>);

private:
    struct State;
    std::shared_ptr<State> state_;
};
}
