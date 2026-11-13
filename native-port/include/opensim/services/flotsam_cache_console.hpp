// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/flotsam_cache_module.hpp"
#include "opensim/services/flotsam_cache_config.hpp"
#include <stdexcept>
namespace opensim::services {
class FlotsamConsoleError : public std::runtime_error {
public:
    enum class Kind { NullReference, ArgumentOutOfRange };
    Kind kind;
    std::optional<std::string> parameter;
    std::optional<std::int64_t> actual_value;
    std::uint32_t hresult() const {
        return kind == Kind::NullReference ? 0x80004003u : 0x80131502u;
    }
    const char* source_type() const {
        return kind == Kind::NullReference ? "System.NullReferenceException"
                                           : "System.ArgumentOutOfRangeException";
    }
    FlotsamConsoleError(Kind kind, const std::string& message, std::optional<std::string> parameter = {},
                        std::optional<std::int64_t> actual = {})
        : runtime_error(message), kind(kind), parameter(std::move(parameter)), actual_value(actual) {}
};
class FlotsamConsoleOutput {
public:
    virtual ~FlotsamConsoleOutput() = default;
    virtual void output(const std::string&) = 0;
};
struct FlotsamConsoleBackend {
    using Text = std::optional<std::string>;
    using Date = framework::AssetDateTime;
    std::function<std::shared_ptr<FlotsamConsoleOutput>()> console;
    std::function<void(std::function<void()>, const std::string&)> post;
    std::function<std::int32_t()> memory_count, negative_count;
    std::function<void()> clear_memory, clear_negative, clear_files, load_defaults, delete_defaults;
    std::function<std::int32_t(const Text&)> file_count;
    std::function<std::vector<std::string>()> hit_report;
    std::function<std::vector<Text>(const Text&, const std::string&)> status_files;
    std::function<Date(const Text&)> last_write;
    std::function<std::string(Date, const std::string&)> format_date;
    std::function<void(int)> sleep;
    std::function<std::int32_t(bool)> touch;
    std::function<void(Date)> clean;
    std::function<Date()> now;
    std::function<bool(const Text&, Date&)> try_parse;
    // Current-culture ToLower and invariant ignore-case comparison remain explicit
    // host operations; an ASCII or ISO-only fallback would silently omit behavior.
    std::function<std::string(const std::string&)> lower;
    std::function<bool(const Text&)> equals_now;
};
class FlotsamCacheConsole {
public:
    using Arguments = std::vector<std::optional<std::string>>;
    FlotsamCacheConsole(std::shared_ptr<FlotsamInstanceConfig>, std::shared_ptr<FlotsamCacheLifecycle>,
                        std::shared_ptr<FlotsamConsoleBackend>);
    // Arguments are source-owned and may contain null string elements. Tasks retain
    // the configuration/lifecycle/backend/output they need after handle returns.
    void handle(const std::shared_ptr<Arguments>&);
    // Source expire operation is reusable by hosts that expose this command before
    // the complete scene-gathering console surface is available. Output is captured
    // once by the caller, as in HandleConsoleCommands.
    static void expire(const std::shared_ptr<Arguments>&, const std::shared_ptr<FlotsamInstanceConfig>&,
                       const std::shared_ptr<FlotsamCacheLifecycle>&,
                       const std::shared_ptr<FlotsamConsoleBackend>&,
                       const std::shared_ptr<FlotsamConsoleOutput>&);
    static void status(const std::shared_ptr<FlotsamInstanceConfig>&,
                       const std::shared_ptr<FlotsamCacheLifecycle>&,
                       const std::shared_ptr<FlotsamConsoleBackend>&,
                       const std::shared_ptr<FlotsamConsoleOutput>&);
    static void touch_assets(const std::shared_ptr<FlotsamCacheLifecycle>&,
                             const std::shared_ptr<FlotsamConsoleBackend>&,
                             const std::shared_ptr<FlotsamConsoleOutput>&);

private:
    std::shared_ptr<FlotsamInstanceConfig> config_;
    std::shared_ptr<FlotsamCacheLifecycle> life_;
    std::shared_ptr<FlotsamConsoleBackend> backend_;
};
}
