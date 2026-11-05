// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_control.hpp"
#include <iostream>
using namespace opensim;
using namespace scene;
namespace {
framework::Uuid id(int n) {
    return framework::Uuid(n == 1 ? "11223344-5566-7788-99aa-000000000001"
                                  : "11223344-5566-7788-99aa-000000000002");
}
std::string probe(int command, int mode, int fail) {
    std::string trace;
    bool run = false, itemrun = false;
    auto step = [&](int point, const std::string& value) {
        trace += "|" + value;
        if (point == fail)
            throw std::runtime_error("injected");
    };
    ScriptControlBackend backend;
    backend.can_edit = [&](auto i, auto o, auto a) {
        step(1, "EDIT:" + i.str() + ":" + o.str() + ":" + a.str());
        return !(mode & 1);
    };
    backend.part_local_id = [&](auto o) -> std::optional<std::uint32_t> {
        step(2, "PART:" + o.str());
        if (mode & 2)
            return {};
        return 123;
    };
    backend.can_reset = [&](auto o, auto i, auto a) {
        step(3, "RESETCHECK:" + o.str() + ":" + i.str() + ":" + a.str());
        return !(mode & 4);
    };
    ScriptControlController* controller = nullptr;
    backend.start = [&](auto local, auto item) {
        step(4, "START:" + std::to_string(local) + ":" + item.str());
        controller->on_start(local, item);
    };
    backend.stop = [&](auto local, auto item) {
        step(5, "STOP:" + std::to_string(local) + ":" + item.str());
        controller->on_stop(local, item);
    };
    backend.reset_event = [&](auto local, auto item) {
        step(6, "RESET:" + std::to_string(local) + ":" + item.str());
        controller->on_reset(local, item);
    };
    backend.instance = [&](auto item) -> std::optional<ScriptControlInstance> {
        step(7, "FIND:" + item.str());
        if (mode & 8)
            return {};
        ScriptControlInstance instance;
        instance.set_running = [&](bool value) {
            step(8, std::string("RUN:") + (value ? "1" : "0"));
            run = value;
        };
        if (!(mode & 16))
            instance.set_item_running = [&](bool value) {
                step(9, std::string("ITEM:") + (value ? "1" : "0"));
                itemrun = value;
            };
        instance.reset = [&] { step(13, "INSTANCE_RESET"); };
        return instance;
    };
    backend.trace_reset = [&](auto local, auto item) {
        step(10, "TRACE:" + std::to_string(local) + ":" + item.str());
    };
    backend.release_urls = [&](auto item) {
        step(11, "URL_MODULE");
        if (!(mode & 32))
            step(12, "URL_REMOVE:" + item.str());
    };
    ScriptControlController control(std::move(backend));
    controller = &control;
    std::string result = "OK";
    try {
        switch (command) {
        case 0:
            control.set_running(id(1), id(2), id(1), false);
            break;
        case 1:
            control.set_running(id(1), id(2), id(1), true);
            break;
        case 2:
            control.process_reset(id(1), id(2), id(1));
            break;
        case 3:
            control.on_start(999, id(1));
            break;
        case 4:
            control.on_stop(999, id(1));
            break;
        case 5:
            control.on_reset(999, id(1));
            break;
        case 6:
            control.reset_script(id(1));
            break;
        }
    } catch (...) {
        result = "THROW";
    }
    return result + ":" + (run ? "1" : "0") + ":" + (itemrun ? "1" : "0") + trace;
}
std::string execution(int state, int run, int active, int suspend, int count, int fail) {
    ScriptExecutionControl control;
    control.state = static_cast<ScriptExecutionState>(state);
    control.active_event = bool(active);
    control.suspend_count = suspend;
    control.pending_events = static_cast<std::size_t>(count);
    std::string trace;
    auto step = [&](int point, const char* s) {
        trace += std::string("|") + s;
        if (point == fail)
            throw std::runtime_error("injected");
    };
    ScriptExecutionQueues queues{[&] { step(1, "YIELD"); }, [&] { step(2, "START"); },
                                 [&] { step(3, "REMOVE_SLEEP"); }, [&] { step(4, "EMPTY"); }};
    std::string result = "OK";
    try {
        script_set_running(control, bool(run), queues);
    } catch (...) {
        result = "THROW";
    }
    return result + ":" + std::to_string(static_cast<int>(control.state)) + ":" +
           (control.running ? "1" : "0") + ":" + std::to_string(control.pending_events) + trace;
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            int kind;
            while (std::cin >> kind)
                if (kind == 2) {
                    int s, r, k, h, e, n;
                    std::cin >> s >> r >> k >> h >> e >> n;
                    ScriptExecutionControl control;
                    control.state = static_cast<ScriptExecutionState>(s);
                    control.running = bool(r);
                    control.pending_events = n;
                    std::cout << static_cast<int>(stopped_script_event(control, bool(k), bool(h), bool(e)))
                              << '\n';
                } else if (!kind) {
                    int c, m, f;
                    std::cin >> c >> m >> f;
                    std::cout << probe(c, m, f) << '\n';
                } else {
                    int s, r, a, u, n, f;
                    std::cin >> s >> r >> a >> u >> n >> f;
                    std::cout << execution(s, r, a, u, n, f) << '\n';
                }
            return 0;
        }
        int checks = 0;
        auto check = [&](bool v, const char* s) {
            ++checks;
            if (!v)
                throw std::runtime_error(s);
        };
        check(probe(1, 1, 0).find("PART:") == std::string::npos, "edit beforepart");
        check(probe(2, 2, 0).find("RESETCHECK:") == std::string::npos, "part before resetcheck");
        check(probe(1, 16, 0).starts_with("OK:1:0"), "null metadata");
        check(probe(5, 8, 0).find("URL_MODULE") == std::string::npos, "missing resetinstance");
        check(execution(8, 1, 1, 0, 0, 0) == "OK:6:1:0|YIELD", "restart active event");
        check(execution(4, 0, 1, 0, 3, 0) == "OK:8:0:0|REMOVE_SLEEP|EMPTY", "stop sleeping clearsqueue");
        ScriptExecutionControl c;
        c.state = ScriptExecutionState::Suspended;
        c.running = false;
        check(stopped_script_event(c, true, true, true) == StoppedScriptEvent::QueueStateEntry,
              "stopped resetstateentry");
        check(stopped_script_event(c, true, true, false) == StoppedScriptEvent::Ignore,
              "stopped touchignored");
        c.pending_events = 1;
        check(stopped_script_event(c, true, true, true) == StoppedScriptEvent::Ignore,
              "one stoppedstateentry");
        check(stopped_script_event(c, false, true, true) == StoppedScriptEvent::Reject, "unknown rejected");
        std::cout << "PASS: " << checks << " script control checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
