// .NET8 RegexCharClass builder port. MIT third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/dotnet_regex_case.hpp"
#include "opensim/framework/dotnet_regex_char_class.hpp"
#include <optional>
#include <utility>
namespace opensim::framework {
class DotnetRegexParseException : public DotnetRegexClassError {
public:
    DotnetRegexParseException(std::u16string message, int offset);
    DotnetRegexParseException(int error, std::u16string message, int offset);
    std::u16string message;
    int error = 31, offset;
};
class DotnetRegexClassBuilder {
    friend struct DotnetRegexClassBuilderProbe;
    using Range = std::pair<char16_t, char16_t>;
    std::optional<std::vector<Range>> ranges_;
    std::optional<std::u16string> categories_;
    std::shared_ptr<DotnetRegexClassBuilder> subtractor_;
    bool negate_ = false;
    DotnetRegexCaseBehavior case_behavior_ = DotnetRegexCaseBehavior::NotSet;
    std::vector<Range>& ensure_ranges();
    std::u16string& ensure_categories();
    void add_ranges(std::u16string_view);
    void canonicalize();
    void append_to(std::u16string&);

public:
    DotnetRegexClassBuilder() = default;
    DotnetRegexClassBuilder(const DotnetRegexClassBuilder&) = delete;
    DotnetRegexClassBuilder& operator=(const DotnetRegexClassBuilder&) = delete;
    bool can_merge() const {
        return !negate_ && !subtractor_;
    }
    void negate(bool value) {
        negate_ = value;
    }
    void add_char(char16_t);
    void add_range(char16_t, char16_t);
    void add_class(const DotnetRegexClassBuilder&);
    bool try_add_class(const DotnetRegexClassBuilder&);
    void add_subtraction(std::shared_ptr<DotnetRegexClassBuilder> value) {
        subtractor_ = std::move(value);
    }
    void add_case_equivalences(const DotnetRegexCultureName&);
    void add_word(bool ecma, bool negate);
    void add_space(bool ecma, bool negate);
    void add_digit(bool ecma, bool negate, std::u16string_view pattern = {}, int current_position = 0);
    void add_category_from_name(std::u16string_view, bool invert, bool case_insensitive,
                                std::u16string_view pattern, int current_position);
    std::u16string to_string_class();
    static std::shared_ptr<DotnetRegexClassBuilder> parse(std::u16string_view);
};
std::u16string dotnet_regex_convert_old_class(std::u16string_view ranges, std::u16string_view categories);
}
