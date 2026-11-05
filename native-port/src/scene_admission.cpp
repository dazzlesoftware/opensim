// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/scene_admission.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::services {
namespace {
AdmissionCaps& caps(const std::shared_ptr<AdmissionCaps>& value) {
    if (!value)
        throw std::invalid_argument("Null caps module");
    return *value;
}
bool viewer_matches(const std::optional<std::string>& entry, const std::string& lowered,
                    std::size_t original_length) {
    if (!entry)
        throw std::invalid_argument("Null viewer entry");
    auto e = framework::utf16_replacing(*entry), v = framework::utf16_replacing(lowered);
    auto length = std::min(e.size(), original_length);
    if (length > v.size())
        throw std::out_of_range("Viewer prefix length");
    return e == v.substr(0, length);
}
}
bool SceneAdmission::verify_user_presence(const std::shared_ptr<framework::AgentCircuitData>& circuit,
                                          std::string& reason) {
    const auto names = [&]() {
        if (!circuit)
            throw std::invalid_argument("Null agent circuit");
        return circuit->first_name.value_or("") + " " + circuit->last_name.value_or("");
    };
    if (!backend_.has_presence_service()) {
        reason = "Failed to verify user presence in the grid for " + names() + " in region " +
                 backend_.region().name.value_or("") + ". Presence service does not exist.";
        return false;
    }
    if (!circuit)
        throw std::invalid_argument("Null agent circuit");
    auto presence = backend_.grid_presence(circuit->session_id);
    if (!presence || presence->user_id != circuit->agent_id.str()) {
        reason = "Failed to verify user presence in the grid for " + names() + ", access denied to region " +
                 backend_.region().name.value_or("") + ".";
        return false;
    }
    reason.clear();
    return true;
}
bool SceneAdmission::new_user_connection(std::shared_ptr<framework::AgentCircuitData> circuit,
                                         std::uint32_t flags, const GridRegion* source, std::string& reason,
                                         bool require_presence) {
    const bool via_login = (flags & (128u | 1073741824u)) != 0;
    reason.clear();
    auto viewer = backend_.viewer_name(circuit);
    if (!circuit)
        throw std::invalid_argument("Null agent circuit");
    if (source && source->raw_server_uri)
        (void)source->server_uri();
    backend_.log_incoming(circuit, flags, source, viewer);
    if (!backend_.logins_enabled()) {
        reason = "Logins to this region are disabled";
        return false;
    }
    bool denied = true;
    std::optional<std::string> lower;
    auto allowed = backend_.allowed_viewers();
    const auto viewer_length = framework::utf16_replacing(viewer).size();
    if (!allowed.empty()) {
        lower = backend_.lower_current_culture(framework::trim_utf8(viewer));
        for (const auto& entry : allowed)
            if (viewer_matches(entry, *lower, viewer_length)) {
                denied = false;
                break;
            }
    } else
        denied = false;
    auto banned = backend_.banned_viewers();
    if (!banned.empty()) {
        if (!lower)
            lower = backend_.lower_current_culture(framework::trim_utf8(viewer));
        for (const auto& entry : banned)
            if (viewer_matches(entry, *lower, viewer_length)) {
                denied = true;
                break;
            }
    }
    if (denied) {
        reason = "Access denied, your viewer is banned";
        return false;
    }
    std::shared_ptr<AdmissionPresence> presence;
    {
        std::lock_guard lock(backend_.remove_client_mutex());
        presence = backend_.presence(circuit->agent_id);
        if (presence && !presence->deleted() && presence->child() &&
            (presence->state() == AdmissionPresenceState::running ||
             presence->state() == AdmissionPresenceState::pre_remove)) {
            presence->state(AdmissionPresenceState::running);
            if (backend_.in_transit(presence->agent_id()))
                presence->do_not_close_after_teleport(true);
        }
    }
    if (presence && presence->state() == AdmissionPresenceState::removing) {
        int polls_left = 10;
        try {
            while (!presence->deleted() && presence->state() == AdmissionPresenceState::removing &&
                   polls_left-- > 0)
                backend_.sleep(1000);
            if (!presence->deleted() && presence->state() == AdmissionPresenceState::removing)
                return false;
        } catch (...) {
            presence.reset();
        }
    }
    if (flags != 0) {
        if (circuit->start_position.x < 0)
            circuit->start_position.x = 1;
        else if (circuit->start_position.x >= static_cast<float>(backend_.region().size_x))
            circuit->start_position.x = static_cast<float>(backend_.region().size_x) - 1;
        if (circuit->start_position.y < 0)
            circuit->start_position.y = 1;
        else if (circuit->start_position.y >= static_cast<float>(backend_.region().size_y))
            circuit->start_position.y = static_cast<float>(backend_.region().size_y) - 1;
        bool check_hub = (flags & (32u | 4u)) == 0 &&
                         (via_login || (!backend_.telehub_allow_landmarks() && (flags & (8u | 16u)) != 0));
        if (!backend_.check_circuit_land_access(*circuit, check_hub, reason)) {
            backend_.remove_circuit(circuit);
            return false;
        }
    }
    {
        std::lock_guard lock(backend_.new_user_mutex());
        if (presence && presence->deleted())
            presence.reset();
        if (presence && !presence->child() && !backend_.in_transit(presence->agent_id())) {
            if (presence->has_client())
                backend_.close_agent(presence->agent_id(), true);
            presence.reset();
        }
        circuit->teleport_flags = flags;
        if (via_login)
            backend_.remove_cached_account(circuit->agent_id);
        backend_.add_circuit(circuit);
        if (!presence) {
            if (require_presence)
                try {
                    if (!verify_user_presence(circuit, reason)) {
                        backend_.remove_circuit(circuit);
                        return false;
                    }
                } catch (const std::exception&) {
                    backend_.remove_circuit(circuit);
                    return false;
                }
            try {
                if (!backend_.authorize_user(circuit, !via_login && backend_.see_into_region(), reason)) {
                    backend_.remove_circuit(circuit);
                    return false;
                }
            } catch (const std::exception&) {
                backend_.remove_circuit(circuit);
                return false;
            }
            if (backend_.caps()) {
                auto module = backend_.caps();
                caps(module).set_agent_caps_seeds(circuit);
                module = backend_.caps();
                caps(module).create_caps(circuit->agent_id, circuit->circuit_code);
            }
        } else {
            presence->teleport_flags(flags);
            if (presence->child()) {
                if (backend_.caps()) {
                    auto module = backend_.caps();
                    caps(module).set_agent_caps_seeds(circuit);
                    module = backend_.caps();
                    caps(module).create_caps(circuit->agent_id, circuit->circuit_code);
                }
                presence->adjust_known_seeds();
            }
        }
        backend_.cache_user_name(circuit);
    }
    if (auto module = backend_.caps())
        module->activate_caps(circuit->circuit_code);
    return true;
}
}
