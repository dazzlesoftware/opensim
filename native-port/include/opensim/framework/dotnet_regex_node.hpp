// .NET8 RegexNode foundations; MIT third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/dotnet_regex_case.hpp"
#include "opensim/framework/dotnet_regex_class_analysis.hpp"
#include <variant>
namespace opensim::framework {
enum class DotnetRegexNodeKind : std::uint8_t {
    Unknown = 0,
    Oneloop = 3,
    Notoneloop = 4,
    Setloop = 5,
    Onelazy = 6,
    Notonelazy = 7,
    Setlazy = 8,
    One = 9,
    Notone = 10,
    Set = 11,
    Multi = 12,
    Backreference = 13,
    Bol = 14,
    Eol = 15,
    Boundary = 16,
    NonBoundary = 17,
    Beginning = 18,
    Start = 19,
    EndZ = 20,
    End = 21,
    Nothing = 22,
    Empty = 23,
    Alternate = 24,
    Concatenate = 25,
    Loop = 26,
    Lazyloop = 27,
    Capture = 28,
    Group = 29,
    PositiveLookaround = 30,
    NegativeLookaround = 31,
    Atomic = 32,
    BackreferenceConditional = 33,
    ExpressionConditional = 34,
    ECMABoundary = 41,
    NonECMABoundary = 42,
    Oneloopatomic = 43,
    Notoneloopatomic = 44,
    Setloopatomic = 45,
    UpdateBumpalong = 46
};
enum class DotnetRegexOptions : std::int32_t {
    None = 0,
    IgnoreCase = 1,
    Multiline = 2,
    ExplicitCapture = 4,
    Compiled = 8,
    Singleline = 16,
    IgnorePatternWhitespace = 32,
    RightToLeft = 64,
    ECMAScript = 256,
    CultureInvariant = 512,
    NonBacktracking = 1024
};
using DotnetRegexExecutionStack = std::function<bool()>;
using DotnetRegexNodeText = std::shared_ptr<const std::u16string>;
class DotnetRegexNodeArena;
struct DotnetRegexStartingLiteral {
    DotnetRegexRange range;
    DotnetRegexNodeText string, set_chars;
    std::shared_ptr<const std::vector<char16_t>> ascii_chars;
    bool negated = false;
};
class DotnetRegexNode {
    friend class DotnetRegexNodeArena;
    friend struct DotnetRegexNodeProbe;
    friend class DotnetRegexParser;
    DotnetRegexNode(DotnetRegexNodeKind kind, std::int32_t options) : kind_(kind), options_(options) {}
    DotnetRegexNodeKind kind_;
    std::int32_t options_;
    DotnetRegexNodeText str_;
    DotnetRegexNodeArena* arena_ = nullptr;
    char16_t ch_ = 0;
    std::int32_t m_ = 0, n_ = 0;
    DotnetRegexNode* parent_ = nullptr;
    std::variant<std::monostate, DotnetRegexNode*, std::vector<DotnetRegexNode*>> children_;
    std::vector<DotnetRegexNode*>& child_list();
    static void process_auto_atomic(DotnetRegexNode*, DotnetRegexNode*, const DotnetRegexExecutionStack&);
    DotnetRegexNode* extract_common_prefix_text(const DotnetRegexExecutionStack&);
    DotnetRegexNode* extract_common_prefix_one_notone_set(const DotnetRegexExecutionStack&);
    static void remove_prefix(DotnetRegexNode*, std::size_t);

public:
    DotnetRegexNode(const DotnetRegexNode&) = delete;
    DotnetRegexNode& operator=(const DotnetRegexNode&) = delete;
    DotnetRegexNodeKind kind() const {
        return kind_;
    }
    std::int32_t options() const {
        return options_;
    }
    const DotnetRegexNodeText& text() const {
        return str_;
    }
    char16_t character() const {
        return ch_;
    }
    std::int32_t minimum() const {
        return m_;
    }
    std::int32_t maximum() const {
        return n_;
    }
    DotnetRegexNode* parent() const {
        return parent_;
    }
    bool is_set_family() const;
    bool is_one_family() const;
    bool is_notone_family() const;
    int child_count() const;
    DotnetRegexNode* child(int) const;
    DotnetRegexNode* reverse_concatenation_if_right_to_left();
    // Source reductions and adoption retain detached nodes in their owning arena.
    DotnetRegexNode* reduce(const DotnetRegexExecutionStack&);
    DotnetRegexNode* reduce_alternation(const DotnetRegexExecutionStack&);
    DotnetRegexNode* reduce_atomic(const DotnetRegexExecutionStack&);
    DotnetRegexNode* reduce_lookaround(const DotnetRegexExecutionStack&);
    DotnetRegexNode* reduce_expression_conditional(const DotnetRegexExecutionStack&);
    DotnetRegexNode* reduce_backreference_conditional(const DotnetRegexExecutionStack&);
    void eliminate_ending_backtracking(const DotnetRegexExecutionStack&);
    void add_child(DotnetRegexNode*, const DotnetRegexExecutionStack&);
    void insert_child(int, DotnetRegexNode*, const DotnetRegexExecutionStack&);
    void replace_child(int, DotnetRegexNode*, const DotnetRegexExecutionStack&);
    DotnetRegexNode* final_optimize(const DotnetRegexExecutionStack&);
    DotnetRegexNode* make_quantifier(bool lazy, std::int32_t minimum, std::int32_t maximum,
                                     const DotnetRegexExecutionStack&);
    DotnetRegexNode* reduce_set();
    void make_loop_atomic();
    DotnetRegexNode* reduce_group();
    DotnetRegexNode* reduce_loops();
    DotnetRegexNode* replace_node_if_unnecessary();
    void reduce_adjacent_strings();
    void reduce_adjacent_loops();
    DotnetRegexNode* reduce_concatenation();
    static bool can_be_made_atomic(DotnetRegexNode*, DotnetRegexNode*, bool iterate_nullable_subsequent,
                                   bool allow_lazy, const DotnetRegexExecutionStack&);
    DotnetRegexNode* find_last_expression_in_loop_for_auto_atomic(const DotnetRegexExecutionStack&);
    void find_and_make_loops_atomic(const DotnetRegexExecutionStack&);
    void reduce_single_letter_and_nested_alternations();
    DotnetRegexNode* remove_redundant_empties_and_nothings();
    std::int32_t compute_min_length(const DotnetRegexExecutionStack&) const;
    std::optional<std::int32_t> compute_max_length(const DotnetRegexExecutionStack&) const;
    bool supports_compilation(std::optional<std::u16string>& reason) const;
    DotnetRegexNode* find_branch_one_or_multi_start();
    char16_t first_char_of_one_or_multi() const;
    DotnetRegexNode* find_starting_literal_node();
    // Original optimizer callers supply nonnegative, bounded buffer capacities.
    std::optional<DotnetRegexStartingLiteral> find_starting_literal(int max_set_characters = 5);
    bool try_get_ordinal_case_insensitive_string(int child_index, int exclusive_child_bound,
                                                 int& nodes_consumed, DotnetRegexNodeText&,
                                                 bool consume_zero_width_nodes = false);
};
// A regex owns its arena: raw parent/child edges retain identity without C++
// ownership cycles, and detached reduction nodes stay alive until arena release.
class DotnetRegexNodeArena {
    std::vector<std::unique_ptr<DotnetRegexNode>> nodes_;

public:
    DotnetRegexNodeArena() = default;
    DotnetRegexNodeArena(const DotnetRegexNodeArena&) = delete;
    DotnetRegexNodeArena& operator=(const DotnetRegexNodeArena&) = delete;
    DotnetRegexNode* create(DotnetRegexNodeKind, std::int32_t options = 0);
    DotnetRegexNode* create(DotnetRegexNodeKind, std::int32_t, char16_t);
    DotnetRegexNode* create(DotnetRegexNodeKind, std::int32_t, DotnetRegexNodeText);
    DotnetRegexNode* create(DotnetRegexNodeKind, std::int32_t, std::int32_t m, std::int32_t n = 0);
    DotnetRegexNode* create_one_with_case_conversion(char16_t, std::int32_t, const DotnetRegexCultureName&,
                                                     DotnetRegexCaseBehavior&);
};
}
