// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace opensim;
using namespace framework;
using namespace script;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

std::shared_ptr<const LslProgram> compile(const std::string &source) {
    const auto compiled = compile_lsl(source);
    std::string diagnostics;
    for (const auto &error : compiled.diagnostics)
        diagnostics += "\n(" + std::to_string(error.line) + "," + std::to_string(error.column) + "): " + error.message;
    check(bool(compiled), "User-function fixture failed to compile" + diagnostics);
    return compiled.program;
}

LslContext context() {
    LslContext result;
    result.object = Uuid::random();
    result.owner = Uuid::random();
    result.item = Uuid::random();
    result.object_name = "User-function fixture";
    return result;
}

void finish(LslInstance &instance, std::size_t budget = 7) {
    for (int i = 0; i < 10000; ++i) {
        const auto result = instance.run(budget);
        check(result.status != LslRunStatus::Faulted, "User-function VM faulted: " + result.error);
        check(result.status != LslRunStatus::Sleeping, "Unexpected sleep in a synchronous user function");
        if (result.status == LslRunStatus::Idle)
            return;
    }
    throw std::runtime_error("User-function VM did not complete within its instruction budget");
}

LslCheckpoint snapshot(LslInstance &instance) {
    const auto saved = instance.checkpoint();
    check(bool(saved), "User-function checkpoint unavailable");
    return *saved;
}

void typed_and_forward() {
    const auto program = compile(R"LSL(
        integer value=7;
        integer result;
        string text;
        vector position;
        rotation orientation;
        list values;
        key identity;
        float real;
        integer first(integer n) {return later(n)+1;}
        integer later(integer n) {
            integer value=n*2;
            {integer inner=1; value+=inner;}
            return value;
        }
        mark(integer value) {result+=value; return;}
        string format(string value) {return value+"!";}
        vector shifted(vector value) {vector offset=<1,2,3>; return value+offset;}
        rotation same(rotation value) {return value;}
        list grown(list value) {value += [9]; return value;}
        key from_string(string value) {return value;}
        float widened(integer value) {return value;}
        default {state_entry() {
            result=first(4); mark(3);
            text=format("okay"); position=shifted(<3,4,5>);
            orientation=same(<1,2,3,4>);
            list original=[1,2]; values=grown(original);
            identity=from_string("00112233-4455-4677-8899-aabbccddeeff"); real=widened(4);
            if(llGetListLength(original)!=2) result=-99;
        }}
    )LSL");
    LslInstance instance(program, context(), {});
    finish(instance, 1);
    const auto state = snapshot(instance);
    check(state.globals.size() == 8 && std::get<std::int32_t>(state.globals[0]) == 7 &&
              std::get<std::int32_t>(state.globals[1]) == 13,
          "Typed/void/forward functions lost parameter isolation, local scope or global writes");
    check(std::get<std::string>(state.globals[2]) == "okay!" &&
              std::get<Vector3d>(state.globals[3]) == Vector3d{4, 6, 8} &&
              std::get<LslRotation>(state.globals[4]) == LslRotation{1, 2, 3, 4},
          "String/vector/rotation function arguments or return values changed");
    const auto list = std::get<std::shared_ptr<const LslList>>(state.globals[5]);
    check(list && list->items.size() == 3 && std::get<std::int32_t>(unbox_lsl_value(list->items[0])) == 1 &&
              std::get<std::int32_t>(unbox_lsl_value(list->items[1])) == 2 &&
              std::get<std::int32_t>(unbox_lsl_value(list->items[2])) == 9 &&
              std::get<std::string>(state.globals[6]) == "00112233-4455-4677-8899-aabbccddeeff" &&
              std::get<double>(state.globals[7]) == 4.0,
          "List/key or integer-to-float function coercion changed");
}

void recursion_and_order() {
    const auto program = compile(R"LSL(
        integer factorial_result;
        integer order;
        integer packed;
        integer arithmetic;
        integer factorial(integer n) {if(n<=1) return 1; return n*factorial(n-1);}
        integer stamp(integer n) {order=order*10+n; return n;}
        integer pack(integer a,integer b,integer c) {return 100*a+10*b+c;}
        default {state_entry() {
            factorial_result=factorial(6);
            packed=pack(stamp(1),stamp(2),stamp(3));
            if(order!=123) packed=-1;
            order=0; arithmetic=stamp(4)+stamp(5)*stamp(6);
        }}
    )LSL");
    LslInstance instance(program, context(), {});
    finish(instance, 1);
    const auto saved = snapshot(instance);
    check(saved.globals.size() == 4 && std::get<std::int32_t>(saved.globals[0]) == 720 &&
              std::get<std::int32_t>(saved.globals[1]) == 654 && std::get<std::int32_t>(saved.globals[2]) == 123 &&
              std::get<std::int32_t>(saved.globals[3]) == 34,
          "Recursive calls lost isolated frames, left-to-right user arguments or right-to-left binary evaluation");

    // Unchanged original YEngine IL evaluates user arguments left-to-right but
    // defers direct variable/component reads until the call loads its arguments.
    const auto deferred = compile(R"LSL(
        vector value=<1,0,0>;
        float result;
        integer change(){value=<2,0,0>;return 3;}
        float pack(float first,integer second){return first*10+second;}
        default {state_entry(){result=pack(value.x,change());}}
    )LSL");
    LslInstance deferred_call(deferred, context(), {});
    finish(deferred_call, 1);
    const auto deferred_state = snapshot(deferred_call);
    check(std::get<double>(deferred_state.globals[1]) == 23,
          "User function loaded a direct component argument before a later argument changed its variable");
}

void sleep_and_checkpoint() {
    const auto program = compile(R"LSL(
        integer trace;
        integer answer=-1;
        integer stamp(integer n) {trace=trace*10+n; return n;}
        integer nap(integer n) {trace=trace*10+2; llSleep(1.0); trace=trace*10+3; return n+1;}
        integer outer(integer n) {integer before=stamp(1); return before+nap(n)+stamp(4);}
        default {state_entry() {answer=outer(5)+stamp(9);}}
    )LSL");
    std::int64_t source_clock = 10000000;
    std::vector<int> source_delays;
    LslHost source_host;
    source_host.clock_ticks = source_host.monotonic_ticks = [&] { return source_clock; };
    source_host.request_sleep = [&](int milliseconds) { source_delays.push_back(milliseconds); };
    const auto caller = context();
    LslInstance source(program, caller, source_host);
    check(source.run(10000).status == LslRunStatus::Sleeping, "Nested sleeping function did not suspend its caller");
    auto saved = snapshot(source);
    check(saved.globals.size() == 2 && std::get<std::int32_t>(saved.globals[0]) == 9142 &&
              std::get<std::int32_t>(saved.globals[1]) == -1 && saved.sleep_remaining == 10000000 &&
              source_delays == std::vector<int>{1000},
          "Nested function sleep replayed operands or lost active caller locals");
    const auto bytes = encode_lsl_checkpoint(saved);
    const auto decoded = decode_lsl_checkpoint(bytes);
    check(decoded && encode_lsl_checkpoint(*decoded) == bytes,
          "Nested call checkpoint did not round-trip deterministically");
    const auto transferred = decode_lsl_checkpoint(encode_lsl_checkpoint(saved, false));
    check(transferred && encode_lsl_checkpoint(*transferred) == bytes,
          "Excluding optional service extensions discarded core function continuation state");
    std::int64_t destination_clock = 90000000;
    std::vector<int> destination_delays;
    LslHost destination_host;
    destination_host.clock_ticks = destination_host.monotonic_ticks = [&] { return destination_clock; };
    destination_host.request_sleep = [&](int milliseconds) { destination_delays.push_back(milliseconds); };
    LslInstance restored(program, caller, destination_host);
    check(restored.restore_checkpoint(*decoded), "Nested function checkpoint could not restore a new VM");
    check(!saved.function.empty() && !saved.call_stack.empty(),
          "Nested sleeping checkpoint omitted active call frames");
    const auto target_before = encode_lsl_checkpoint(snapshot(restored));
    std::vector<LslCheckpoint> malformed;
    malformed.push_back(saved);
    malformed.back().function = "unknown_function";
    malformed.push_back(saved);
    malformed.back().locals.push_back(std::int32_t{0});
    malformed.push_back(saved);
    malformed.back().instruction = std::numeric_limits<std::size_t>::max();
    malformed.push_back(saved);
    malformed.back().call_stack.back().function = "unknown_caller";
    malformed.push_back(saved);
    malformed.back().call_stack.back().return_expression = std::numeric_limits<std::size_t>::max();
    malformed.push_back(saved);
    malformed.back().evaluations.push_back({std::numeric_limits<std::size_t>::max(), std::int32_t{0}});
    malformed.push_back(saved);
    malformed.back().call_stack.back().evaluations.push_back(
        {std::numeric_limits<std::size_t>::max(), std::int32_t{0}});
    for (const auto &invalid : malformed) {
        check(!restored.restore_checkpoint(invalid), "Malformed user-function checkpoint was admitted");
        check(encode_lsl_checkpoint(snapshot(restored)) == target_before,
              "Rejected user-function checkpoint mutated the active target VM");
    }
    check(restored.run(10000).status == LslRunStatus::Sleeping, "Restored sleeping call resumed too early");
    destination_clock += 9999999;
    check(restored.run(10000).status == LslRunStatus::Sleeping, "Restored nested call lost relative wake time");
    ++destination_clock;
    finish(restored, 1);
    const auto resumed = snapshot(restored);
    check(std::get<std::int32_t>(resumed.globals[0]) == 91423 && std::get<std::int32_t>(resumed.globals[1]) == 20 &&
              destination_delays.empty(),
          "Restored nested function replayed side effects, sleep or completed operands");
    source_clock += 10000000;
    finish(source, 1);
    const auto original = snapshot(source);
    check(std::get<std::int32_t>(original.globals[0]) == 91423 && std::get<std::int32_t>(original.globals[1]) == 20 &&
              source_delays == std::vector<int>{1000},
          "Live nested function did not resume exactly once");
    check(encode_lsl_checkpoint(saved) == bytes && encode_lsl_checkpoint(*decoded) == bytes,
          "Resuming a function mutated its saved checkpoint");
}

void overloads_initializers_and_budget() {
    const auto program = compile(R"LSL(
        integer initial=forward_init();
        integer overloaded;
        integer parity;
        integer forward_init(){return 3;}
        integer pick(integer n){return n+10;}
        integer pick(float n){return (integer)n+20;}
        integer odd(integer n){if(n==0)return 0;return even(n-1);}
        integer even(integer n){if(n==0)return 1;return odd(n-1);}
        default {state_entry(){overloaded=pick(1)+pick(1.5);parity=even(6)*10+odd(5);}}
    )LSL");
    LslInstance instance(program, context(), {});
    finish(instance, 1);
    const auto state = snapshot(instance);
    check(std::get<std::int32_t>(state.globals[0]) == 3 && std::get<std::int32_t>(state.globals[1]) == 32 &&
              std::get<std::int32_t>(state.globals[2]) == 11,
          "Overload resolution, mutual recursion or forward global initialization changed");

    const auto recursive = compile(R"LSL(
        integer result;
        integer recur(integer n){if(n<=0)return 0;return 1+recur(n-1);}
        default {state_entry(){result=recur(16);}}
    )LSL");
    LslInstance bounded(recursive, context(), {});
    bool suspended_call = false;
    for (int i = 0; i < 1000 && !suspended_call; ++i) {
        const auto progress = bounded.run(1);
        check(progress.instructions <= 1 && progress.status == LslRunStatus::BudgetExhausted,
              "Recursive function bypassed its execution budget");
        suspended_call = snapshot(bounded).call_stack.size() >= 4;
    }
    check(suspended_call, "Recursive function did not retain a bounded suspended call stack");
    finish(bounded, 1);
    check(std::get<std::int32_t>(snapshot(bounded).globals[0]) == 16, "Budgeted recursion resumed incorrectly");
}

void initializer_sleep_checkpoint() {
    const auto program = compile(R"LSL(
        integer trace;
        integer value=initialize();
        integer done;
        integer initialize(){trace=trace*10+1;llSleep(0.5);trace=trace*10+2;return 7;}
        default {state_entry(){done=value+1;}}
    )LSL");
    std::int64_t clock = 0;
    std::vector<int> sleeps;
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    host.request_sleep = [&](int milliseconds) { sleeps.push_back(milliseconds); };
    const auto caller = context();
    LslInstance before(program, caller, host);
    check(before.run(10000).status == LslRunStatus::Sleeping, "Sleeping global initializer did not yield");
    const auto saved = snapshot(before);
    check(!saved.function.empty() && !saved.call_stack.empty() && saved.sleep_remaining == 5000000 &&
              std::get<std::int32_t>(saved.globals[0]) == 1 && sleeps == std::vector<int>{500},
          "Global initializer did not preserve its in-progress function and side effects");
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    check(bool(decoded), "Sleeping global initializer checkpoint could not be decoded");
    clock = 90000000;
    LslInstance restored(program, caller, host);
    check(restored.restore_checkpoint(*decoded), "Sleeping global initializer checkpoint could not restore");
    check(restored.run(10000).status == LslRunStatus::Sleeping, "Restored global initializer resumed before its wake");
    clock += 5000000;
    finish(restored, 1);
    const auto state = snapshot(restored);
    check(std::get<std::int32_t>(state.globals[0]) == 12 && std::get<std::int32_t>(state.globals[1]) == 7 &&
              std::get<std::int32_t>(state.globals[2]) == 8 && sleeps == std::vector<int>{500},
          "Restored initializer replayed its call/sleep or failed to continue global/state initialization");
    const auto completed = decode_lsl_checkpoint(encode_lsl_checkpoint(state));
    LslInstance completed_restore(program, caller, host);
    check(completed && completed_restore.restore_checkpoint(*completed),
          "Completed later-index global initializer left an invalid inactive checkpoint");
    finish(completed_restore, 1);
    const auto completed_state = snapshot(completed_restore);
    check(std::get<std::int32_t>(completed_state.globals[0]) == 12 &&
              std::get<std::int32_t>(completed_state.globals[2]) == 8 && sleeps == std::vector<int>{500},
          "Completed initializer checkpoint replayed global initialization");
}

void reset_and_states() {
    const auto resetting = compile(R"LSL(
        integer count;
        resetter() {count=42; llResetScript(); count=99;}
        default {state_entry() {count+=1;} touch_start(integer n) {count=5; resetter(); count=88;}}
    )LSL");
    LslInstance reset(resetting, context(), {});
    finish(reset);
    check(std::get<std::int32_t>(snapshot(reset).globals[0]) == 1, "Reset fixture did not initialize");
    check(reset.post_event({"touch_start", {std::int32_t{1}}, {}}), "Could not dispatch reset fixture touch");
    finish(reset, 1);
    check(std::get<std::int32_t>(snapshot(reset).globals[0]) == 1,
          "Reset inside a function resumed an abandoned function/caller or retained globals");

    const auto changing = compile(R"LSL(
        string trace;
        change_state() {trace+="F"; state next; trace+="X";}
        default {state_entry() {trace+="D"; change_state(); trace+="Y";} state_exit() {trace+="E";}}
        state next {state_entry() {trace+="N";}}
    )LSL");
    LslInstance transition(changing, context(), {});
    finish(transition, 1);
    const auto changed = snapshot(transition);
    check(changed.state == "next" && std::get<std::string>(changed.globals[0]) == "DFEN",
          "State change inside a function failed to unwind callers and run state_exit/state_entry in order");
}

void cross_state_event_admission() {
    const auto program = compile(R"LSL(
        integer result;
        default {state_entry(){} touch_start(integer count){state ready;}}
        state ready {
            state_entry(){}
            link_message(integer sender,integer number,string text,key id){result=result*10+number;}
        }
    )LSL");
    LslHost host;
    host.clock_ticks = host.monotonic_ticks = [] { return std::int64_t{10000000}; };
    const auto link = [](std::int32_t number) {
        return LslEvent{
            "link_message",
            {std::int32_t{1}, number, std::string("queued"), std::string("11111111-2222-3333-4444-555555555555")},
            {}};
    };
    LslInstance instance(program, context(), host);
    finish(instance, 1);
    constexpr auto link_bit = std::uint64_t{1} << 34;
    check(!(instance.event_mask() & link_bit) && !instance.handles_event("link_message") &&
              (instance.declared_event_mask() & link_bit),
          "Any-state declarations changed current-state subscriptions");
    check(instance.post_event(link(1)) && instance.post_event(link(2)),
          "Other-state link messages were rejected at admission");
    auto saved = snapshot(instance);
    check(saved.queued_events.size() == 2, "Other-state event queue was lost before checkpoint");
    LslInstance restored(program, context(), host);
    check(restored.restore_checkpoint(saved), "Checkpoint rejected events declared only in another state");
    check(snapshot(restored).queued_events.size() == 2, "Restored cross-state event queue changed");
    finish(restored, 1);
    check(std::get<std::int32_t>(snapshot(restored).globals[0]) == 0,
          "Event dispatched to a handler outside the current state");

    LslInstance projected(program, context(), host);
    finish(projected, 1);
    std::vector<LslEvent> many;
    for (int i = 0; i < 65; ++i)
        many.push_back(link(i));
    projected.restore_queued_events(many);
    check(projected.queued_events() == 64, "Queued projection lost any-state events or exceeded the per-event limit");
    const auto full = snapshot(projected);
    LslInstance bounded(program, context(), host);
    check(bounded.restore_checkpoint(full) && !bounded.post_event(link(7)),
          "Restored per-event admission limit changed");
    auto malformed = full;
    malformed.queued_events.push_back(link(8));
    const auto before = encode_lsl_checkpoint(snapshot(bounded));
    check(!bounded.restore_checkpoint(malformed) && encode_lsl_checkpoint(snapshot(bounded)) == before,
          "Oversized queued-event checkpoint was accepted or changed the VM");
    malformed = full;
    malformed.active_event = link(9);
    malformed.locals = malformed.active_event->arguments;
    check(!bounded.restore_checkpoint(malformed), "Active-event validation used any-state declarations");
    auto bad = link(1);
    bad.arguments[0] = std::string("wrong sender type");
    check(instance.post_event(bad), "Any-state admission rejected original raw event arguments");
    const auto admitted_bad = snapshot(instance);
    check(admitted_bad.queued_events.size() == 3 &&
              std::get<std::string>(admitted_bad.queued_events.back().arguments.front()) == "wrong sender type",
          "Admission coerced or discarded a raw argument before handler dispatch");
    finish(instance, 1);
    check(snapshot(instance).queued_events.empty() && std::get<std::int32_t>(snapshot(instance).globals[0]) == 0,
          "An event without a current-state handler was dispatched instead of discarded");

    // Active original StateChange clears all pending events. The old
    // ClearQueueExceptLinkMessages helper is commented out, not its contract.
    LslInstance changing(program, context(), host);
    finish(changing, 1);
    check(changing.post_event({"touch_start", {std::int32_t{1}}, {}}) && changing.post_event(link(1)) &&
              changing.post_event(link(2)),
          "Could not prepare a queued cross-state transition");
    LslInstance changed(program, context(), host);
    check(changed.restore_checkpoint(snapshot(changing)),
          "Cross-state transition queue did not survive checkpoint restoration");
    finish(changed, 1);
    check(snapshot(changed).state == "ready" && std::get<std::int32_t>(snapshot(changed).globals[0]) == 0,
          "State transition did not preserve original all-event queue clearing");
    check(changed.handles_event("link_message") && (changed.event_mask() & link_bit) && changed.post_event(link(3)) &&
              changed.post_event(link(4)),
          "New-state handler did not accept its events");
    finish(changed, 1);
    check(std::get<std::int32_t>(snapshot(changed).globals[0]) == 34,
          "Current-state event delivery lost FIFO ordering");
    LslInstance invalid_dispatch(program, context(), host);
    check(invalid_dispatch.restore_checkpoint(snapshot(changed)) && invalid_dispatch.post_event(bad),
          "Current-state raw event could not reach the original dispatch boundary");
    const auto rejected_at_dispatch = invalid_dispatch.run(1);
    check(rejected_at_dispatch.status == LslRunStatus::Faulted && rejected_at_dispatch.instructions == 0 &&
              rejected_at_dispatch.error == "Specified cast is not valid.",
          "Required event argument prefix was not rejected before executing the handler body");

    const auto implicit_entry = compile("default{touch_start(integer count){state other;}} state other{}");
    LslInstance generated(implicit_entry, context(), host);
    constexpr auto entry_bit = std::uint64_t{1} << 30;
    constexpr auto exit_bit = std::uint64_t{1} << 1;
    const auto initial = snapshot(generated);
    check(initial.queued_events.size() == 1 && initial.queued_events[0].name == "state_entry" &&
              (generated.declared_event_mask() & entry_bit) && !(generated.declared_event_mask() & exit_bit),
          "Original generated default entry was omitted or synthetic exits became declarations");
    check(!(generated.event_mask() & entry_bit), "Generated default entry changed current-state world subscriptions");
    LslInstance generated_copy(implicit_entry, context(), host);
    check(generated_copy.restore_checkpoint(initial) && generated_copy.post_event({"state_entry", {}, {}}) &&
              !generated_copy.post_event({"state_exit", {}, {}}),
          "Generated default entry admission/checkpoint or undeclared exit rejection changed");
    finish(generated_copy, 1);
    check(generated_copy.post_event({"touch_start", {std::int32_t{1}}, {}}),
          "Could not enter state without an entry handler");
    finish(generated_copy, 1);
    check(snapshot(generated_copy).state == "other" && !(generated_copy.event_mask() & entry_bit) &&
              (generated_copy.declared_event_mask() & entry_bit) && !(generated_copy.declared_event_mask() & exit_bit),
          "Generated default entry broadened another state's world subscriptions");
}

void lowered_stack_native_checkpoint() {
    for (const bool new_call : {false, true}) {
        const std::string source = "integer entries;integer calls;integer result;"
                                   "integer checkedLeaf(){while(FALSE){}return 7;}"
                                   "integer recur(integer n){calls++;if(n>0)return recur(n-1);"
                                   "integer k=1;while(k>0){k--;}return " +
                                   std::string(new_call ? "checkedLeaf()" : "7") +
                                   ";}default{state_entry(){entries++;result=recur(130);}}";
        const auto program = compile(source);
        std::int64_t clock = 0;
        LslHost original_host;
        original_host.clock_ticks = original_host.monotonic_ticks = [&] { return clock; };
        original_host.stack_limit_bytes = 64 * 1024;
        const auto identity = context();
        LslInstance original(program, identity, original_host);
        bool captured = false;
        LslCheckpoint saved;
        for (unsigned step = 0; step < 10000; ++step) {
            const auto progress = original.run(1);
            check(progress.status != LslRunStatus::Faulted,
                  "Native recursion faulted before reaching its checked 64K continuation: " + progress.error);
            saved = snapshot(original);
            if (saved.call_stack.size() >= 130 && saved.locals.size() == 2 &&
                std::get<std::int32_t>(saved.locals[0]) == 0 && std::get<std::int32_t>(saved.locals[1]) == 1) {
                captured = true;
                break;
            }
        }
        check(captured && std::get<std::int32_t>(saved.globals[0]) == 1 &&
                  std::get<std::int32_t>(saved.globals[1]) == 131,
              "Native checkpoint did not preserve its already-entered oversized recursive stack");
        const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
        auto reduced_host = original_host;
        reduced_host.stack_limit_bytes = 16 * 1024;
        LslInstance reduced(program, identity, reduced_host);
        check(decoded && reduced.restore_checkpoint(*decoded),
              "Reducing configured stack size rejected an existing native continuation during restore");
        if (new_call) {
            LslRunResult progress;
            auto before_fault = snapshot(reduced);
            for (unsigned step = 0; step < 10000; ++step) {
                progress = reduced.run(1);
                if (progress.status == LslRunStatus::Faulted || progress.status == LslRunStatus::Idle)
                    break;
                before_fault = snapshot(reduced);
                check(std::get<std::int32_t>(before_fault.globals[0]) == 1 &&
                          std::get<std::int32_t>(before_fault.globals[1]) == 131 &&
                          std::get<std::int32_t>(before_fault.globals[2]) == 0,
                      "Reduced-stack continuation replayed its caller before the new method check");
            }
            check(progress.status == LslRunStatus::Faulted &&
                      progress.error.find("OutOfStackException") != std::string::npos,
                  "A new complex method entry bypassed the reduced stack check");
            // Faulted VMs deliberately expose no checkpoint. Verify the last
            // valid continuation immediately before the rejected new entry.
            check(std::get<std::int32_t>(before_fault.globals[0]) == 1 &&
                      std::get<std::int32_t>(before_fault.globals[1]) == 131 &&
                      std::get<std::int32_t>(before_fault.globals[2]) == 0,
                  "Reduced-stack method fault replayed its caller or completed the forbidden new call");
        } else {
            finish(reduced, 1);
            const auto done = snapshot(reduced);
            check(std::get<std::int32_t>(done.globals[0]) == 1 && std::get<std::int32_t>(done.globals[1]) == 131 &&
                      std::get<std::int32_t>(done.globals[2]) == 7 && done.function.empty() && done.call_stack.empty(),
                  "Reduced-stack existing continuation could not unwind without replay or a new stack check");
        }
    }
}

void compiler_errors() {
    for (const std::string &bad :
         {"integer same(){return 1;} integer same(){return 2;} default{state_entry(){}}",
          "integer f(integer n,integer n){return n;} default{state_entry(){}}",
          "integer f(integer n){return n;} default{state_entry(){f();}}",
          "integer f(integer n){return n;} default{state_entry(){f(1,2);}}",
          "integer f(integer n){return n;} default{state_entry(){f(<1,2,3>);}}",
          "integer f(){return \"wrong\";} default{state_entry(){}}", "integer f(){return;} default{state_entry(){}}",
          "f(){return 1;} default{state_entry(){}}", "f(){} default{state_entry(){integer n=f();}}",
          "integer f(){ {integer local=1;} return local;} default{state_entry(){}}",
          "integer missing(){integer n=1;} default{state_entry(){}}",
          "integer f(integer n){if(n)return 1;} default{state_entry(){}}",
          "integer f(integer n){return n;} default{state_entry(){integer n=f(1.5);}}",
          "default{state_entry(){integer n=+1;}}", "default{state_entry(){float n=+1.0;}}",
          "integer f(integer n){return +n;} default{state_entry(){}}"})
        check(!compile_lsl(bad), "Compiler accepted an invalid user-function declaration/call: " + bad);
    // The unchanged YEngine declaration oracle explicitly accepts a function
    // and global with the same name, and separate nested-block declarations.
    const auto names = compile(R"LSL(
        integer same=3;
        integer result;
        integer same(){return 7;}
        integer scoped(integer n){integer x=n; {integer x=2;} return x;}
        default {state_entry(){result=same()+same+scoped(5);}}
    )LSL");
    LslInstance instance(names, context(), {});
    finish(instance, 1);
    const auto state = snapshot(instance);
    check(std::get<std::int32_t>(state.globals[0]) == 3 && std::get<std::int32_t>(state.globals[1]) == 15,
          "Separate variable/function namespaces or nested local shadowing changed");
}
} // namespace

int main() {
    try {
        typed_and_forward();
        recursion_and_order();
        sleep_and_checkpoint();
        overloads_initializers_and_budget();
        initializer_sleep_checkpoint();
        reset_and_states();
        cross_state_event_admission();
        lowered_stack_native_checkpoint();
        compiler_errors();
        std::cout << "PASS: typed user functions, recursion, ordering, resumable calls, checkpoints and lifecycle\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
