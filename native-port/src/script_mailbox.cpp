// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/script_mailbox.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <algorithm>

namespace opensim::scene {
ScriptMailbox::ScriptMailbox(int owner_per_hour, int recipient_per_hour,
                             int smtp_per_day, int smtp_recipient_per_hour)
    : owner_per_hour_(owner_per_hour), recipient_per_hour_(recipient_per_hour),
      smtp_per_day_(smtp_per_day), smtp_recipient_per_hour_(smtp_recipient_per_hour),
      smtp_daily_{double(smtp_per_day), Clock::now()} {}

bool ScriptMailbox::consume(Throttle& throttle, int limit, Clock::time_point now) {
    const auto elapsed = std::chrono::duration<double>(now - throttle.last).count();
    throttle.remaining += elapsed * (limit / 3600.0);
    throttle.last = now;
    if (throttle.remaining > limit)
        throttle.remaining = limit;
    else if (throttle.remaining <= 0)
        return false;
    --throttle.remaining;
    return true;
}

bool ScriptMailbox::allow_owner(framework::Uuid owner, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    const auto [entry, inserted] = owners_.try_emplace(owner, Throttle{double(owner_per_hour_), now});
    return inserted || consume(entry->second, owner_per_hour_, now);
}

bool ScriptMailbox::allow_recipient(std::string address, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    const auto [entry, inserted] = recipients_.try_emplace(
        std::move(address), Throttle{double(recipient_per_hour_), now});
    return inserted || consume(entry->second, recipient_per_hour_, now);
}

bool ScriptMailbox::allow_smtp(std::string address, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    // The source subtracts elapsed time from the daily SMTP allowance.
    const auto elapsed = std::chrono::duration<double>(now - smtp_daily_.last).count();
    smtp_daily_.remaining -= elapsed * (smtp_per_day_ / 86400.0);
    smtp_daily_.last = now;
    if (smtp_daily_.remaining > smtp_per_day_)
        smtp_daily_.remaining = smtp_per_day_;
    else if (smtp_daily_.remaining <= 0)
        return false;
    --smtp_daily_.remaining;

    const auto [entry, inserted] = smtp_recipients_.try_emplace(
        std::move(address), Throttle{double(smtp_recipient_per_hour_), now});
    return inserted || consume(entry->second, smtp_recipient_per_hour_, now);
}

void ScriptMailbox::add(framework::Uuid part) {
    std::lock_guard lock(mutex_);
    queues_.try_emplace(part);
}

void ScriptMailbox::remove(framework::Uuid part) {
    std::lock_guard lock(mutex_);
    expires_.erase(part);
    queues_.erase(part);
}

void ScriptMailbox::insert(framework::Uuid part, ScriptEmail message, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    const auto queue = queues_.find(part);
    if (queue == queues_.end() || queue->second.size() >= 50)
        return;
    queue->second.push_back(std::move(message));
    expires_[part] = now + timeout;
}

std::optional<ScriptEmail> ScriptMailbox::next(framework::Uuid part, std::string_view sender,
                                               std::string_view subject, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    const auto expire_throttles = [&](auto& entries, auto& next_expiry) {
        if (!entries.empty() && now > next_expiry) {
            std::erase_if(entries, [&](const auto& entry) { return entry.second.last < now - throttle_window; });
            next_expiry = now + throttle_window;
        }
    };
    expire_throttles(owners_, next_owner_expiry_);
    expire_throttles(recipients_, next_recipient_expiry_);
    expire_throttles(smtp_recipients_, next_smtp_recipient_expiry_);
    expires_[part] = now + timeout;
    if (expires_.size() > 1 && now > next_expiry_) {
        std::erase_if(expires_, [&](const auto& expiry) {
            if (expiry.second >= now)
                return false;
            queues_[expiry.first].clear();
            return true;
        });
        next_expiry_ = now + timeout;
    }
    const auto queue = queues_.find(part);
    if (queue == queues_.end())
        return std::nullopt;
    auto& messages = queue->second;
    const auto found = std::ranges::find_if(messages, [&](const ScriptEmail& message) {
        // The original condition ignores the subject when no sender is supplied.
        return sender.empty() ||
            (framework::invariant_equals_ignore_case(sender, message.sender) &&
             (subject.empty() || framework::invariant_equals_ignore_case(subject, message.subject)));
    });
    if (found == messages.end())
        return std::nullopt;
    auto message = std::move(*found);
    messages.erase(found);
    message.remaining = static_cast<std::int32_t>(messages.size());
    if (messages.empty())
        expires_.erase(part);
    return message;
}
}
