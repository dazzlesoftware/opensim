// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/lsl_runtime.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim::script;

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        const auto program = compile_lsl(R"lsl(
            integer value;
            default {
                moving_start() { value = value * 10 + 1; }
                moving_end() { value = value * 10 + 2; }
            }
        )lsl");
        check(bool(program), "Moving handlers did not compile");
        LslInstance instance(program.program, {}, {});
        check(instance.event_mask() == ((1ull << 17) | (1ull << 18)), "Moving event mask differs from original enum");
        check(instance.run(1000).status == LslRunStatus::Idle, "Initial moving script faulted");
        check(!instance.post_event({"moving_start", {std::int32_t(1)}, {}}), "Moving start accepted an argument");
        check(!instance.post_event({"moving_end", {std::int32_t(1)}, {}}), "Moving end accepted an argument");
        check(instance.post_event({"moving_start", {}, {}}) && instance.post_event({"moving_end", {}, {}}) &&
                  instance.post_event({"moving_start", {}, {}}),
              "Moving event queue rejected empty arguments");
        const auto before = instance.checkpoint();
        check(before && before->queued_events.size() == 3, "Moving event queue lost duplicates");
        const auto bytes = encode_lsl_checkpoint(*before);
        const auto decoded = decode_lsl_checkpoint(bytes);
        check(decoded && decoded->queued_events.size() == 3 && decoded->queued_events[0].name == "moving_start" &&
                  decoded->queued_events[1].name == "moving_end" && decoded->queued_events[2].name == "moving_start",
              "Moving event checkpoint did not preserve order");
        LslInstance restored(program.program, {}, {});
        check(restored.restore_checkpoint(*decoded), "Moving event checkpoint restore failed");
        check(restored.run(1000).status == LslRunStatus::Idle, "Restored moving script faulted");
        check(std::get<std::int32_t>(restored.checkpoint()->globals.front()) == 121,
              "Moving handlers executed out of order");
        check(instance.run(1000).status == LslRunStatus::Idle &&
                  std::get<std::int32_t>(instance.checkpoint()->globals.front()) == 121,
              "Original moving queue changed during checkpoint");
        for (const auto &source :
             {"default { moving_start(integer value) {} }", "default { moving_end(integer value) {} }"})
            check(!compile_lsl(source), "Moving handler with parameters compiled");
        std::cout << "PASS: moving handlers, masks, argument rejection, FIFO and checkpoint restore\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
