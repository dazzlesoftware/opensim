// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include <string>
#include <string_view>

namespace opensim::network {
enum class ScriptSmtpTls { Automatic, StartTls, Implicit };

struct ScriptSmtpSecurity {
    ScriptSmtpTls tls = ScriptSmtpTls::Automatic;
    bool verify_chain = true;
    bool verify_name = true;
    std::string login, password;
};

void send_script_smtp(const std::string& host, int port, std::string_view from, std::string_view to,
                      std::string_view subject, std::string_view body,
                      std::string_view owner, std::string_view object,
                      const ScriptSmtpSecurity& security);
}
