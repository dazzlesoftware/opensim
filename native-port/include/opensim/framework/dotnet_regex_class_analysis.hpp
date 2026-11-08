// .NET8 RegexCharClass analysis helpers. MIT third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/dotnet_regex_char_class.hpp"
#include <optional>
#include <span>
namespace opensim::framework {
using DotnetRegexClassText=std::optional<std::u16string_view>;
struct DotnetRegexRange {
    char16_t first = 0;
    char16_t last = 0;
};
struct DotnetRegexClassAnalysis {
 bool only_ranges=false,contains_only_ascii=false,contains_no_ascii=false,all_ascii_contained=false,all_non_ascii_contained=false;
 int lower_bound=0,upper_bound=0;
};
char16_t dotnet_regex_singleton_char(DotnetRegexClassText);
bool dotnet_regex_class_negated(DotnetRegexClassText,int offset=0);
bool dotnet_regex_class_subtraction(DotnetRegexClassText);
bool dotnet_regex_class_mergeable(DotnetRegexClassText);
bool dotnet_regex_class_empty(DotnetRegexClassText);
bool dotnet_regex_class_singleton(DotnetRegexClassText);
bool dotnet_regex_class_singleton_inverse(DotnetRegexClassText);
bool dotnet_regex_only_categories(DotnetRegexClassText,std::span<std::int32_t>,int& count,bool& negated);
bool dotnet_regex_single_range(DotnetRegexClassText,char16_t& low,char16_t& high);
bool dotnet_regex_double_range(DotnetRegexClassText,DotnetRegexRange&,DotnetRegexRange&);
int dotnet_regex_set_chars(DotnetRegexClassText,std::span<char16_t>);
std::optional<std::u16string> dotnet_regex_ascii_set_chars(DotnetRegexClassText);
bool dotnet_regex_may_overlap(DotnetRegexClassText,DotnetRegexClassText);
bool dotnet_regex_participates_in_case_conversion(std::int32_t);
bool dotnet_regex_participates_in_case_conversion(std::span<const char16_t>);
bool dotnet_regex_is_ascii(std::span<const char16_t>);
bool dotnet_regex_differ_by_one_bit(char16_t,char16_t,int& mask);
DotnetRegexClassAnalysis dotnet_regex_analyze_class(DotnetRegexClassText);
std::optional<std::vector<DotnetRegexRange>> dotnet_regex_compute_ranges(std::span<const char16_t>);
// Immutable string handles preserve the original singleton/ASCII-pair caches.
std::shared_ptr<const std::u16string> dotnet_regex_chars_to_string_class(std::span<const char16_t>);
std::shared_ptr<const std::u16string> dotnet_regex_one_to_string_class(char16_t);
// Interned runtime class literals only; builder results remain distinct strings.
std::shared_ptr<const std::u16string> dotnet_regex_class_literal(std::u16string_view);
}
