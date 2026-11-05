// .NET8 RegexPrefixAnalyzer; MIT runtime license.
#pragma once
#include "opensim/framework/dotnet_regex_parser.hpp"
namespace opensim::framework {
struct DotnetRegexFixedDistanceSet {
    DotnetRegexFixedDistanceSet() = default;
    DotnetRegexFixedDistanceSet(std::shared_ptr<const std::vector<char16_t>> value, DotnetRegexNodeText text,
                                std::int32_t offset)
        : chars(std::move(value)), set(std::move(text)), distance(offset) {}
    std::shared_ptr<const std::vector<char16_t>> chars;
    DotnetRegexNodeText set;
    std::int32_t distance = 0;
    bool negated = false;
    std::optional<DotnetRegexRange> range;
};
struct DotnetRegexLiteralAfterLoop {
    DotnetRegexNode* loop = nullptr;
    char16_t character = 0;
    DotnetRegexNodeText string;
    std::shared_ptr<const std::vector<char16_t>> chars;
};
DotnetRegexNodeText dotnet_regex_find_prefix(DotnetRegexNode*, const DotnetRegexExecutionStack&);
DotnetRegexNodeText dotnet_regex_find_ordinal_case_insensitive_prefix(DotnetRegexNode*);
DotnetRegexNodeText dotnet_regex_find_first_char_class(DotnetRegexNode*, const DotnetRegexExecutionStack&);
std::optional<std::vector<DotnetRegexFixedDistanceSet>>
dotnet_regex_find_fixed_distance_sets(DotnetRegexNode*, bool thorough, const DotnetRegexExecutionStack&);
void dotnet_regex_sort_fixed_distance_sets(std::vector<DotnetRegexFixedDistanceSet>&);
std::optional<DotnetRegexLiteralAfterLoop> dotnet_regex_find_literal_after_loop(DotnetRegexNode*);
DotnetRegexNodeKind dotnet_regex_find_leading_anchor(DotnetRegexNode*, const DotnetRegexExecutionStack&);
DotnetRegexNodeKind dotnet_regex_find_trailing_anchor(DotnetRegexNode*, const DotnetRegexExecutionStack&);
}
