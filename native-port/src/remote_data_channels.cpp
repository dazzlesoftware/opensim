#include "opensim/scene/remote_data_channels.hpp"
#include <algorithm>
#include <iterator>
#include <stdexcept>

namespace opensim::scene {
RemoteDataChannels::RemoteDataChannels(Sender sender) : sender_(std::move(sender)) {}

RemoteDataChannels::~RemoteDataChannels() {
    for (auto& worker : workers_)
        worker.thread.request_stop();
    for (auto& worker : workers_)
        worker.thread.join();
}

framework::Uuid RemoteDataChannels::send(framework::Uuid item, std::string channel,
                                        std::string destination, Data data, framework::Uuid region) {
    const auto message = framework::Uuid::random();
    std::lock_guard lock(mutex_);
    workers_.remove_if([](const auto& worker) { return worker.finished; });
    auto& worker = workers_.emplace_back();
    worker.item = item;
    worker.region = region;
    try {
        worker.thread = std::jthread(
            [this, &worker, completion = Completion{item, region, message, std::move(channel), std::move(data)},
             destination = std::move(destination)](std::stop_token stop) mutable {
                try {
                    if (!stop.stop_requested())
                        sender_(completion.channel, destination, completion.data, stop);
                } catch (const std::exception& error) {
                    completion.data.text = error.what();
                }
                std::lock_guard lock(mutex_);
                if (!stop.stop_requested())
                    completed_.push_back(std::move(completion));
                worker.finished = true;
            });
    } catch (...) {
        workers_.pop_back();
        throw;
    }
    return message;
}

void RemoteDataChannels::cancel_script(framework::Uuid item, framework::Uuid region) {
    cancel(region, item);
}

void RemoteDataChannels::cancel_region(framework::Uuid region) {
    cancel(region, std::nullopt);
}

void RemoteDataChannels::cancel(framework::Uuid region, std::optional<framework::Uuid> item) {
    std::lock_guard lock(mutex_);
    const auto matches = [&](const auto& entry) {
        return entry.region == region && (!item || entry.item == *item);
    };
    std::erase_if(channels_, [&](const auto& entry) {
        return matches(entry.second);
    });
    std::erase_if(requests_, [&](const auto& request) {
        return matches(request.receiver);
    });
    std::erase_if(completed_, matches);
    for (auto& [message, pending] : pending_)
        if (matches(pending->receiver))
            pending->cancelled = true;
    changed_.notify_all();
    for (auto& worker : workers_)
        if (matches(worker))
            worker.thread.request_stop();
}

std::vector<RemoteDataChannels::Completion> RemoteDataChannels::take_completed(framework::Uuid region) {
    std::lock_guard lock(mutex_);
    workers_.remove_if([](const auto& worker) { return worker.finished; });
    std::vector<Completion> result;
    result.reserve(completed_.size());
    const auto first = std::stable_partition(completed_.begin(), completed_.end(),
        [region](const Completion& completion) { return completion.region != region; });
    std::move(first, completed_.end(), std::back_inserter(result));
    completed_.erase(first, completed_.end());
    return result;
}

framework::Uuid RemoteDataChannels::open(std::uint32_t local_id, framework::Uuid item,
                                        framework::Uuid requested, framework::Uuid region) {
    std::lock_guard lock(mutex_);
    for (const auto& [channel, receiver] : channels_)
        if (receiver.item == item && receiver.region == region)
            return channel;

    auto channel = requested;
    if (channel.is_zero()) {
        do {
            channel = framework::Uuid::random();
        } while (channel.is_zero() || channels_.contains(channel));
    }
    if (!channels_.emplace(channel, Receiver{local_id, item, region}).second)
        throw std::invalid_argument("Remote data channel already exists");
    return channel;
}

void RemoteDataChannels::close(framework::Uuid channel) {
    std::lock_guard lock(mutex_);
    channels_.erase(channel);
}

std::optional<RemoteDataChannels::Receiver> RemoteDataChannels::find(framework::Uuid channel) const {
    std::lock_guard lock(mutex_);
    const auto found = channels_.find(channel);
    if (found == channels_.end())
        return std::nullopt;
    return found->second;
}

RemoteDataChannels::Result RemoteDataChannels::request(framework::Uuid channel, Data data,
                                                       std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    const auto receiver = channels_.find(channel);
    if (receiver == channels_.end())
        return {Status::InvalidChannel, {}};
    auto message = framework::Uuid::random();
    while (message.is_zero() || pending_.contains(message))
        message = framework::Uuid::random();
    auto pending = std::make_shared<Pending>(Pending{channel, receiver->second, {}});
    pending_.emplace(message, pending);
    try {
        requests_.push_back({receiver->second, channel, message, std::move(data)});
        changed_.wait_for(lock, timeout, [&] { return pending->reply.has_value() || pending->cancelled; });
    } catch (...) {
        pending_.erase(message);
        std::erase_if(requests_, [&](const Request& request) { return request.message == message; });
        throw;
    }
    pending_.erase(message);
    std::erase_if(requests_, [&](const Request& request) { return request.message == message; });
    return {pending->reply ? Status::Replied : Status::TimedOut, std::move(pending->reply)};
}

std::vector<RemoteDataChannels::Request> RemoteDataChannels::take_requests(framework::Uuid region) {
    std::lock_guard lock(mutex_);
    std::vector<Request> result;
    result.reserve(requests_.size());
    const auto first = std::stable_partition(requests_.begin(), requests_.end(),
        [region](const Request& request) { return request.receiver.region != region; });
    std::move(first, requests_.end(), std::back_inserter(result));
    requests_.erase(first, requests_.end());
    return result;
}

bool RemoteDataChannels::reply(framework::Uuid channel, framework::Uuid message, Data data) {
    std::lock_guard lock(mutex_);
    auto found = pending_.find(message);
    if (message.is_zero()) {
        for (auto it = pending_.begin(); it != pending_.end(); ++it)
            if (it->second->channel == channel && !it->second->reply && !it->second->cancelled)
                found = it;
    }
    if (found == pending_.end() || found->second->reply || found->second->cancelled)
        return false;
    found->second->reply = std::move(data);
    changed_.notify_all();
    return true;
}
}
