// .NET8 RegexFindOptimizations; MIT runtime license.
#pragma once
#include "opensim/framework/dotnet_regex_prefix.hpp"
namespace opensim::framework {
enum class DotnetRegexFindMode : std::int32_t {
    LeadingAnchor_LeftToRight_Beginning,
    LeadingAnchor_LeftToRight_Start,
    LeadingAnchor_LeftToRight_EndZ,
    LeadingAnchor_LeftToRight_End,
    LeadingAnchor_RightToLeft_Beginning,
    LeadingAnchor_RightToLeft_Start,
    LeadingAnchor_RightToLeft_EndZ,
    LeadingAnchor_RightToLeft_End,
    TrailingAnchor_FixedLength_LeftToRight_End,
    TrailingAnchor_FixedLength_LeftToRight_EndZ,
    LeadingString_LeftToRight,
    LeadingString_RightToLeft,
    LeadingString_OrdinalIgnoreCase_LeftToRight,
    LeadingSet_LeftToRight,
    LeadingSet_RightToLeft,
    LeadingChar_RightToLeft,
    FixedDistanceChar_LeftToRight,
    FixedDistanceString_LeftToRight,
    FixedDistanceSets_LeftToRight,
    LiteralAfterLoop_LeftToRight,
    NoSearch
};
struct DotnetRegexFindData {
    DotnetRegexFindMode mode = DotnetRegexFindMode::NoSearch;
    DotnetRegexNodeKind leading_anchor = DotnetRegexNodeKind::Unknown,
                        trailing_anchor = DotnetRegexNodeKind::Unknown;
    std::int32_t minimum_length = 0;
    std::optional<std::int32_t> maximum_length;
    DotnetRegexNodeText leading_prefix;
    struct {
        char16_t character = 0;
        DotnetRegexNodeText string;
        std::int32_t distance = 0;
    } fixed_literal;
    std::optional<std::vector<DotnetRegexFixedDistanceSet>> fixed_sets;
    std::optional<DotnetRegexLiteralAfterLoop> literal_after_loop;
};
class DotnetRegexFindOptimizations {
    std::shared_ptr<DotnetRegexNodeArena> owner_;
    DotnetRegexFindData data_;
    std::unique_ptr<DotnetRegexAsciiCacheSlot[]> ascii_;
    std::size_t ascii_count_ = 0;
    void caches(std::size_t count);

public:
    DotnetRegexFindOptimizations(const DotnetRegexParsedPattern&, const DotnetRegexExecutionStack&);
    const DotnetRegexFindData& data() const {
        return data_;
    }
    bool is_useful() const {
        return data_.mode != DotnetRegexFindMode::NoSearch || data_.leading_anchor == DotnetRegexNodeKind::Bol;
    }
    bool try_find_left_to_right(std::u16string_view, std::int32_t& position, std::int32_t start);
    bool try_find_right_to_left(std::u16string_view, std::int32_t& position, std::int32_t start);
    // Observing these slots does not initialize them; matching uses the original
    // per-slot concurrent ASCII cache kernel.
    std::size_t ascii_lookup_count() const {
        return ascii_count_;
    }
    const DotnetRegexAsciiCacheSlot& ascii_lookup(std::size_t index) const;
};
}
