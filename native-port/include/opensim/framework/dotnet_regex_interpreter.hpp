// .NET8 RegexInterpreter/RegexRunner execution; MIT runtime license.
#pragma once
#include "opensim/framework/dotnet_regex_bytecode.hpp"
#include "opensim/framework/dotnet_regex_find.hpp"
namespace opensim::framework {
enum class DotnetRegexRunnerMode : int { ExistenceRequired, BoundsRequired, FullMatchRequired };
struct DotnetRegexCapture {
    std::int32_t index = 0, length = 0;
};
struct DotnetRegexGroup {
    std::int32_t number = 0;
    std::u16string name;
    bool success = false;
    std::int32_t index = 0, length = 0;
    std::vector<DotnetRegexCapture> captures;
};
struct DotnetRegexScanResult {
    DotnetRegexNodeText input;
    bool success = false;
    std::int32_t index = -1, length = -1, text_position = -1;
    std::vector<DotnetRegexGroup> groups;
};
using DotnetRegexTickCount = std::function<std::int64_t()>;
class DotnetRegexMatchTimeout : public DotnetRegexClassError {
  public:
    DotnetRegexNodeText input, pattern;
    std::int64_t timeout_ticks;
    DotnetRegexMatchTimeout(DotnetRegexNodeText, DotnetRegexNodeText, std::int32_t timeout_milliseconds);
};
void dotnet_regex_validate_timeout(std::int64_t ticks);
// This is the original backtracking interpreter, not a replacement for the
// separate symbolic NonBacktracking engine or the higher-level Regex API.
// One instance is one runner; independent instances may share immutable input.
class DotnetRegexInterpreter {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    DotnetRegexInterpreter(DotnetRegexParsedPattern, DotnetRegexNodeText pattern,
                           const DotnetRegexExecutionStack&);
    ~DotnetRegexInterpreter();
    DotnetRegexInterpreter(DotnetRegexInterpreter&&) noexcept;
    DotnetRegexInterpreter& operator=(DotnetRegexInterpreter&&) noexcept;
    DotnetRegexInterpreter(const DotnetRegexInterpreter&) = delete;
    DotnetRegexInterpreter& operator=(const DotnetRegexInterpreter&) = delete;
    DotnetRegexScanResult scan(DotnetRegexNodeText input, std::int32_t beginning, std::int32_t length,
                               std::int32_t start_at, std::int32_t previous_length = -1,
                               DotnetRegexRunnerMode mode = DotnetRegexRunnerMode::FullMatchRequired,
                               std::int64_t timeout_ticks = -10000, DotnetRegexTickCount clock = {});
};
}
