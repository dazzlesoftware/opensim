// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace opensim;
using namespace framework;
using namespace script;

namespace {
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}

LslContext caller() {
    LslContext context;
    context.object = Uuid("00112233-4455-4677-8899-aabbccddeeff");
    context.owner = Uuid("11223344-5566-4788-99aa-bbccddeeff00");
    context.item = Uuid("22334455-6677-4889-aabb-ccddeeff0011");
    context.object_name = "Child teleport caller";
    context.position = {1, 2, 3};
    context.root_position = Vector3{70, 80, 90};
    return context;
}

struct Harness {
    LslHost host;
    std::int64_t clock = 0;
    std::vector<std::string> events;
    std::vector<LslChat> chats, listeners;
    std::vector<int> delays;
    bool denied = false;
    int calls = 0;
    std::int32_t reply = 1;
    std::int32_t expected_flags = 7;

    Harness() {
        host.clock_ticks = [this] { return clock; };
        host.ossl_permission_error = [this](Uuid object, Uuid item, std::string_view function) {
            check(object == caller().object && item == caller().item && function == "osTeleportObject",
                  "Teleport permission check lost caller identity or function name");
            events.emplace_back("permission");
            return denied ? std::string("Denied teleport permission") : std::string{};
        };
        host.teleport_object = [this](Uuid source, Uuid target, Vector3 position, Quaternion rotation,
                                      std::int32_t flags) {
            events.emplace_back("callback");
            ++calls;
            check(source == caller().object, "Teleport callback received group identity instead of the host part");
            check(target == Uuid("33445566-7788-499a-bbcc-ddeeff001122"),
                  "Teleport callback received the wrong target");
            check(position == Vector3{1.25f, -2.5f, 30.75f}, "Teleport callback changed target position");
            check(rotation == Quaternion{.1f, .2f, .3f, .4f}, "Teleport callback normalized or changed raw rotation");
            check(flags == expected_flags, "Teleport callback changed the integer flags");
            return reply;
        };
        host.chat = [this](const LslChat &chat) {
            events.emplace_back("chat");
            chats.push_back(chat);
        };
        host.listener = [this](const LslChat &chat) {
            events.emplace_back("listener");
            listeners.push_back(chat);
        };
        host.request_sleep = [this](int delay) {
            events.emplace_back("sleep");
            delays.push_back(delay);
        };
    }
};

std::shared_ptr<const LslProgram> compile(const std::string &body) {
    auto result = compile_lsl("integer result=99; default {state_entry() {" + body + "}}");
    std::string errors;
    for (const auto &diagnostic : result.diagnostics)
        errors += diagnostic.message + "; ";
    check(bool(result), "Teleport fixture did not compile: " + errors);
    return result.program;
}

void finish(LslInstance &instance, Harness &host) {
    for (int i = 0; i < 32; ++i) {
        const auto result = instance.run(10000);
        check(result.status != LslRunStatus::Faulted, "Teleport script faulted: " + result.error);
        if (result.status == LslRunStatus::Idle)
            return;
        if (result.status == LslRunStatus::Sleeping)
            host.clock += 20000000;
    }
    throw std::runtime_error("Teleport script did not finish");
}

int result(LslInstance &instance) {
    const auto checkpoint = instance.checkpoint();
    check(checkpoint && checkpoint->globals.size() == 1, "Teleport script result checkpoint unavailable");
    return std::get<std::int32_t>(checkpoint->globals.front());
}

const std::string target = "\"33445566-7788-499a-bbcc-ddeeff001122\"";
const std::string arguments = ", <1.25,-2.5,30.75>, <0.1,0.2,0.3,0.4>, ";

void valid_calls() {
    const auto program = compile("result=osTeleportObject((key)" + target + arguments +
                                 "OSTPOBJ_NONE|OSTPOBJ_STOPATTARGET|OSTPOBJ_STOPONFAIL|OSTPOBJ_SETROT);");
    for (int code : {-4, -3, -2, -1, 0, 1}) {
        Harness host;
        host.reply = code;
        LslInstance instance(program, caller(), host.host);
        finish(instance, host);
        check(result(instance) == code && host.calls == 1,
              "Teleport callback return code did not reach LSL expression");
        check(host.events == std::vector<std::string>{"permission", "callback"},
              "Valid teleport changed permission/callback ordering or added a delay");
        check(host.chats.empty() && host.listeners.empty() && host.delays.empty(),
              "Valid teleport emitted an error or slept");
    }
    Harness negative;
    negative.expected_flags = -123;
    LslInstance negative_flags(compile("result=osTeleportObject(" + target + arguments + "-123);"), caller(),
                               negative.host);
    finish(negative_flags, negative);
    check(result(negative_flags) == 1, "Signed teleport flags did not reach callback");

    Harness no_callback;
    no_callback.host.teleport_object = {};
    LslInstance absent(program, caller(), no_callback.host);
    finish(absent, no_callback);
    check(result(absent) == -1 && no_callback.events == std::vector<std::string>{"permission"} &&
              no_callback.delays.empty(),
          "Unavailable teleport callback did not fail silently without sleep");

    Harness zero;
    zero.host.teleport_object = [&](Uuid source, Uuid object, Vector3, Quaternion, std::int32_t) {
        zero.events.emplace_back("callback");
        ++zero.calls;
        check(source == caller().object && object.is_zero(), "NULL_KEY did not remain a valid parsed target UUID");
        return std::int32_t{-1};
    };
    LslInstance zero_target(compile("result=osTeleportObject(NULL_KEY" + arguments + "0);"), caller(), zero.host);
    finish(zero_target, zero);
    check(result(zero_target) == -1 && zero.calls == 1 && zero.delays.empty() && zero.chats.empty(),
          "Valid zero UUID incorrectly used malformed-key error behavior");
}

void malformed_and_denied() {
    const auto invalid = compile("result=osTeleportObject(\"invalid-key\"" + arguments + "OSTPOBJ_NONE);");
    Harness host;
    LslInstance instance(invalid, caller(), host.host);
    finish(instance, host);
    check(result(instance) == -1 && host.calls == 0, "Malformed teleport key reached callback or returned wrong value");
    check(host.events == std::vector<std::string>{"permission", "chat", "listener", "sleep"} &&
              host.delays == std::vector<int>{1000},
          "Malformed key did not preserve shout/listener/sleep ordering");
    check(host.chats.size() == 1 && host.listeners.size() == 1, "Malformed key did not emit one shout to both sinks");
    for (const auto &chat : {host.chats.front(), host.listeners.front()})
        check(chat.type == LslChatType::Shout && chat.channel == std::numeric_limits<std::int32_t>::max() &&
                  chat.text == "osTeleportObject() invalid object Key" && chat.use_root_position &&
                  chat.context.object == caller().object && chat.context.object_name == caller().object_name &&
                  chat.context.root_position == caller().root_position,
              "Malformed key error lost original shout type, root position or child identity");

    Harness denied;
    denied.denied = true;
    LslInstance rejected(invalid, caller(), denied.host);
    const auto outcome = rejected.run(10000);
    check(outcome.status == LslRunStatus::Faulted &&
              outcome.error.find("Denied teleport permission") != std::string::npos,
          "Denied teleport did not fault at the OSSL permission boundary");
    check(denied.calls == 0 && denied.delays.empty() && denied.listeners.empty() && !denied.events.empty() &&
              denied.events.front() == "permission",
          "Permission denial did not precede key parsing, callback and invalid-key sleep");
    for (const auto &chat : denied.chats)
        check(chat.text.find("invalid object Key") == std::string::npos && chat.type != LslChatType::Shout,
              "Denied malformed key emitted the parse-error shout");
}

void compiler_surface() {
    // User-defined functions are a separate missing compiler feature. Exercise
    // the supported expression contexts without masking that full-parity gap.
    const auto expression = compile_lsl(
        "default {state_entry() {integer code=osTeleportObject(NULL_KEY,<1,2,3>,ZERO_ROTATION,OSTPOBJ_NONE); "
        "if(osTeleportObject(NULL_KEY,<0,0,0>,ZERO_ROTATION,0)==-1)"
        "code=1; osTeleportObject(NULL_KEY,<0,0,0>,ZERO_ROTATION,0);}}");
    std::string expression_diagnostics;
    for (const auto &diagnostic : expression.diagnostics)
        expression_diagnostics += "\n(" + std::to_string(diagnostic.line) + "," + std::to_string(diagnostic.column) +
                                  "): " + diagnostic.message;
    check(bool(expression),
          "Integer teleport call did not compile in assignments, conditions and statements" + expression_diagnostics);
    for (const std::string &bad :
         {"osTeleportObject(NULL_KEY,<0,0,0>,ZERO_ROTATION)", "osTeleportObject(NULL_KEY,<0,0,0>,ZERO_ROTATION,0,1)",
          "osTeleportObject(1,<0,0,0>,ZERO_ROTATION,0)", "osTeleportObject(NULL_KEY,1,ZERO_ROTATION,0)",
          "osTeleportObject(NULL_KEY,<0,0,0>,1,0)", "osTeleportObject(NULL_KEY,<0,0,0>,ZERO_ROTATION,1.5)",
          "llTeleportObject(NULL_KEY,<0,0,0>,ZERO_ROTATION,0)"})
        check(!compile_lsl("default {state_entry() {" + bad + ";}}"),
              "Compiler accepted unsupported teleport signature: " + bad);
}
} // namespace

int main() {
    try {
        compiler_surface();
        valid_calls();
        malformed_and_denied();
        std::cout << "PASS: original OSSL object teleport signature, constants, dispatch, errors and permissions\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
