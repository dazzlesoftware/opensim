// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/installed_fonts.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <iostream>
#include <stdexcept>

using namespace opensim;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

void check_names(const script::LslValue &value, const std::vector<std::string> &names) {
    const auto *list = std::get_if<std::shared_ptr<const script::LslList>>(&value);
    check(list && *list && (*list)->items.size() == names.size(), "Native script font count differs from provider");
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto *name = std::get_if<script::LslBoxedString>(&(*list)->items[i]);
        check(name && name->value == names[i], "Native script font order, name, or LSLString boxing differs");
    }
}

std::string json(const std::string &text) {
    return network::osd_json(network::Value(text), true);
}
} // namespace

int main() {
    try {
        const auto language = framework::installed_font_ui_language();
        const auto names = framework::installed_font_families(language);
        constexpr auto source = R"LSL(integer calls;list names;integer done;
default{state_entry(){calls++;names=xmrFontsAvailable();done=1;}})LSL";
        const auto compiled = script::compile_lsl(source);
        check(bool(compiled.program), "Actual xmrFontsAvailable source did not compile");
        script::LslContext context;
        context.object = framework::Uuid::random();
        context.item = framework::Uuid::random();
        script::LslHost host; // Deliberately no font callback: exercise production fallback.
        script::LslInstance instance(compiled.program, context, host);
        std::optional<script::LslCheckpoint> suspended;
        for (unsigned step = 0; step < 1000; ++step) {
            const auto result = instance.run(1);
            check(result.status != script::LslRunStatus::Faulted, "Native default font API faulted: " + result.error);
            const auto checkpoint = instance.checkpoint();
            check(checkpoint.has_value(), "Native font source could not checkpoint");
            if (checkpoint->active_event && !checkpoint->global_initializer_cursor && checkpoint->instruction == 2) {
                check(checkpoint->globals.size() == 3 && std::get<std::int32_t>(checkpoint->globals[0]) == 1 &&
                          std::get<std::int32_t>(checkpoint->globals[2]) == 0,
                      "Native checkpoint did not suspend after the completed font API");
                check_names(checkpoint->globals[1], names);
                suspended = checkpoint;
                break;
            }
        }
        check(suspended.has_value(), "Native source did not reach its post-enumeration boundary");
        const auto decoded = script::decode_lsl_checkpoint(script::encode_lsl_checkpoint(*suspended));
        check(decoded.has_value(), "Native font checkpoint codec failed");
        auto resumed_host = host;
        unsigned replay_calls = 0;
        resumed_host.fonts_available = [&]() -> std::vector<std::string> {
            ++replay_calls;
            throw std::runtime_error("Completed font API replayed after native restore");
        };
        script::LslInstance resumed(compiled.program, context, resumed_host);
        check(resumed.restore_checkpoint(*decoded), "Fresh native VM rejected its font checkpoint");
        const auto result = resumed.run(100);
        check(result.status == script::LslRunStatus::Idle,
              "Fresh native font source failed to finish: " + result.error);
        const auto completed = resumed.checkpoint();
        check(completed && completed->globals.size() == 3 && std::get<std::int32_t>(completed->globals[0]) == 1 &&
                  std::get<std::int32_t>(completed->globals[2]) == 1 && replay_calls == 0,
              "Fresh native completion repeated enumeration or script prefix");
        check_names(completed->globals[1], names);
        std::cout
            << "{\"success\":true,\"evidence_scope\":\"native current environment smoke; not an original same-image "
               "parity claim\",\"default_ui_language\":"
            << language
            << ",\"source_default_provider\":true,\"boxed_strings\":true,\"checkpoint_no_replay\":true,"
               "\"count\":"
            << names.size() << ",\"names\":[";
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (i)
                std::cout << ',';
            std::cout << json(names[i]);
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cout << "{\"success\":false,\"error\":" << json(error.what()) << "}\n";
        return 1;
    }
}
