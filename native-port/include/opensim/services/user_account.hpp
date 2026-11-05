// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/agent_circuit.hpp"
namespace opensim::services {
using AccountValues = framework::OrderedDictionary<std::string, std::any>;
bool parse_account_integer(std::string_view text, std::int32_t& value);
std::int32_t account_integer(std::string_view text);
struct UserAccount {
    using Clock = std::function<std::int32_t()>;
    std::string principal_id = "00000000-0000-0000-0000-000000000000",
                scope_id = "00000000-0000-0000-0000-000000000000";
    std::optional<std::string> first_name, last_name, email, user_title, user_country;
    int user_level = 0, user_flags = 0, created = 0;
    bool local_to_grid = true;
    std::shared_ptr<framework::AgentCircuitData::ServiceUrls> service_urls = nullptr;
    UserAccount() = default;
    explicit UserAccount(const framework::Uuid& principal) : principal_id(principal.str()) {}
    UserAccount(const framework::Uuid& scope, const framework::Uuid& principal,
                std::optional<std::string> first, std::optional<std::string> last,
                std::optional<std::string> mail, Clock clock = {});
    UserAccount(const framework::Uuid& scope, std::optional<std::string> first,
                std::optional<std::string> last, std::optional<std::string> mail, Clock clock = {});
    UserAccount(std::string principal, std::string scope, std::optional<std::string> first,
                std::optional<std::string> last, int level, int flags)
        : principal_id(std::move(principal)), scope_id(std::move(scope)), first_name(std::move(first)),
          last_name(std::move(last)), user_level(level), user_flags(flags) {}
    explicit UserAccount(const AccountValues& values);
    std::string name() const {
        return first_name.value_or("") + " " + last_name.value_or("");
    }
    std::shared_ptr<AccountValues> to_key_value_pairs() const;
};
}
