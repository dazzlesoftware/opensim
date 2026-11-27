// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/ossl_aes.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include <iostream>
#include <stdexcept>

using namespace opensim::script;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

int main() {
    try {
        const auto encoded = ossl_aes_encrypt("secret", "héllo world", "fixed IV");
        check(encoded == ossl_aes_encrypt("secret", "héllo world", "fixed IV"),
              "Custom IV encryption is deterministic");
        check(ossl_aes_decrypt("secret", encoded) == "héllo world", "Embedded IV decrypts fixed-IV ciphertext");
        check(ossl_aes_decrypt("secret", "ignored:" + encoded.substr(33), "fixed IV") == "héllo world",
              "External IV ignores the encoded IV prefix");
        const auto random = ossl_aes_encrypt("secret", "random payload");
        check(random != ossl_aes_encrypt("secret", "random payload") &&
                  ossl_aes_decrypt("secret", random) == "random payload",
              "Random IV roundtrip");
        check(ossl_aes_decrypt("secret", "not hex") == "" && ossl_aes_decrypt("secret", "gg:00") == "",
              "Malformed hex returns empty before AES construction");
        const auto program = compile_lsl(R"LSL(default { state_entry() {
            string a = osAESEncryptTo("secret", "payload", "iv");
            llSay(1, osAESDecrypt("secret", a));
            llSay(1, osAESDecryptFrom("secret", a, "iv"));
            llSay(1, osAESDecrypt("secret", osAESEncrypt("secret", "random")));
        } })LSL");
        check(bool(program), "All four AES functions must compile");
        LslHost host;
        std::vector<std::string> output;
        host.ossl_permission_error = [](auto, auto, auto) { return "denied"; };
        host.chat = [&](const LslChat &chat) { output.push_back(chat.text); };
        LslInstance instance(program.program, {}, host);
        check(instance.run(1000).status == LslRunStatus::Idle &&
                  output == std::vector<std::string>{"payload", "payload", "random"},
              "AES execution matches always-active source wrappers");
        const auto malformed =
            compile_lsl("default{state_entry(){string x=osAESDecrypt(\"s\",\"invalid\");llSay(1,\"continued\");}}");
        check(bool(malformed), "Malformed AES input still compiles");
        std::optional<LslChat> shout, listener;
        std::vector<int> sleeps;
        std::int64_t ticks = 0;
        int warnings = 0;
        bool continued = false;
        host.clock_ticks = [&] { return ticks; };
        host.chat = [&](const LslChat &chat) {
            if (chat.text == "continued")
                continued = true;
            else {
                shout = chat;
                ++warnings;
            }
        };
        host.listener = [&](const LslChat &chat) { listener = chat; };
        host.request_sleep = [&](int milliseconds) { sleeps.push_back(milliseconds); };
        LslInstance warning(malformed.program, {}, host);
        check(warning.run(100).status == LslRunStatus::Sleeping && shout && listener &&
                  shout->type == LslChatType::Shout && shout->use_root_position &&
                  shout->text == "osAESDecrypt: Failed to Decrypt!" && sleeps == std::vector<int>{1000},
              "Empty decryption result shouts through both paths and sleeps");
        ticks = 10000000;
        check(warning.run(100).status == LslRunStatus::Idle && continued && warnings == 1,
              "AES diagnostic resumes once without repeating crypto or the warning");
        const auto empty = compile_lsl("default{state_entry(){llSay(1,osAESDecryptFrom(\"s\",\"invalid\",\"\"));}}");
        output.clear();
        sleeps.clear();
        host.chat = [&](const LslChat &chat) { output.push_back(chat.text); };
        LslInstance no_iv(empty.program, {}, host);
        check(no_iv.run(100).status == LslRunStatus::Idle && output == std::vector<std::string>{""} && sleeps.empty(),
              "Empty required arguments bypass crypto without diagnostic or sleep");
        const auto bad_padding = compile_lsl(
            "default{state_entry(){string x=osAESDecrypt(\"s\",\"00000000000000000000000000000000:00\");}}");
        LslInstance fault(bad_padding.program, {}, host);
        check(fault.run(100).status == LslRunStatus::Faulted, "Invalid ciphertext must fault rather than return empty");
        std::cout << "PASS: native OSSL AES crypto, dispatch, diagnostics and sleep\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
