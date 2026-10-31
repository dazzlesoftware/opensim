// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>
namespace opensim::script {
using LslValue=std::variant<std::int32_t,std::string>;
struct LslDiagnostic {std::size_t line,column;std::string message;};
class LslProgram;
struct LslCompileResult {std::shared_ptr<const LslProgram> program;std::vector<LslDiagnostic> diagnostics;explicit operator bool()const{return bool(program);}};
// Supported grammar is intentionally bounded. Unsupported syntax/types/API calls
// fail compilation; no partial program or hardcoded asset-text execution occurs.
LslCompileResult compile_lsl(std::string_view source);
struct LslDetector {framework::Uuid key;};
struct LslEvent {std::string name;std::vector<LslValue> arguments;std::vector<LslDetector> detectors;};
struct LslContext {framework::Uuid object,owner,group,item;std::string object_name;framework::Vector3 position;};
enum class LslChatType {Say,Owner};
struct LslChat {LslChatType type;std::int32_t channel;std::string text;LslContext context;};
struct LslHost {
 // Time in 100ns ticks. Defaults to a steady clock; tests/hosts can inject time.
 std::function<std::int64_t()> clock_ticks;
 std::function<void(const LslChat&)> chat;
 std::function<void(const LslChat&)> listener;
 std::function<void(const std::string&)> console;
 std::function<void(std::int32_t milliseconds)> request_sleep;
 std::optional<std::int32_t> console_channel;
 float script_delay_factor=1.f;
};
enum class LslRunStatus {Idle,BudgetExhausted,Sleeping,Faulted};
struct LslRunResult {LslRunStatus status;std::size_t instructions=0;std::string error;};
class LslInstance {
public:
 LslInstance(std::shared_ptr<const LslProgram>,LslContext,LslHost);
 LslInstance(std::shared_ptr<const LslProgram>,LslContext,LslHost,bool start_running);
 ~LslInstance();
 LslInstance(LslInstance&&)noexcept;
 LslInstance& operator=(LslInstance&&)noexcept;
 LslInstance(const LslInstance&)=delete;
 void reset();
 bool running()const;
 void running(bool);
 bool post_event(LslEvent);
 LslRunResult run(std::size_t instruction_budget);
 std::size_t queued_events()const;
 bool handles_event(std::string_view name)const;
 std::uint64_t event_mask()const;
 // Valid during host callbacks and while an event is budget-suspended.
 const LslEvent* current_event()const;
 void set_context(LslContext);
private:
 struct Impl;std::unique_ptr<Impl> impl_;
};
}
