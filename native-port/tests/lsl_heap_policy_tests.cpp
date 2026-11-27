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

LslCheckpoint checkpoint(const LslInstance &instance) {
    const auto saved = instance.checkpoint();
    check(saved.has_value(), "Heap policy checkpoint unavailable");
    return *saved;
}

void policy_changes_and_receiving_limits() {
    const auto compiled = compile_lsl(R"(YOption allowall;
integer used_before; integer left_after; object nil;
default { state_entry() {
    object local = "12345678";
    used_before = xmrHeapUsed();
    llSleep(0.1);
    left_after = xmrHeapLeft();
    local = nil;
} })");
    check(bool(compiled), "Heap policy source control rejected");
    std::int64_t clock = 100000000;
    LslHost host;
    host.heap_limit_bytes = 64;
    host.clock_ticks = host.monotonic_ticks = [&] { return clock; };
    LslInstance producer(compiled.program, {}, host);
    check(producer.run(10000).status == LslRunStatus::Sleeping, "Heap policy control did not suspend");
    const auto saved = checkpoint(producer);
    check(saved.globals.size() == 3 && std::get<std::int32_t>(saved.globals[0]) == 24 &&
              producer.heap_bytes_used() == 24,
          "Heap policy control did not retain its charged local Object");
    const auto decoded = decode_lsl_checkpoint(encode_lsl_checkpoint(saved));
    check(decoded.has_value(), "Heap policy checkpoint codec rejected");

    producer.set_heap_limit_bytes(12);
    check(producer.heap_bytes_used() == 24, "Changing the host heap limit rewrote retained script usage");
    clock += saved.sleep_remaining + 1;
    const auto recovered = producer.run(10000);
    check(recovered.status == LslRunStatus::Idle && recovered.error.empty(),
          "Over-budget local could not release storage after host policy change");
    check(std::get<std::int32_t>(checkpoint(producer).globals[1]) == -12 && producer.heap_bytes_used() == 8,
          "HeapLeft did not reflect the lowered host policy before local storage was released");

    LslHost constrained = host;
    constrained.heap_limit_bytes = 20;
    LslInstance rejected(compiled.program, {}, constrained);
    check(!rejected.restore_checkpoint(*decoded), "Native checkpoint ignored the receiving host heap limit");

    LslHost receiving = host;
    receiving.heap_limit_bytes = 32;
    LslInstance accepted(compiled.program, {}, receiving);
    check(accepted.restore_checkpoint(*decoded), "Native checkpoint rejected a sufficient receiving host heap limit");
    clock += checkpoint(accepted).sleep_remaining + 1;
    const auto result = accepted.run(10000);
    check(result.status == LslRunStatus::Idle && result.error.empty(), "Receiving-host heap policy resume failed");
    const auto output = checkpoint(accepted);
    check(std::get<std::int32_t>(output.globals[0]) == 24 && std::get<std::int32_t>(output.globals[1]) == 8,
          "Checkpoint restored the producer's heap policy instead of using the receiving host policy");
}
} // namespace

int main() {
    try {
        policy_changes_and_receiving_limits();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
