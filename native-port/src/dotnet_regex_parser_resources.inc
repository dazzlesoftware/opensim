// Pinned .NET8.0.31 invariant RegexParser resources; MIT runtime license.
constexpr std::u16string_view parse_messages[]={
 u"", // Unknown
 u"Too many | in (?()|).", // AlternationHasTooManyConditions
 u"Illegal conditional (?(...)) expression.", // AlternationHasMalformedCondition
 u"Incomplete \\p{X} character escape.", // InvalidUnicodePropertyEscape
 u"Malformed \\p{X} character escape.", // MalformedUnicodePropertyEscape
 u"Unrecognized escape sequence \\{0}.", // UnrecognizedEscape
 u"Unrecognized control character.", // UnrecognizedControlCharacter
 u"Missing control character.", // MissingControlCharacter
 u"Insufficient or invalid hexadecimal digits.", // InsufficientOrInvalidHexDigits
 u"Quantifier and capture group numbers must be less than or equal to Int32.MaxValue.", // QuantifierOrCaptureGroupOutOfRange
 u"Reference to undefined group name '{0}'.", // UndefinedNamedReference
 u"Reference to undefined group number {0}.", // UndefinedNumberedReference
 u"Malformed \\k<...> named back reference.", // MalformedNamedReference
 u"Illegal \\ at end of pattern.", // UnescapedEndingBackslash
 u"Unterminated (?#...) comment.", // UnterminatedComment
 u"Unrecognized grouping construct.", // InvalidGroupingConstruct
 u"Alternation conditions do not capture and cannot be named.", // AlternationHasNamedCapture
 u"Alternation conditions cannot be comments.", // AlternationHasComment
 u"Conditional alternation is missing a closing parenthesis after the group number {0}.", // AlternationHasMalformedReference
 u"Conditional alternation refers to an undefined group number {0}.", // AlternationHasUndefinedReference
 u"Invalid group name: Group names must begin with a word character.", // CaptureGroupNameInvalid
 u"Capture number cannot be zero.", // CaptureGroupOfZero
 u"Unterminated [] set.", // UnterminatedBracket
 u"A subtraction must be the last element in a character class.", // ExclusionGroupNotLast
 u"[x-y] range in reverse order.", // ReversedCharacterRange
 u"Cannot include class \\{0} in character range.", // ShorthandClassInCharacterRange
 u"Not enough )'s.", // InsufficientClosingParentheses
 u"Illegal {x,y} with x > y.", // ReversedQuantifierRange
 u"Nested quantifier '{0}'.", // NestedQuantifiersNotParenthesized
 u"Quantifier '{0}' following nothing.", // QuantifierAfterNothing
 u"Too many )'s.", // InsufficientOpeningParentheses
 u"Unknown property '{0}'.", // UnrecognizedUnicodeProperty
};
