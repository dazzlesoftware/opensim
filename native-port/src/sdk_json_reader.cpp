// Derived from bundled OpenMetaverse.StructuredData LitJson lexer/reader/mapper.
// BSD license: native-port/protocol/LIBOPENMETAVERSE-LICENSE.txt.
#include "opensim/network/sdk_json_reader.hpp"
#include "opensim/network/sdk_llsd_xml.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/text.hpp"
#include <map>
#include <charconv>
#include <algorithm>
namespace opensim::network {
SdkJsonError::SdkJsonError(std::u16string text)
    : std::invalid_argument(framework::utf8(text)), message(std::move(text)) {
}
namespace {
[[noreturn]] void error(std::u16string message) {
    throw SdkJsonError(std::move(message));
}
std::u16string token_name(int value) {
    static const char16_t* names[] = {u"None",      u"Number",   u"True",  u"False",      u"Null",
                                      u"CharSeq",   u"Char",     u"Text",  u"Object",     u"ObjectPrime",
                                      u"Pair",      u"PairRest", u"Array", u"ArrayPrime", u"Value",
                                      u"ValueRest", u"String",   u"End",   u"Epsilon"};
    return value >= 65536 && value <= 65554 ? std::u16string(names[value - 65536])
                                            : framework::utf16_replacing(std::to_string(value));
}
class Lexer {
    struct Context {
        Lexer& L;
        bool Return = false;
        int NextState = 0, StateStack = 0;
    };
    std::u16string_view text_;
    std::size_t position_ = 0;
    int input_buffer = 0, input_char = 0, state = 1, unichar = 0;
    bool allow_comments = true, allow_single_quoted_strings = true;
    std::u16string string_buffer;
    Context context{*this};
    bool GetChar() {
        input_char = NextChar();
        if (input_char != -1)
            return true;
        end_of_input = true;
        return false;
    }
    int NextChar() {
        if (input_buffer != 0) {
            int result = input_buffer;
            input_buffer = 0;
            return result;
        }
        return position_ < text_.size() ? text_[position_++] : -1;
    }
    void UngetChar() {
        input_buffer = input_char;
    }
    static int HexValue(int digit) {
        switch (digit) {
        case 65:
        case 97:
            return 10;
        case 66:
        case 98:
            return 11;
        case 67:
        case 99:
            return 12;
        case 68:
        case 100:
            return 13;
        case 69:
        case 101:
            return 14;
        case 70:
        case 102:
            return 15;
        default:
            return digit - 48;
        }
    }
    static char16_t ProcessEscChar(int esc_char) {
        switch (esc_char) {
        case 34:
        case 39:
        case 47:
        case 92:
            return static_cast<char16_t>(esc_char);
        case 110:
            return '\n';
        case 116:
            return '\t';
        case 114:
            return '\r';
        case 98:
            return '\b';
        case 102:
            return '\f';
        default:
            return '?';
        }
    }
    static bool State1(Context& ctx) {
        while (ctx.L.GetChar()) {
            if (ctx.L.input_char == 32 || (ctx.L.input_char >= 9 && ctx.L.input_char <= 13)) {
                continue;
            }
            if (ctx.L.input_char >= 49 && ctx.L.input_char <= 57) {
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                ctx.NextState = 3;
                return true;
            }
            switch (ctx.L.input_char) {
            case 34:
                ctx.NextState = 19;
                ctx.Return = true;
                return true;
            case 44:
            case 58:
            case 91:
            case 93:
            case 123:
            case 125:
                ctx.NextState = 1;
                ctx.Return = true;
                return true;
            case 45:
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                ctx.NextState = 2;
                return true;
            case 48:
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                ctx.NextState = 4;
                return true;
            case 102:
                ctx.NextState = 12;
                return true;
            case 110:
                ctx.NextState = 16;
                return true;
            case 116:
                ctx.NextState = 9;
                return true;
            case 39:
                if (!ctx.L.allow_single_quoted_strings) {
                    return false;
                }
                ctx.L.input_char = 34;
                ctx.NextState = 23;
                ctx.Return = true;
                return true;
            case 47:
                if (!ctx.L.allow_comments) {
                    return false;
                }
                ctx.NextState = 25;
                return true;
            default:
                return false;
            }
        }
        return true;
    }
    static bool State2(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char >= 49 && ctx.L.input_char <= 57) {
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
            ctx.NextState = 3;
            return true;
        }
        if (ctx.L.input_char == 48) {
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
            ctx.NextState = 4;
            return true;
        }
        return false;
    }
    static bool State3(Context& ctx) {
        while (ctx.L.GetChar()) {
            if (ctx.L.input_char >= 48 && ctx.L.input_char <= 57) {
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                continue;
            }
            if (ctx.L.input_char == 32 || (ctx.L.input_char >= 9 && ctx.L.input_char <= 13)) {
                ctx.Return = true;
                ctx.NextState = 1;
                return true;
            }
            switch (ctx.L.input_char) {
            case 44:
            case 93:
            case 125:
                ctx.L.UngetChar();
                ctx.Return = true;
                ctx.NextState = 1;
                return true;
            case 46:
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                ctx.NextState = 5;
                return true;
            case 69:
            case 101:
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                ctx.NextState = 7;
                return true;
            default:
                return false;
            }
        }
        return true;
    }
    static bool State4(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 32 || (ctx.L.input_char >= 9 && ctx.L.input_char <= 13)) {
            ctx.Return = true;
            ctx.NextState = 1;
            return true;
        }
        switch (ctx.L.input_char) {
        case 44:
        case 93:
        case 125:
            ctx.L.UngetChar();
            ctx.Return = true;
            ctx.NextState = 1;
            return true;
        case 46:
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
            ctx.NextState = 5;
            return true;
        case 69:
        case 101:
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
            ctx.NextState = 7;
            return true;
        default:
            return false;
        }
    }
    static bool State5(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char >= 48 && ctx.L.input_char <= 57) {
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
            ctx.NextState = 6;
            return true;
        }
        return false;
    }
    static bool State6(Context& ctx) {
        while (ctx.L.GetChar()) {
            if (ctx.L.input_char >= 48 && ctx.L.input_char <= 57) {
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                continue;
            }
            if (ctx.L.input_char == 32 || (ctx.L.input_char >= 9 && ctx.L.input_char <= 13)) {
                ctx.Return = true;
                ctx.NextState = 1;
                return true;
            }
            switch (ctx.L.input_char) {
            case 44:
            case 93:
            case 125:
                ctx.L.UngetChar();
                ctx.Return = true;
                ctx.NextState = 1;
                return true;
            case 69:
            case 101:
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                ctx.NextState = 7;
                return true;
            default:
                return false;
            }
        }
        return true;
    }
    static bool State7(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char >= 48 && ctx.L.input_char <= 57) {
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
            ctx.NextState = 8;
            return true;
        }
        int num = ctx.L.input_char;
        if (num == 43 || num == 45) {
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
            ctx.NextState = 8;
            return true;
        }
        return false;
    }
    static bool State8(Context& ctx) {
        while (ctx.L.GetChar()) {
            if (ctx.L.input_char >= 48 && ctx.L.input_char <= 57) {
                ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
                continue;
            }
            if (ctx.L.input_char == 32 || (ctx.L.input_char >= 9 && ctx.L.input_char <= 13)) {
                ctx.Return = true;
                ctx.NextState = 1;
                return true;
            }
            int num = ctx.L.input_char;
            if (num == 44 || num == 93 || num == 125) {
                ctx.L.UngetChar();
                ctx.Return = true;
                ctx.NextState = 1;
                return true;
            }
            return false;
        }
        return true;
    }
    static bool State9(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 114) {
            ctx.NextState = 10;
            return true;
        }
        return false;
    }
    static bool State10(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 117) {
            ctx.NextState = 11;
            return true;
        }
        return false;
    }
    static bool State11(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 101) {
            ctx.Return = true;
            ctx.NextState = 1;
            return true;
        }
        return false;
    }
    static bool State12(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 97) {
            ctx.NextState = 13;
            return true;
        }
        return false;
    }
    static bool State13(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 108) {
            ctx.NextState = 14;
            return true;
        }
        return false;
    }
    static bool State14(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 115) {
            ctx.NextState = 15;
            return true;
        }
        return false;
    }
    static bool State15(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 101) {
            ctx.Return = true;
            ctx.NextState = 1;
            return true;
        }
        return false;
    }
    static bool State16(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 117) {
            ctx.NextState = 17;
            return true;
        }
        return false;
    }
    static bool State17(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 108) {
            ctx.NextState = 18;
            return true;
        }
        return false;
    }
    static bool State18(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 108) {
            ctx.Return = true;
            ctx.NextState = 1;
            return true;
        }
        return false;
    }
    static bool State19(Context& ctx) {
        while (ctx.L.GetChar()) {
            switch (ctx.L.input_char) {
            case 34:
                ctx.L.UngetChar();
                ctx.Return = true;
                ctx.NextState = 20;
                return true;
            case 92:
                ctx.StateStack = 19;
                ctx.NextState = 21;
                return true;
            }
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
        }
        return true;
    }
    static bool State20(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 34) {
            ctx.Return = true;
            ctx.NextState = 1;
            return true;
        }
        return false;
    }
    static bool State21(Context& ctx) {
        ctx.L.GetChar();
        switch (ctx.L.input_char) {
        case 117:
            ctx.NextState = 22;
            return true;
        case 34:
        case 39:
        case 47:
        case 92:
        case 98:
        case 102:
        case 110:
        case 114:
        case 116:
            ctx.L.string_buffer.push_back(ProcessEscChar(ctx.L.input_char));
            ctx.NextState = ctx.StateStack;
            return true;
        default:
            return false;
        }
    }
    static bool State22(Context& ctx) {
        int num = 0;
        int num2 = 4096;
        ctx.L.unichar = 0;
        while (ctx.L.GetChar()) {
            if ((ctx.L.input_char >= 48 && ctx.L.input_char <= 57) ||
                (ctx.L.input_char >= 65 && ctx.L.input_char <= 70) ||
                (ctx.L.input_char >= 97 && ctx.L.input_char <= 102)) {
                ctx.L.unichar += HexValue(ctx.L.input_char) * num2;
                num++;
                num2 /= 16;
                if (num == 4) {
                    ctx.L.string_buffer.push_back(static_cast<char16_t>(ctx.L.unichar));
                    ctx.NextState = ctx.StateStack;
                    return true;
                }
                continue;
            }
            return false;
        }
        return true;
    }
    static bool State23(Context& ctx) {
        while (ctx.L.GetChar()) {
            switch (ctx.L.input_char) {
            case 39:
                ctx.L.UngetChar();
                ctx.Return = true;
                ctx.NextState = 24;
                return true;
            case 92:
                ctx.StateStack = 23;
                ctx.NextState = 21;
                return true;
            }
            ctx.L.string_buffer.push_back((char16_t)ctx.L.input_char);
        }
        return true;
    }
    static bool State24(Context& ctx) {
        ctx.L.GetChar();
        if (ctx.L.input_char == 39) {
            ctx.L.input_char = 34;
            ctx.Return = true;
            ctx.NextState = 1;
            return true;
        }
        return false;
    }
    static bool State25(Context& ctx) {
        ctx.L.GetChar();
        switch (ctx.L.input_char) {
        case 42:
            ctx.NextState = 27;
            return true;
        case 47:
            ctx.NextState = 26;
            return true;
        default:
            return false;
        }
    }
    static bool State26(Context& ctx) {
        while (ctx.L.GetChar()) {
            if (ctx.L.input_char == 10) {
                ctx.NextState = 1;
                return true;
            }
        }
        return true;
    }
    static bool State27(Context& ctx) {
        while (ctx.L.GetChar()) {
            if (ctx.L.input_char == 42) {
                ctx.NextState = 28;
                return true;
            }
        }
        return true;
    }
    static bool State28(Context& ctx) {
        while (ctx.L.GetChar()) {
            if (ctx.L.input_char != 42) {
                if (ctx.L.input_char == 47) {
                    ctx.NextState = 1;
                    return true;
                }
                ctx.NextState = 27;
                return true;
            }
        }
        return true;
    }

  public:
    bool end_of_input = false;
    int token = 0;
    std::u16string string_value;
    explicit Lexer(std::u16string_view text) : text_(text) {
    }
    bool next() {
        static constexpr int returns[] = {65542, 0, 65537, 65537, 0,     65537, 0, 65537, 0,     0,
                                          65538, 0, 0,     0,     65539, 0,     0, 65540, 65541, 65542,
                                          0,     0, 65541, 65542, 0,     0,     0, 0};
        context.Return = false;
        while (true) {
            bool valid = false;
            switch (state) {
            case 1:
                valid = State1(context);
                break;
            case 2:
                valid = State2(context);
                break;
            case 3:
                valid = State3(context);
                break;
            case 4:
                valid = State4(context);
                break;
            case 5:
                valid = State5(context);
                break;
            case 6:
                valid = State6(context);
                break;
            case 7:
                valid = State7(context);
                break;
            case 8:
                valid = State8(context);
                break;
            case 9:
                valid = State9(context);
                break;
            case 10:
                valid = State10(context);
                break;
            case 11:
                valid = State11(context);
                break;
            case 12:
                valid = State12(context);
                break;
            case 13:
                valid = State13(context);
                break;
            case 14:
                valid = State14(context);
                break;
            case 15:
                valid = State15(context);
                break;
            case 16:
                valid = State16(context);
                break;
            case 17:
                valid = State17(context);
                break;
            case 18:
                valid = State18(context);
                break;
            case 19:
                valid = State19(context);
                break;
            case 20:
                valid = State20(context);
                break;
            case 21:
                valid = State21(context);
                break;
            case 22:
                valid = State22(context);
                break;
            case 23:
                valid = State23(context);
                break;
            case 24:
                valid = State24(context);
                break;
            case 25:
                valid = State25(context);
                break;
            case 26:
                valid = State26(context);
                break;
            case 27:
                valid = State27(context);
                break;
            case 28:
                valid = State28(context);
                break;
            }
            if (!valid)
                error(u"Invalid character '" + std::u16string(1, static_cast<char16_t>(input_char)) +
                      u"' in input string");
            if (end_of_input)
                return false;
            if (context.Return)
                break;
            state = context.NextState;
        }
        string_value = std::move(string_buffer);
        string_buffer.clear();
        token = returns[state - 1];
        if (token == 65542)
            token = input_char;
        state = context.NextState;
        return true;
    }
};
enum class Token {
    None,
    ArrayStart,
    ArrayEnd,
    ObjectStart,
    ObjectEnd,
    String,
    PropertyName,
    Double,
    Int,
    Long,
    Boolean,
    Null
};
class Reader {
    Lexer lexer_;
    std::vector<int> stack_{65553, 65543};
    bool end_input_ = false, end_json_ = false, started_ = false, in_string_ = false, return_ = false;
    int input_ = 0, symbol_ = 0;
    int peek_stack() const {
        if (stack_.empty())
            throw SdkJsonValueError("Stack empty.");
        return stack_.back();
    }
    bool next() {
        if (end_input_)
            return false;
        lexer_.next();
        if (lexer_.end_of_input) {
            end_input_ = end_json_ = true;
            return false;
        }
        input_ = lexer_.token;
        return true;
    }
    void number() {
        auto number_text = framework::utf8(lexer_.string_value);
        double real = 0;
        if (number_text.find_first_of(".eE") != number_text.npos &&
            try_parse_invariant_real(number_text, real)) {
            token = Token::Double;
            value.kind = Value::Kind::Real;
            value.real = real;
            return;
        }
        std::int64_t number = 0;
        auto parsed = std::from_chars(number_text.data(), number_text.data() + number_text.size(), number);
        if (parsed.ec == std::errc{} && parsed.ptr == number_text.data() + number_text.size()) {
            if (number >= INT32_MIN && number <= INT32_MAX) {
                token = Token::Int;
                value = Value(static_cast<std::int32_t>(number));
            } else {
                token = Token::Long;
                value = Value::long_integer(number);
            }
            return;
        }
        std::uint64_t wide = 0;
        auto unsigned_parse =
            std::from_chars(number_text.data(), number_text.data() + number_text.size(), wide);
        if (unsigned_parse.ec == std::errc{} &&
            unsigned_parse.ptr == number_text.data() + number_text.size()) {
            token = Token::Long;
            unsigned_long = true;
            return;
        }
        token = Token::Int;
        value = Value(std::int32_t{0});
    }
    void process() {
        switch (symbol_) {
        case 91:
            token = Token::ArrayStart;
            return_ = true;
            break;
        case 93:
            token = Token::ArrayEnd;
            return_ = true;
            break;
        case 123:
            token = Token::ObjectStart;
            return_ = true;
            break;
        case 125:
            token = Token::ObjectEnd;
            return_ = true;
            break;
        case 34:
            if (in_string_) {
                in_string_ = false;
                return_ = true;
            } else {
                if (token == Token::None)
                    token = Token::String;
                in_string_ = true;
            }
            break;
        case 65541:
            text = lexer_.string_value;
            break;
        case 65539:
            token = Token::Boolean;
            value = Value(false);
            return_ = true;
            break;
        case 65540:
            token = Token::Null;
            return_ = true;
            break;
        case 65537:
            number();
            return_ = true;
            break;
        case 65546:
            token = Token::PropertyName;
            break;
        case 65538:
            token = Token::Boolean;
            value = Value(true);
            return_ = true;
            break;
        }
    }
    static const std::map<std::pair<int, int>, std::vector<int>>& table() {
        static const std::map<std::pair<int, int>, std::vector<int>> value = {
            {{65548, 91}, {91, 65549}},
            {{65549, 34}, {65550, 65551, 93}},
            {{65549, 91}, {65550, 65551, 93}},
            {{65549, 93}, {93}},
            {{65549, 123}, {65550, 65551, 93}},
            {{65549, 65537}, {65550, 65551, 93}},
            {{65549, 65538}, {65550, 65551, 93}},
            {{65549, 65539}, {65550, 65551, 93}},
            {{65549, 65540}, {65550, 65551, 93}},
            {{65544, 123}, {123, 65545}},
            {{65545, 34}, {65546, 65547, 125}},
            {{65545, 125}, {125}},
            {{65546, 34}, {65552, 58, 65550}},
            {{65547, 44}, {44, 65546, 65547}},
            {{65547, 125}, {65554}},
            {{65552, 34}, {34, 65541, 34}},
            {{65543, 91}, {65548}},
            {{65543, 123}, {65544}},
            {{65550, 34}, {65552}},
            {{65550, 91}, {65548}},
            {{65550, 123}, {65544}},
            {{65550, 65537}, {65537}},
            {{65550, 65538}, {65538}},
            {{65550, 65539}, {65539}},
            {{65550, 65540}, {65540}},
            {{65551, 44}, {44, 65550, 65551}},
            {{65551, 93}, {65554}}};
        return value;
    }

  public:
    bool unsigned_long = false;
    Token token = Token::None;
    Value value;
    std::u16string text;
    explicit Reader(std::u16string_view input) : lexer_(input) {
    }
    bool read() {
        if (end_input_)
            return false;
        if (end_json_) {
            end_json_ = false;
            stack_ = {65553, 65543};
        }
        in_string_ = return_ = false;
        token = Token::None;
        value = {};
        unsigned_long = false;
        text.clear();
        if (!started_) {
            started_ = true;
            if (!next())
                return false;
        }
        while (true) {
            if (return_) {
                if (peek_stack() == 65553)
                    end_json_ = true;
                return true;
            }
            symbol_ = peek_stack();
            stack_.pop_back();
            process();
            if (symbol_ == input_) {
                if (!next())
                    break;
                continue;
            }
            auto entry = table().find({symbol_, input_});
            if (entry == table().end())
                error(u"Invalid token '" + token_name(input_) + u"' in input string");
            if (entry->second[0] != 65554)
                for (auto i = entry->second.rbegin(); i != entry->second.rend(); ++i)
                    stack_.push_back(*i);
        }
        if (peek_stack() != 65553)
            error(u"Input doesn't evaluate to proper JSON text");
        return return_;
    }
};
Value mapped(Reader& reader, std::size_t depth) {
    if (depth > 512)
        throw SdkJsonValueError("OSD JSON nesting exceeds native limit");
    reader.read();
    switch (reader.token) {
    case Token::ArrayEnd:
    case Token::Null:
        return {};
    case Token::String: {
        if (reader.text.empty())
            return {};
        Value value(framework::utf8(reader.text));
        value.sdk_string = std::make_shared<const std::u16string>(reader.text);
        return value;
    }
    case Token::Long:
        if (reader.unsigned_long)
            throw SdkJsonValueError("Unable to cast object of type 'System.UInt64' to type 'System.Int64'.");
        return reader.value;
    case Token::Double:
    case Token::Int:
    case Token::Boolean:
        return reader.value;
    case Token::ArrayStart: {
        Value value;
        value.kind = Value::Kind::Array;
        while (true) {
            auto child = mapped(reader, depth + 1);
            if (child.kind == Value::Kind::Undefined && reader.token == Token::ArrayEnd)
                break;
            value.array.push_back(std::move(child));
        }
        return value;
    }
    case Token::ObjectStart: {
        Value value;
        value.kind = Value::Kind::Map;
        while (true) {
            reader.read();
            if (reader.token == Token::ObjectEnd)
                break;
            auto key = sdk_map_key_bytes(reader.text);
            value.set(key, mapped(reader, depth + 1));
        }
        return value;
    }
    default:
        return {};
    }
}
}
Value read_sdk_json(std::u16string_view text) {
    Reader reader(text);
    return mapped(reader, 0);
}
}
