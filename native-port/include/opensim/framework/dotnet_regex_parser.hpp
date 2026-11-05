// .NET8 RegexParser native port; MIT third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/dotnet_regex_node.hpp"
#include "opensim/framework/dotnet_regex_class_builder.hpp"
#include <map>
namespace opensim::framework {
struct DotnetRegexParsedPattern {
 std::shared_ptr<DotnetRegexNodeArena> arena;
 DotnetRegexNode* root=nullptr;
 std::int32_t options=0,capture_count=0;
 std::optional<std::vector<std::u16string>> capture_names;
 std::optional<std::map<std::u16string,std::int32_t>> capture_name_to_number;
 std::optional<std::map<std::int32_t,std::int32_t>> capture_number_sparse_mapping;
 std::optional<DotnetRegexCultureName> backreference_culture;
};
// Parser output and capture metadata. RegexFindOptimizations and execution are
// separate consumers, still under port; no alternate regex engine is substituted.
DotnetRegexParsedPattern dotnet_regex_parse(DotnetRegexNodeText pattern,std::int32_t options,const DotnetRegexCultureName&,const DotnetRegexExecutionStack&);
DotnetRegexNodeText dotnet_regex_escape(DotnetRegexNodeText);
DotnetRegexNodeText dotnet_regex_unescape(DotnetRegexNodeText);
std::int32_t dotnet_regex_map_capture_number(std::int32_t,const std::optional<std::map<std::int32_t,std::int32_t>>&);
std::u16string dotnet_regex_group_name_from_number(const std::optional<std::map<std::int32_t,std::int32_t>>&,const std::optional<std::vector<std::u16string>>&,std::int32_t capture_size,std::int32_t number);
DotnetRegexCultureName dotnet_regex_target_culture(std::int32_t options,const DotnetRegexCultureName& current_culture);
struct DotnetRegexParsedReplacement {
 DotnetRegexNodeText pattern;
 std::vector<DotnetRegexNodeText> strings;
 std::vector<std::int32_t> rules;
 bool has_backreferences=false;
};
DotnetRegexParsedReplacement dotnet_regex_parse_replacement(DotnetRegexNodeText pattern,std::int32_t options,const std::optional<std::map<std::int32_t,std::int32_t>>& captures,std::int32_t capture_size,const std::optional<std::map<std::u16string,std::int32_t>>& names,const DotnetRegexCultureName& current_culture,const DotnetRegexExecutionStack&);
}
