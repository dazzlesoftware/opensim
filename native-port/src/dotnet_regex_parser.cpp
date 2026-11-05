// Copyright (c) .NET Foundation and Contributors.
// .NET8.0.31 RegexParser; MIT third_party/dotnet_collation/LICENSE.
#include "opensim/framework/dotnet_regex_parser.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
namespace opensim::framework {
namespace {
using K = DotnetRegexNodeKind;
using Text = DotnetRegexNodeText;
using namespace std::literals;
#include "dotnet_regex_parser_resources.inc"
[[noreturn]] void parser_null() {
    throw DotnetRegexClassError("System.NullReferenceException", 0x80004003u,
                                "Object reference not set to an instance of an object.");
}
[[noreturn]] void parser_index() {
    throw DotnetRegexClassError("System.IndexOutOfRangeException", 0x80131508u,
                                "Index was outside the bounds of the array.");
}
Text text(std::u16string value) {
    return value.empty() ? runtime_empty_text() : std::make_shared<const std::u16string>(std::move(value));
}
std::u16string number(int value) {
    return utf16(std::to_string(value));
}
bool digit(char16_t c) {
    return c >= u'0' && c <= u'9';
}
constexpr std::uint8_t category[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 0, 0, 2, 3, 0, 0, 0, 3, 3, 4, 4, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 0, 3, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 3, 0, 0, 0};
bool special(char16_t c) {
    return c < 128 && category[c] >= 3;
}
bool special_space(char16_t c) {
    return c < 128 && category[c] >= 1;
}
bool quantifier(char16_t c) {
    return c < 128 && category[c] >= 4;
}
bool space(char16_t c) {
    return c < 128 && category[c] == 1;
}
}
class DotnetRegexParser {
    std::u16string pattern_;
    int pos_ = 0, options_, autocap_ = 0, capcount_ = 0, captop_ = 0;
    DotnetRegexCultureName culture_;
    DotnetRegexExecutionStack execution_stack_;
    DotnetRegexCaseBehavior case_behavior_ = DotnetRegexCaseBehavior::NotSet;
    bool ignore_next_paren_ = false, ignore_case_backrefs_ = false;
    std::map<int, int> caps_;
    bool caps_present_ = true;
    int capsize_ = 0;
    std::optional<std::map<std::u16string, int>> capnames_;
    std::optional<std::vector<std::u16string>> capnamelist_;
    std::optional<std::vector<int>> capnumlist_;
    std::vector<int> option_stack_;
    std::shared_ptr<DotnetRegexNodeArena> arena_ = std::make_shared<DotnetRegexNodeArena>();
    DotnetRegexNode *stack_ = nullptr, *group_ = nullptr, *alternation_ = nullptr, *concatenation_ = nullptr,
                    *unit_ = nullptr;
    bool ecma() const {
        return (options_ & 256) != 0;
    }
    int length() const {
        return static_cast<int>(pattern_.size());
    }
    char16_t at(int index) const {
        if (index < 0 || index >= length())
            parser_index();
        return pattern_[index];
    }
    [[noreturn]] void fail(int code, std::u16string argument = {}) const {
        auto message = std::u16string(parse_messages[code]);
        auto offset = message.find(u"{0}");
        if (offset != std::u16string::npos)
            message.replace(offset, 3, argument);
        throw DotnetRegexParseException(
            code, u"Invalid pattern '" + pattern_ + u"' at offset " + number(pos_) + u". " + message, pos_);
    }
    void add(DotnetRegexNode* parent, DotnetRegexNode* child) {
        parent->add_child(child, execution_stack_);
    }
    DotnetRegexNode* one(char16_t c) {
        return arena_->create_one_with_case_conversion(c, options_, culture_, case_behavior_);
    }
    DotnetRegexNode* node(K kind) {
        return arena_->create(kind, options_);
    }
    DotnetRegexNode* set(std::u16string value) {
        return arena_->create(K::Set, options_ & ~1, text(std::move(value)));
    }
    bool capture_slot(int slot) const {
        return caps_present_ ? caps_.contains(slot) : slot >= 0 && slot < capsize_;
    }
    int capture_name(const std::u16string& name) const {
        if (!capnames_)
            return -1;
        auto found = capnames_->find(name);
        return found == capnames_->end() ? -1 : found->second;
    }
    void note_slot(int slot, int position) {
        if (caps_.emplace(slot, position).second) {
            ++capcount_;
            if (captop_ <= slot)
                captop_ = slot == 2147483647 ? slot : slot + 1;
        }
    }
    void note_name(std::u16string name, int position) {
        if (!capnames_) {
            capnames_.emplace();
            capnamelist_.emplace();
        }
        if (capnames_->emplace(name, position).second)
            capnamelist_->push_back(std::move(name));
    }
    std::u16string scan_capname() {
        int start = pos_;
        while (pos_ < length()) {
            if (!dotnet_regex_boundary_word_char(at(pos_++))) {
                --pos_;
                break;
            }
        }
        return pattern_.substr(start, pos_ - start);
    }
    int scan_decimal() {
        int value = 0;
        while (pos_ < length() && digit(at(pos_))) {
            int next = at(pos_++) - u'0';
            if (value > 214748364 || (value == 214748364 && next > 7))
                fail(9);
            value = value * 10 + next;
        }
        return value;
    }
    char16_t scan_octal() {
        int remaining = std::min(3, length() - pos_), value = 0;
        while (remaining > 0 && static_cast<unsigned>(at(pos_) - u'0') <= 7) {
            value = value * 8 + at(pos_++) - u'0';
            if (ecma() && value >= 32)
                break;
            --remaining;
        }
        return static_cast<char16_t>(value & 255);
    }
    char16_t scan_hex(int count) {
        int value = 0;
        if (pos_ + count <= length()) {
            while (count > 0) {
                auto c = at(pos_++);
                int nibble = c >= u'0' && c <= u'9'   ? c - u'0'
                             : c >= u'a' && c <= u'f' ? c - u'a' + 10
                             : c >= u'A' && c <= u'F' ? c - u'A' + 10
                                                      : 255;
                if (nibble == 255)
                    break;
                value = value * 16 + nibble;
                --count;
            }
        }
        if (count > 0)
            fail(8);
        return static_cast<char16_t>(value);
    }
    char16_t scan_control() {
        if (pos_ == length())
            fail(7);
        auto c = at(pos_++);
        if (c >= u'a' && c <= u'z')
            c -= 32;
        c = static_cast<char16_t>(c - 64);
        if (c < 32)
            return c;
        fail(6);
    }
    char16_t scan_char_escape() {
        auto c = at(pos_++);
        if (c >= u'0' && c <= u'7') {
            --pos_;
            return scan_octal();
        }
        switch (c) {
        case u'x':
            return scan_hex(2);
        case u'u':
            return scan_hex(4);
        case u'a':
            return 7;
        case u'b':
            return 8;
        case u'e':
            return 27;
        case u'f':
            return 12;
        case u'n':
            return 10;
        case u'r':
            return 13;
        case u't':
            return 9;
        case u'v':
            return 11;
        case u'c':
            return scan_control();
        default:
            if (!ecma() && dotnet_regex_boundary_word_char(c))
                fail(5, std::u16string(1, c));
            return c;
        }
    }
    void scan_options() {
        bool remove = false;
        while (pos_ < length()) {
            auto c = at(pos_);
            if (c == u'-')
                remove = true;
            else if (c == u'+')
                remove = false;
            else {
                int option = 0;
                switch (c | 32) {
                case u'i':
                    option = 1;
                    break;
                case u'm':
                    option = 2;
                    break;
                case u'n':
                    option = 4;
                    break;
                case u's':
                    option = 16;
                    break;
                case u'x':
                    option = 32;
                    break;
                default:
                    return;
                }
                if (remove)
                    options_ &= ~option;
                else
                    options_ |= option;
            }
            ++pos_;
        }
    }
    std::u16string parse_property() {
        if (pos_ + 2 >= length())
            fail(3);
        if (at(pos_++) != u'{')
            fail(4);
        int start = pos_;
        while (pos_ < length()) {
            auto c = at(pos_++);
            if (!dotnet_regex_boundary_word_char(c) && c != u'-') {
                --pos_;
                break;
            }
        }
        auto result = pattern_.substr(start, pos_ - start);
        if (pos_ == length() || at(pos_++) != u'}')
            fail(3);
        return result;
    }
    void scan_blank() {
        for (;;) {
            if (options_ & 32)
                while (pos_ < length() && space(at(pos_)))
                    ++pos_;
            if ((options_ & 32) && pos_ < length() && at(pos_) == u'#') {
                auto end = pattern_.find(u'\n', pos_);
                pos_ = end == std::u16string::npos ? length() : static_cast<int>(end);
                continue;
            }
            if (pos_ + 2 < length() && at(pos_) == u'(' && at(pos_ + 1) == u'?' && at(pos_ + 2) == u'#') {
                auto end = pattern_.find(u')', pos_);
                if (end == std::u16string::npos) {
                    pos_ = length();
                    fail(14);
                }
                pos_ = static_cast<int>(end) + 1;
                continue;
            }
            return;
        }
    }
    bool true_quantifier() const {
        int start = pos_, cursor = pos_, remaining = length() - pos_;
        auto c = at(cursor);
        if (c != u'{')
            return c <= u'{' && category[c] >= 4;
        while (--remaining > 0 && digit(c = at(++cursor))) {
        }
        if (remaining == 0 || cursor - start == 1)
            return false;
        if (c == u'}')
            return true;
        if (c != u',')
            return false;
        while (--remaining > 0 && digit(c = at(++cursor))) {
        }
        return remaining > 0 && c == u'}';
    }
    std::shared_ptr<DotnetRegexClassBuilder> scan_char_class(bool insensitive, bool scan_only);
    DotnetRegexNode* scan_basic_backslash(bool scan_only);
    DotnetRegexNode* scan_backslash(bool scan_only);
    DotnetRegexNode* scan_group_open();
    DotnetRegexNode* scan_dollar();
    DotnetRegexNode* scan_regex();
    void count_captures();
    void assign_name_slots();
    void add_to_concatenate(int start, int count, bool replacement = false) {
        if (count == 0)
            return;
        if (count == 1) {
            add(concatenation_, replacement ? arena_->create_one_with_case_conversion(
                                                  at(start), options_ & ~1, culture_, case_behavior_)
                                            : one(at(start)));
            return;
        }
        auto value = pattern_.substr(start, count);
        if (!(options_ & 1) || replacement ||
            !dotnet_regex_participates_in_case_conversion(std::span<const char16_t>(value))) {
            add(concatenation_, arena_->create(K::Multi, options_ & ~1, text(std::move(value))));
            return;
        }
        for (auto c : value)
            add(concatenation_, one(c));
    }
    void push_group() {
        group_->parent_ = stack_;
        alternation_->parent_ = group_;
        concatenation_->parent_ = alternation_;
        stack_ = concatenation_;
    }
    void pop_group() {
        concatenation_ = stack_;
        alternation_ = concatenation_->parent_;
        group_ = alternation_->parent_;
        stack_ = group_->parent_;
        if (group_->kind() == K::ExpressionConditional && group_->child_count() == 0) {
            if (!unit_)
                fail(2);
            add(group_, unit_);
            unit_ = nullptr;
        }
    }
    void start_group(DotnetRegexNode* open) {
        group_ = open;
        alternation_ = node(K::Alternate);
        concatenation_ = node(K::Concatenate);
    }
    bool conditional() const {
        return group_->kind() == K::BackreferenceConditional || group_->kind() == K::ExpressionConditional;
    }
    void add_alternate() {
        add(conditional() ? group_ : alternation_, concatenation_->reverse_concatenation_if_right_to_left());
        concatenation_ = node(K::Concatenate);
    }
    void add_group() {
        if (conditional()) {
            add(group_, concatenation_->reverse_concatenation_if_right_to_left());
            if ((group_->kind() == K::BackreferenceConditional && group_->child_count() > 2) ||
                group_->child_count() > 3)
                fail(1);
        } else {
            add(alternation_, concatenation_->reverse_concatenation_if_right_to_left());
            add(group_, alternation_);
        }
        unit_ = group_;
    }

  public:
    DotnetRegexParser(Text pattern, int options, DotnetRegexCultureName culture,
                      DotnetRegexExecutionStack stack)
        : options_(options), culture_(std::move(culture)), execution_stack_(std::move(stack)) {
        if (!pattern)
            parser_null();
        pattern_ = *pattern;
    }
    DotnetRegexParsedPattern parse() {
        int original_options = options_;
        count_captures();
        pos_ = 0;
        autocap_ = 1;
        ignore_next_paren_ = false;
        option_stack_.clear();
        options_ = original_options;
        stack_ = nullptr;
        auto root = scan_regex();
        DotnetRegexParsedPattern result;
        result.arena = arena_;
        result.root = root;
        result.options = original_options;
        result.capture_names = capnamelist_;
        result.capture_name_to_number = capnames_;
        if (!capnumlist_ || captop_ == static_cast<int>(capnumlist_->size()))
            result.capture_count = captop_;
        else {
            result.capture_count = static_cast<int>(capnumlist_->size());
            result.capture_number_sparse_mapping.emplace();
            for (int i = 0; i < result.capture_count; ++i)
                (*result.capture_number_sparse_mapping)[(*capnumlist_)[i]] = i;
        }
        if (ignore_case_backrefs_)
            result.backreference_culture = culture_;
        return result;
    }
    Text unescape(Text input) {
        auto first = input->find(u'\\');
        if (first == std::u16string::npos)
            return input;
        std::u16string result = input->substr(0, first);
        int cursor = static_cast<int>(first);
        do {
            ++cursor;
            pos_ = cursor;
            if (cursor < length())
                result.push_back(scan_char_escape());
            cursor = pos_;
            int start = cursor;
            while (cursor < length() && at(cursor) != u'\\')
                ++cursor;
            result += pattern_.substr(start, cursor - start);
        } while (cursor < length());
        return text(std::move(result));
    }
    DotnetRegexParsedReplacement replacement(Text original, const std::optional<std::map<int, int>>& captures,
                                             int size,
                                             const std::optional<std::map<std::u16string, int>>& names) {
        caps_present_ = captures.has_value();
        if (captures)
            caps_ = *captures;
        capsize_ = size;
        capnames_ = names;
        concatenation_ = node(K::Concatenate);
        while (pos_ < length()) {
            int start = pos_;
            auto next = pattern_.find(u'$', pos_);
            pos_ = next == std::u16string::npos ? length() : static_cast<int>(next);
            add_to_concatenate(start, pos_ - start, true);
            if (pos_ < length()) {
                ++pos_;
                add(concatenation_, scan_dollar());
                unit_ = nullptr;
            }
        }
        DotnetRegexParsedReplacement result;
        result.pattern = std::move(original);
        std::u16string pending;
        auto flush = [&]() {
            if (!pending.empty()) {
                result.rules.push_back(static_cast<int>(result.strings.size()));
                result.strings.push_back(text(std::move(pending)));
                pending.clear();
            }
        };
        for (int i = 0; i < concatenation_->child_count(); ++i) {
            auto child = concatenation_->child(i);
            switch (child->kind()) {
            case K::Multi:
                if (child->text())
                    pending += *child->text();
                break;
            case K::One:
                pending.push_back(child->character());
                break;
            case K::Backreference: {
                flush();
                int slot = child->minimum();
                if (caps_present_ && slot >= 0) {
                    auto found = caps_.find(slot);
                    if (found == caps_.end())
                        parser_null();
                    slot = found->second;
                }
                result.rules.push_back(
                    std::bit_cast<std::int32_t>(0u - 5u - static_cast<std::uint32_t>(slot)));
                result.has_backreferences = true;
                break;
            }
            default:
                break;
            }
        }
        flush();
        return result;
    }
};
DotnetRegexNode* DotnetRegexParser::scan_dollar() {
    if (pos_ == length())
        return one(u'$');
    auto c = at(pos_);
    int start = pos_, last = pos_;
    bool bracket = c == u'{' && pos_ + 1 < length();
    if (bracket)
        c = at(++pos_);
    if (digit(c)) {
        if (!bracket && ecma()) {
            int found = -1, value = c - u'0';
            ++pos_;
            if (capture_slot(value)) {
                found = value;
                last = pos_;
            }
            while (pos_ < length() && digit(c = at(pos_))) {
                int next = c - u'0';
                if (value > 214748364 || (value == 214748364 && next > 7))
                    fail(9);
                value = value * 10 + next;
                ++pos_;
                if (capture_slot(value)) {
                    found = value;
                    last = pos_;
                }
            }
            pos_ = last;
            if (found >= 0)
                return arena_->create(K::Backreference, options_, found, 0);
        } else {
            int slot = scan_decimal();
            if ((!bracket || (pos_ < length() && at(pos_++) == u'}')) && capture_slot(slot))
                return arena_->create(K::Backreference, options_, slot, 0);
        }
    } else if (bracket && dotnet_regex_boundary_word_char(c)) {
        auto name = scan_capname();
        if (pos_ < length() && at(pos_++) == u'}' && capnames_) {
            auto found = capnames_->find(name);
            if (found != capnames_->end())
                return arena_->create(K::Backreference, options_, found->second, 0);
        }
    } else if (!bracket) {
        int slot = 1;
        switch (c) {
        case u'$':
            ++pos_;
            return one(u'$');
        case u'&':
            slot = 0;
            break;
        case u'`':
            slot = -1;
            break;
        case u'\'':
            slot = -2;
            break;
        case u'+':
            slot = -3;
            break;
        case u'_':
            slot = -4;
            break;
        default:
            break;
        }
        if (slot != 1) {
            ++pos_;
            return arena_->create(K::Backreference, options_, slot, 0);
        }
    }
    pos_ = start;
    return one(u'$');
}
void DotnetRegexParser::assign_name_slots() {
    if (capnames_)
        for (auto& name : *capnamelist_) {
            while (capture_slot(autocap_))
                ++autocap_;
            int position = capnames_->at(name);
            (*capnames_)[name] = autocap_;
            note_slot(autocap_, position);
            ++autocap_;
        }
    if (capcount_ < captop_) {
        capnumlist_.emplace();
        for (auto entry : caps_)
            capnumlist_->push_back(entry.first);
    }
    if (!capnames_ && !capnumlist_)
        return;
    int name_index = 0, next_named = -1;
    std::optional<std::vector<std::u16string>> named;
    if (capnames_) {
        named = capnamelist_;
        next_named = capnames_->at((*named)[0]);
    } else
        capnames_.emplace();
    capnamelist_.emplace();
    for (int i = 0; i < capcount_; ++i) {
        int slot = capnumlist_ ? (*capnumlist_)[i] : i;
        if (next_named == slot) {
            capnamelist_->push_back((*named)[name_index++]);
            next_named =
                name_index == static_cast<int>(named->size()) ? -1 : capnames_->at((*named)[name_index]);
        } else {
            auto value = number(slot);
            capnamelist_->push_back(value);
            (*capnames_)[value] = slot;
        }
    }
}
void DotnetRegexParser::count_captures() {
    note_slot(0, 0);
    autocap_ = 1;
    while (pos_ < length()) {
        int position = pos_;
        switch (at(pos_++)) {
        case u'\\':
            if (pos_ < length())
                scan_backslash(true);
            break;
        case u'#':
            if (options_ & 32) {
                --pos_;
                scan_blank();
            }
            break;
        case u'[':
            scan_char_class(false, true);
            break;
        case u')':
            if (!option_stack_.empty()) {
                options_ = option_stack_.back();
                option_stack_.pop_back();
            }
            break;
        case u'(':
            if (pos_ + 1 < length() && at(pos_) == u'?' && at(pos_ + 1) == u'#') {
                --pos_;
                scan_blank();
            } else {
                option_stack_.push_back(options_);
                if (pos_ < length() && at(pos_) == u'?') {
                    ++pos_;
                    if (pos_ + 1 < length() && (at(pos_) == u'<' || at(pos_) == u'\'')) {
                        ++pos_;
                        auto c = at(pos_);
                        if (c != u'0' && dotnet_regex_boundary_word_char(c)) {
                            if (c >= u'1' && c <= u'9')
                                note_slot(scan_decimal(), position);
                            else
                                note_name(scan_capname(), position);
                        }
                    } else {
                        scan_options();
                        if (pos_ < length()) {
                            if (at(pos_) == u')') {
                                ++pos_;
                                option_stack_.pop_back();
                            } else if (at(pos_) == u'(') {
                                ignore_next_paren_ = true;
                                break;
                            }
                        }
                    }
                } else if (!(options_ & 4) && !ignore_next_paren_)
                    note_slot(autocap_++, position);
            }
            ignore_next_paren_ = false;
            break;
        default:
            break;
        }
    }
    assign_name_slots();
}
std::shared_ptr<DotnetRegexClassBuilder> DotnetRegexParser::scan_char_class(bool insensitive,
                                                                            bool scan_only) {
    char16_t range_start = 0;
    bool range = false, first = true, closed = false;
    auto builder = scan_only ? nullptr : std::make_shared<DotnetRegexClassBuilder>();
    if (pos_ < length() && at(pos_) == u'^') {
        ++pos_;
        if (!scan_only)
            builder->negate(true);
        if (ecma() && at(pos_) == u']')
            first = false;
    }
    for (; pos_ < length(); first = false) {
        bool escaped = false;
        auto c = at(pos_++);
        if (c == u']') {
            if (!first) {
                closed = true;
                break;
            }
        } else if (c == u'\\' && pos_ < length()) {
            c = at(pos_++);
            if (c == u'd' || c == u'D' || c == u's' || c == u'S' || c == u'w' || c == u'W' || c == u'p' ||
                c == u'P') {
                if (!scan_only) {
                    if (range)
                        fail(25, std::u16string(1, c));
                    switch (c) {
                    case u'd':
                    case u'D':
                        builder->add_digit(ecma(), c == u'D', pattern_, pos_);
                        break;
                    case u's':
                    case u'S':
                        builder->add_space(ecma(), c == u'S');
                        break;
                    case u'w':
                    case u'W':
                        builder->add_word(ecma(), c == u'W');
                        break;
                    default: {
                        auto property = parse_property();
                        builder->add_category_from_name(property, c != u'p', insensitive, pattern_, pos_);
                        break;
                    }
                    }
                } else if (c == u'p' || c == u'P')
                    parse_property();
                continue;
            }
            if (c == u'-') {
                if (scan_only)
                    continue;
                if (range) {
                    if (range_start > c)
                        fail(24);
                    builder->add_range(range_start, c);
                    range = false;
                    range_start = 0;
                } else
                    builder->add_range(c, c);
                continue;
            }
            --pos_;
            c = scan_char_escape();
            escaped = true;
        }
        if (range) {
            range = false;
            if (scan_only)
                continue;
            if (c == u'[' && !escaped && !first) {
                builder->add_char(range_start);
                builder->add_subtraction(scan_char_class(insensitive, scan_only));
                if (pos_ < length() && at(pos_) != u']')
                    fail(23);
            } else {
                if (range_start > c)
                    fail(24);
                builder->add_range(range_start, c);
            }
        } else if (pos_ + 1 < length() && at(pos_) == u'-' && at(pos_ + 1) != u']') {
            range_start = c;
            range = true;
            ++pos_;
        } else if (pos_ < length() && c == u'-' && !escaped && at(pos_) == u'[' && !first) {
            ++pos_;
            auto subtraction = scan_char_class(insensitive, scan_only);
            if (!scan_only) {
                builder->add_subtraction(subtraction);
                if (pos_ < length() && at(pos_) != u']')
                    fail(23);
            }
        } else if (!scan_only)
            builder->add_range(c, c);
    }
    if (!closed)
        fail(22);
    if (!scan_only && insensitive)
        builder->add_case_equivalences(culture_);
    return builder;
}
DotnetRegexNode* DotnetRegexParser::scan_backslash(bool scan_only) {
    auto c = at(pos_);
    switch (c) {
    case u'A':
    case u'B':
    case u'G':
    case u'Z':
    case u'b':
    case u'z': {
        ++pos_;
        if (scan_only)
            return nullptr;
        K kind = c == u'A'   ? K::Beginning
                 : c == u'G' ? K::Start
                 : c == u'Z' ? K::EndZ
                 : c == u'z' ? K::End
                 : c == u'b' ? (ecma() ? K::ECMABoundary : K::Boundary)
                             : (ecma() ? K::NonECMABoundary : K::NonBoundary);
        return node(kind);
    }
    case u'w':
    case u'W':
    case u'd':
    case u'D':
    case u's':
    case u'S': {
        ++pos_;
        if (scan_only)
            return nullptr;
        std::u16string_view literal;
        switch (c) {
        case u'w':
            literal = ecma() ? u"\0\n\0"
                               u"0:A[_`a{\u0130\u0131"sv
                             : u"\0\0\n\0\2\4\5\3\1\6\t\x0013\0"sv;
            break;
        case u'W':
            literal = ecma() ? u"\1\n\0"
                               u"0:A[_`a{\u0130\u0131"sv
                             : u"\0\0\n\0\xfffe\xfffc\xfffb\xfffd\xffff\xfffa\xfff7\xffed\0"sv;
            break;
        case u's':
            literal = ecma() ? u"\0\4\0\t\x000e !"sv
                             : u"\0\0\1"
                               u"d"sv;
            break;
        case u'S':
            literal = ecma() ? u"\1\4\0\t\x000e !"sv : u"\0\0\1\xff9c"sv;
            break;
        case u'd':
            literal = ecma() ? u"\0\2\0"
                               u"0:"sv
                             : u"\0\0\1\t"sv;
            break;
        default:
            literal = ecma() ? u"\1\2\0"
                               u"0:"sv
                             : u"\0\0\1\xfff7"sv;
            break;
        }
        return arena_->create(K::Set, options_ & ~1, dotnet_regex_class_literal(literal));
    }
    case u'p':
    case u'P': {
        ++pos_;
        if (scan_only)
            return nullptr;
        DotnetRegexClassBuilder builder;
        auto property = parse_property();
        builder.add_category_from_name(property, c != u'p', (options_ & 1) != 0, pattern_, pos_);
        if (options_ & 1)
            builder.add_case_equivalences(culture_);
        return set(builder.to_string_class());
    }
    default: {
        auto result = scan_basic_backslash(scan_only);
        if (result && result->kind() == K::Backreference && (result->options() & 1))
            ignore_case_backrefs_ = true;
        return result;
    }
    }
}
DotnetRegexNode* DotnetRegexParser::scan_basic_backslash(bool scan_only) {
    int start = pos_;
    char16_t end = 0;
    bool named = false;
    auto c = at(pos_);
    if (c == u'k') {
        if (pos_ + 1 < length()) {
            ++pos_;
            c = at(pos_++);
            if (c == u'\'' || c == u'<') {
                named = true;
                end = c == u'\'' ? u'\'' : u'>';
            }
        }
        if (!named || pos_ == length())
            fail(12);
        c = at(pos_);
    } else if ((c == u'\'' || c == u'<') && pos_ + 1 < length()) {
        named = true;
        end = c == u'\'' ? u'\'' : u'>';
        ++pos_;
        c = at(pos_);
    }
    if (named && digit(c)) {
        int slot = scan_decimal();
        if (pos_ < length() && at(pos_++) == end) {
            if (scan_only)
                return nullptr;
            if (!capture_slot(slot))
                fail(11, number(slot));
            return arena_->create(K::Backreference, options_, slot, 0);
        }
    } else if (!named && c >= u'1' && c <= u'9') {
        if (ecma()) {
            int found = -1, value = c - u'0', backslash_position = pos_ - 1;
            while (value <= captop_) {
                if (capture_slot(value) && caps_.at(value) < backslash_position)
                    found = value;
                ++pos_;
                if (pos_ == length() || !digit(c = at(pos_)))
                    break;
                value = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value) * 10u +
                                                    static_cast<std::uint32_t>(c - u'0'));
            }
            if (found >= 0)
                return scan_only ? nullptr : arena_->create(K::Backreference, options_, found, 0);
        } else {
            int slot = scan_decimal();
            if (scan_only)
                return nullptr;
            if (capture_slot(slot))
                return arena_->create(K::Backreference, options_, slot, 0);
            if (slot <= 9)
                fail(11, number(slot));
        }
    } else if (named && dotnet_regex_boundary_word_char(c)) {
        auto name = scan_capname();
        if (pos_ < length() && at(pos_++) == end) {
            if (scan_only)
                return nullptr;
            int slot = capture_name(name);
            if (slot < 0)
                fail(10, name);
            return arena_->create(K::Backreference, options_, slot, 0);
        }
    }
    pos_ = start;
    c = scan_char_escape();
    return scan_only ? nullptr : one(c);
}
DotnetRegexNode* DotnetRegexParser::scan_group_open() {
    if (pos_ == length() || at(pos_) != u'?' || (pos_ + 1 < length() && at(pos_ + 1) == u')')) {
        if ((options_ & 4) || ignore_next_paren_) {
            ignore_next_paren_ = false;
            return node(K::Group);
        }
        return arena_->create(K::Capture, options_, autocap_++, -1);
    }
    ++pos_;
    if (pos_ != length()) {
        auto construct = at(pos_++);
        switch (construct) {
        case u':':
            return node(K::Group);
        case u'=':
            options_ &= ~64;
            return node(K::PositiveLookaround);
        case u'!':
            options_ &= ~64;
            return node(K::NegativeLookaround);
        case u'>':
            return node(K::Atomic);
        case u'\'':
        case u'<': {
            auto end = construct == u'\'' ? u'\'' : u'>';
            if (pos_ == length())
                break;
            auto c = at(pos_++);
            if (c == u'!' || c == u'=') {
                if (end == u'\'')
                    break;
                options_ |= 64;
                return node(c == u'!' ? K::NegativeLookaround : K::PositiveLookaround);
            }
            --pos_;
            int slot = -1, balance = -1;
            bool minus = false;
            if (digit(c)) {
                slot = scan_decimal();
                if (!capture_slot(slot))
                    slot = -1;
                if (pos_ < length() && at(pos_) != end && at(pos_) != u'-')
                    fail(20);
                if (slot == 0)
                    fail(21);
            } else if (dotnet_regex_boundary_word_char(c)) {
                slot = capture_name(scan_capname());
                if (pos_ < length() && at(pos_) != end && at(pos_) != u'-')
                    fail(20);
            } else {
                if (c != u'-')
                    fail(20);
                minus = true;
            }
            if ((slot != -1 || minus) && pos_ + 1 < length() && at(pos_) == u'-') {
                ++pos_;
                c = at(pos_);
                if (digit(c)) {
                    balance = scan_decimal();
                    if (!capture_slot(balance))
                        fail(11, number(balance));
                    if (pos_ < length() && at(pos_) != end)
                        fail(20);
                } else {
                    if (!dotnet_regex_boundary_word_char(c))
                        fail(20);
                    auto name = scan_capname();
                    balance = capture_name(name);
                    if (balance < 0)
                        fail(10, name);
                    if (pos_ < length() && at(pos_) != end)
                        fail(20);
                }
            }
            if ((slot != -1 || balance != -1) && pos_ < length() && at(pos_++) == end)
                return arena_->create(K::Capture, options_, slot, balance);
            break;
        }
        case u'(': {
            int start = pos_;
            if (pos_ < length()) {
                auto c = at(pos_);
                if (digit(c)) {
                    int slot = scan_decimal();
                    if (pos_ < length() && at(pos_++) == u')') {
                        if (capture_slot(slot))
                            return arena_->create(K::BackreferenceConditional, options_, slot, 0);
                        fail(19, number(slot));
                    }
                    fail(18, number(slot));
                }
                if (dotnet_regex_boundary_word_char(c)) {
                    int slot = capture_name(scan_capname());
                    if (slot >= 0 && pos_ < length() && at(pos_++) == u')')
                        return arena_->create(K::BackreferenceConditional, options_, slot, 0);
                }
            }
            pos_ = start - 1;
            ignore_next_paren_ = true;
            if (pos_ + 2 < length() && at(pos_ + 1) == u'?') {
                if (at(pos_ + 2) == u'#')
                    fail(17);
                if (at(pos_ + 2) == u'\'' || (pos_ + 3 < length() && at(pos_ + 2) == u'<' &&
                                              at(pos_ + 3) != u'!' && at(pos_ + 3) != u'='))
                    fail(16);
            }
            return node(K::ExpressionConditional);
        }
        default:
            --pos_;
            if (group_->kind() != K::ExpressionConditional)
                scan_options();
            if (pos_ == length())
                break;
            construct = at(pos_++);
            if (construct == u')')
                return nullptr;
            if (construct == u':')
                return node(K::Group);
            break;
        }
    }
    fail(15);
}
DotnetRegexNode* DotnetRegexParser::scan_regex() {
    bool previous_quantifier = false;
    start_group(arena_->create(K::Capture, options_ & ~1, 0, -1));
    while (pos_ < length()) {
        bool nested = previous_quantifier;
        previous_quantifier = false;
        scan_blank();
        int start = pos_;
        while (pos_ < length() && (!((options_ & 32) ? special_space(at(pos_)) : special(at(pos_))) ||
                                   (at(pos_) == u'{' && !true_quantifier())))
            ++pos_;
        int literal_end = pos_;
        scan_blank();
        char16_t c;
        if (pos_ == length())
            c = u'!';
        else if (special(c = at(pos_))) {
            previous_quantifier = quantifier(c);
            ++pos_;
        } else
            c = u' ';
        if (start < literal_end) {
            int count = literal_end - start - (previous_quantifier ? 1 : 0);
            nested = false;
            if (count > 0)
                add_to_concatenate(start, count);
            if (previous_quantifier)
                unit_ = one(at(literal_end - 1));
        }
        bool finish = false;
        switch (c) {
        case u'[':
            unit_ = set(scan_char_class((options_ & 1) != 0, false)->to_string_class());
            break;
        case u'(': {
            option_stack_.push_back(options_);
            auto open = scan_group_open();
            if (open) {
                push_group();
                start_group(open);
            } else
                option_stack_.pop_back();
            continue;
        }
        case u'|':
            add_alternate();
            continue;
        case u')':
            if (!stack_)
                fail(30);
            add_group();
            pop_group();
            options_ = option_stack_.back();
            option_stack_.pop_back();
            if (!unit_)
                continue;
            break;
        case u'\\':
            if (pos_ == length())
                fail(13);
            unit_ = scan_backslash(false);
            break;
        case u'^':
            unit_ = node((options_ & 2) ? K::Bol : K::Beginning);
            break;
        case u'$':
            unit_ = node((options_ & 2) ? K::Eol : K::EndZ);
            break;
        case u'.':
            unit_ = (options_ & 16) ? arena_->create(K::Set, options_ & ~1,
                                                     dotnet_regex_class_literal(std::u16string{0, 1, 0, 0}))
                                    : arena_->create(K::Notone, options_ & ~1, u'\n');
            break;
        case u'*':
        case u'+':
        case u'?':
        case u'{':
            if (!unit_)
                fail(nested ? 28 : 29, std::u16string(1, c));
            --pos_;
            break;
        case u' ':
            continue;
        case u'!':
            finish = true;
            break;
        default:
            break;
        }
        if (finish)
            break;
        scan_blank();
        if (pos_ == length() || !(previous_quantifier = true_quantifier())) {
            add(concatenation_, unit_);
            unit_ = nullptr;
            continue;
        }
        c = at(pos_++);
        while (unit_) {
            int minimum = 0, maximum = 0;
            switch (c) {
            case u'*':
                maximum = 2147483647;
                break;
            case u'+':
                minimum = 1;
                maximum = 2147483647;
                break;
            case u'?':
                maximum = 1;
                break;
            case u'{': {
                start = pos_;
                minimum = maximum = scan_decimal();
                if (start < pos_ && pos_ < length() && at(pos_) == u',') {
                    ++pos_;
                    maximum = (pos_ == length() || at(pos_) == u'}') ? 2147483647 : scan_decimal();
                }
                if (start == pos_ || pos_ == length() || at(pos_++) != u'}') {
                    add(concatenation_, unit_);
                    unit_ = nullptr;
                    pos_ = start - 1;
                }
                break;
            }
            default:
                break;
            }
            if (!unit_)
                break;
            scan_blank();
            bool lazy = false;
            if (pos_ < length() && at(pos_) == u'?') {
                ++pos_;
                lazy = true;
            }
            if (minimum > maximum)
                fail(27);
            add(concatenation_, unit_->make_quantifier(lazy, minimum, maximum, execution_stack_));
            unit_ = nullptr;
        }
    }
    if (stack_)
        fail(26);
    add_group();
    return unit_->final_optimize(execution_stack_);
}
DotnetRegexParsedPattern dotnet_regex_parse(Text pattern, int options, const DotnetRegexCultureName& culture,
                                            const DotnetRegexExecutionStack& stack) {
    return DotnetRegexParser(std::move(pattern), options, culture, stack).parse();
}
Text dotnet_regex_escape(Text input) {
    if (!input)
        return input;
    constexpr auto metachars = u"\t\n\f\r #$()*+.?[\\^{|"sv;
    auto first = input->find_first_of(metachars);
    if (first == std::u16string::npos)
        return input;
    std::u16string result = input->substr(0, first);
    for (std::size_t i = first; i < input->size(); ++i) {
        auto c = (*input)[i];
        if (metachars.find(c) != std::u16string::npos) {
            result.push_back(u'\\');
            switch (c) {
            case u'\n':
                c = u'n';
                break;
            case u'\r':
                c = u'r';
                break;
            case u'\t':
                c = u't';
                break;
            case u'\f':
                c = u'f';
                break;
            default:
                break;
            }
        }
        result.push_back(c);
    }
    return text(std::move(result));
}
Text dotnet_regex_unescape(Text input) {
    if (!input)
        parser_null();
    return DotnetRegexParser(input, 0, [] { return std::u16string{}; }, {}).unescape(input);
}
DotnetRegexParsedReplacement
dotnet_regex_parse_replacement(Text pattern, int options, const std::optional<std::map<int, int>>& caps,
                               int capsize, const std::optional<std::map<std::u16string, int>>& names,
                               const DotnetRegexCultureName& culture,
                               const DotnetRegexExecutionStack& stack) {
    DotnetRegexCultureName target =
        (options & 512) ? DotnetRegexCultureName([] { return std::u16string{}; }) : culture;
    return DotnetRegexParser(pattern, options, std::move(target), stack)
        .replacement(std::move(pattern), caps, capsize, names);
}
int dotnet_regex_map_capture_number(int number, const std::optional<std::map<int, int>>& caps) {
    if (number == -1 || !caps)
        return number;
    auto found = caps->find(number);
    if (found == caps->end())
        parser_null();
    return found->second;
}
std::u16string dotnet_regex_group_name_from_number(const std::optional<std::map<int, int>>& caps,
                                                   const std::optional<std::vector<std::u16string>>& names,
                                                   int size, int slot) {
    if (!names) {
        if (static_cast<std::uint32_t>(slot) < static_cast<std::uint32_t>(size))
            return utf16(std::to_string(static_cast<std::uint32_t>(slot)));
    } else {
        bool valid = true;
        if (caps) {
            auto found = caps->find(slot);
            if (found == caps->end())
                valid = false;
            else
                slot = found->second;
        }
        if (valid && static_cast<std::uint32_t>(slot) < names->size())
            return (*names)[slot];
    }
    return {};
}
DotnetRegexCultureName dotnet_regex_target_culture(int options, const DotnetRegexCultureName& current) {
    return (options & 512) ? DotnetRegexCultureName([] { return std::u16string{}; }) : current;
}
}
