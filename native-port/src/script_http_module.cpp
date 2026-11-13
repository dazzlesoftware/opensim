// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/script_http_module.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/dotnet_regex_char_class.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/invariant_case.hpp"
#include "opensim/framework/text.hpp"
#include <httplib.h>
#include <algorithm>
#include <atomic>

namespace opensim::network {
namespace {

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void set_header(ScriptHttpRequest& request, std::string name, std::string value) {
    const auto found = std::ranges::find(request.headers, name, &std::pair<std::string, std::string>::first);
    if (found == request.headers.end())
        request.headers.emplace_back(std::move(name), std::move(value));
    else
        found->second = std::move(value);
}

constexpr std::string_view fatal_headers =
    "|accept|accept-charset|content-type|from|host|referer|te|trailer|upgrade|user-agent|via|";
constexpr std::string_view skipped_headers =
    "|accept-ch|accept-ch-lifetime|access-control-request-headers|access-control-request-method|accept-encoding|"
    "accept-patch|accept-post|accept-ranges|cache-control|connection|content-length|cookie|cookie2|date|"
    "device-memory|dtn|early-data|expect|feature-policy|keep-alive|if-match|if-modified-since|if-none-match|"
    "if-unmodified-since|max-forwards|origin|pragma|server|set-cookie|set-cookie2|transfer-encoding|vary|"
    "viewport-width|warning|width|x-forwarded-for|x-forwarded-host|x-forwarded-proto|";
constexpr std::string_view scene_headers =
    "|x-secondlife-shard|x-secondlife-object-name|x-secondlife-object-key|x-secondlife-region|"
    "x-secondlife-local-position|x-secondlife-local-velocity|x-secondlife-local-rotation|"
    "x-secondlife-owner-name|x-secondlife-owner-key|";
constexpr std::string_view blocked_url_headers =
    "|connection|content-length|from|host|proxy-authorization|referer|trailer|transfer-encoding|via|authorization|";

bool listed(std::string_view list, std::string_view name) {
    list.remove_prefix(1);
    while (!list.empty()) {
        const auto separator = list.find('|');
        if (framework::invariant_equals_ignore_case(list.substr(0, separator), name))
            return true;
        list.remove_prefix(separator == list.npos ? list.size() : separator + 1);
    }
    return false;
}

framework::ConfigUriText uri_text(std::string_view text) {
    return std::make_shared<const std::u16string>(framework::utf16_replacing(text));
}

bool parse_options(ScriptHttpRequest& request, ScriptHttpSubmission& result,
                   const std::vector<ScriptHttpOption>& options, int max_body_bytes) {
    unsigned custom_count = 0;
    std::vector<std::pair<int, std::string_view>> parameters;
    for (std::size_t index = 0; index < options.size(); index += 2) {
        const auto flag = options[index].integer.value_or(-1);
        if (flag < 0 || flag > 6) {
            result.errors.push_back("llHTTPRequest: Parameter " + std::to_string(index) + " is an invalid flag");
            result.invalid_flag = true;
            return false;
        }
        if (flag != 5) {
            parameters.emplace_back(flag, options.at(index + 1).text);
            continue;
        }
        ++index;
        for (unsigned count = 1; count <= 8; ++count) {
            if (custom_count >= 8) {
                result.errors.emplace_back("llHTTPRequest: Max number of custom headers is 8, excess ignored");
                break;
            }
            if (options.size() - index < 2) {
                if (count == 1)
                    result.errors.push_back("llHTTPRequest: Missing name/value for custom header at parameter " +
                                            std::to_string(index));
                return false;
            }
            const auto& name = options[index].text;
            const auto lower = framework::lower_invariant_culture(name);
            if (framework::invariant_starts_with(lower, "proxy-") ||
                framework::invariant_starts_with(lower, "sec-") || listed(fatal_headers, name)) {
                result.errors.push_back("llHTTPRequest: Name is invalid as a custom header at parameter " +
                                        std::to_string(index));
                return false;
            }
            if (!listed(skipped_headers, name) && !listed(scene_headers, name)) {
                const auto& value = options[index + 1].text;
                if (framework::utf16_replacing(name).size() + framework::utf16_replacing(value).size() > 253) {
                    result.errors.push_back(
                        "llHTTPRequest: name and value length exceds 253 characters for custom header at parameter " +
                        std::to_string(index));
                    return false;
                }
                set_header(request, name, value);
                ++custom_count;
            }
            if (index + 2 >= options.size())
                break;
            const auto next = framework::utf16_replacing(options[index + 2].text);
            if (next.empty())
                throw std::out_of_range("Index was outside the bounds of the array.");
            constexpr std::uint8_t decimal_digit = 8;
            if (framework::dotnet_regex_unicode_category(next.front()) == decimal_digit)
                break;
            index += 2;
        }
    }
    for (const auto& [flag, value] : parameters) {
        const auto integer = [&] { return framework::invariant_convert_to_int32(framework::utf16_replacing(value)); };
        switch (flag) {
        case 0:
            request.method = value;
            break;
        case 1:
            request.mime_type = value;
            break;
        case 2:
            try {
                const auto length = integer();
                request.max_body_bytes = length > max_body_bytes ? max_body_bytes : std::max(length, 64);
            } catch (const framework::InvariantNumberError&) {
                // An invalid body length leaves the previous limit in place.
            }
            break;
        case 3:
            request.verify_certificate = integer() != 0;
            break;
        case 6:
            request.pragma_no_cache = integer() != 0;
            break;
        }
    }
    return true;
}

void parse_url_headers(std::string& url, ScriptHttpRequest& request) {
    if (const auto newline = url.find('\n'); newline != url.npos) {
        auto lines = std::string_view(url).substr(newline + 1);
        while (!lines.empty()) {
            const auto end = lines.find('\n');
            const auto line = lines.substr(0, end);
            if (line.empty())
                break;
            if (const auto colon = line.find(':'); colon != line.npos) {
                auto name = framework::trim_utf8(line.substr(0, colon));
                if (!listed(scene_headers, name) && !listed(blocked_url_headers, name))
                    set_header(request, std::move(name), framework::trim_utf8(line.substr(colon + 1)));
            }
            if (end == lines.npos)
                break;
            lines.remove_prefix(end + 1);
        }
        url = framework::trim_utf8(std::string_view(url).substr(0, newline));
        if (const auto protocol = url.find(" HTTP/"); protocol != url.npos)
            url.resize(protocol);
    }
    const auto scheme_length = url.starts_with("https://") ? 8u : url.starts_with("http://") ? 7u : 0u;
    if (!scheme_length)
        return;
    const auto at = url.find('@', scheme_length);
    if (at == url.npos)
        return;
    const auto credentials = framework::utf16_replacing(
        std::string_view(url).substr(scheme_length, at - scheme_length));
    const auto colon = credentials.find(u':');
    if (colon == credentials.npos || colon == 0 || colon + 1 == credentials.size() ||
        !std::ranges::all_of(std::u16string_view(credentials).substr(0, colon), framework::dotnet_regex_word_char) ||
        !std::ranges::all_of(std::u16string_view(credentials).substr(colon + 1), framework::dotnet_regex_word_char))
        return;
    std::string ascii;
    ascii.reserve(credentials.size());
    for (const auto character : credentials)
        ascii += character <= 0x7f ? static_cast<char>(character) : '?';
    set_header(request, "Authorization", "Basic " + httplib::detail::base64_encode(ascii));
    url.erase(scheme_length, at + 1 - scheme_length);
}

}

struct ScriptHttpService::Job {
    framework::Uuid part, item, id;
    std::uint32_t local_id = 0;
    unsigned redirects = 0;
    ScriptHttpRequest request;
    std::weak_ptr<RegionState> region;
    std::atomic<bool> cancelled{false};
};

struct ScriptHttpService::RegionState {
    std::mutex mutex;
    std::deque<ScriptHttpCompletion> completed;
};

ScriptHttpService::ScriptHttpService(std::shared_ptr<const framework::OutboundUrlFilter> filter,
                                     int max_body_bytes, int timeout_ms, int worker_count,
                                     std::shared_ptr<framework::ConfigXmlProxy> proxy)
    : filter_(std::move(filter)), uris_(framework::native_config_uri_object_backend()),
      transport_(filter_, native_web_http_handler_factory(), timeout_ms, worker_count, std::move(proxy)),
      max_body_bytes_(max_body_bytes), workers_(static_cast<std::size_t>(std::max(worker_count, 1))) {}

ScriptHttpService::~ScriptHttpService() {
    {
        std::lock_guard lock(mutex_);
        for (auto& worker : workers_)
            worker.thread.request_stop();
    }
    for (auto& worker : workers_)
        if (worker.thread.joinable())
            worker.thread.join();
}

std::size_t ScriptHttpService::active_workers() const {
    std::lock_guard lock(mutex_);
    return active_workers_;
}

ScriptHttpModule::ScriptHttpModule(std::shared_ptr<ScriptHttpService> service,
                                   script::HttpRequestThrottleSettings throttle)
    : service_(std::move(service)), state_(std::make_shared<ScriptHttpService::RegionState>()), throttle_(throttle) {
    if (!service_)
        throw std::invalid_argument("Script HTTP module requires its shared service");
}

ScriptHttpModule::~ScriptHttpModule() {
    stop_all();
}

ScriptHttpSubmission ScriptHttpModule::submit(
    framework::Uuid part, std::uint32_t local_id, framework::Uuid owner, framework::Uuid item,
    std::string url, const std::vector<ScriptHttpOption>& options, std::string body,
    const std::function<void(ScriptHttpRequest&)>& add_scene_headers) {
    ScriptHttpSubmission result;
    if (!throttle_.allow(local_id, owner, now_seconds())) {
        result.key = framework::Uuid{}.str();
        return result;
    }
    const auto uri = service_->uris_->try_create(uri_text(url), framework::ConfigUriObjectKind::Absolute);
    if (!uri) {
        result.errors.emplace_back("llHTTPRequest: Invalid url");
        return result;
    }
    const auto address = uri->http_components();
    if (!address || !address->is_http()) {
        result.errors.emplace_back("llHTTPRequest: Invalid url schema");
        return result;
    }
    if (!service_->filter_->allows(*address)) {
        result.errors.push_back("llHttpRequest: Request to " + url + " disallowed by filter");
        return result;
    }
    ScriptHttpRequest request;
    if (!parse_options(request, result, options, service_->max_body_bytes_))
        return result;
    if (add_scene_headers)
        add_scene_headers(request);
    parse_url_headers(url, request);
    request.uri = service_->uris_->try_create(uri_text(url), framework::ConfigUriObjectKind::Absolute);
    request.body = std::move(body);
    auto job = std::make_shared<ScriptHttpService::Job>();
    job->part = part;
    job->local_id = local_id;
    job->item = item;
    job->id = framework::Uuid::random();
    job->request = std::move(request);
    job->region = state_;
    service_->enqueue(job);
    result.key = job->id.str();
    return result;
}

void ScriptHttpService::enqueue(const std::shared_ptr<Job>& job) {
    std::lock_guard lock(mutex_);
    if (!pending_.emplace(job->id, job).second)
        throw std::logic_error("Duplicate script HTTP request ID");
    try {
        queue_locked(job);
    } catch (...) {
        pending_.erase(job->id);
        throw;
    }
}

void ScriptHttpService::queue_locked(const std::shared_ptr<Job>& job) {
    if (waiting_.size() >= 5000)
        return;
    waiting_.push_back(job);
    if (active_workers_ < workers_.size() && active_workers_ < waiting_.size()) {
        const auto worker = std::ranges::find(workers_, false, &Worker::running);
        auto* slot = &*worker;
        try {
            slot->thread = std::jthread([this, slot](std::stop_token stop) { work(*slot, stop); });
        } catch (...) {
            waiting_.pop_back();
            throw;
        }
        slot->running = true;
        ++active_workers_;
    }
    wake_.notify_one();
}

void ScriptHttpService::cancel(const RegionState& region, std::optional<framework::Uuid> item) {
    std::lock_guard lock(mutex_);
    std::erase_if(pending_, [&](const auto& entry) {
        const auto& job = entry.second;
        if (job->region.lock().get() != &region || (item && job->item != *item))
            return false;
        job->cancelled = true;
        return true;
    });
}

void ScriptHttpModule::stop(std::uint32_t local_id, framework::Uuid item) {
    throttle_.stop(local_id, now_seconds());
    service_->cancel(*state_, item);
    std::lock_guard lock(state_->mutex);
    std::erase_if(state_->completed, [item](const ScriptHttpCompletion& completion) {
        return completion.item == item;
    });
}

std::deque<ScriptHttpCompletion> ScriptHttpModule::take_completed() {
    std::lock_guard lock(state_->mutex);
    std::deque<ScriptHttpCompletion> result;
    result.swap(state_->completed);
    return result;
}

void ScriptHttpModule::stop_all() {
    service_->cancel(*state_);
    throttle_.clear();
    std::lock_guard lock(state_->mutex);
    state_->completed.clear();
}

void ScriptHttpService::work(Worker& worker, std::stop_token stop) {
    for (;;) {
        std::shared_ptr<Job> job;
        {
            std::unique_lock lock(mutex_);
            if (!wake_.wait_for(lock, stop, std::chrono::seconds(2), [&] { return !waiting_.empty(); }) ||
                stop.stop_requested()) {
                worker.running = false;
                --active_workers_;
                return;
            }
            job = std::move(waiting_.front());
            waiting_.pop_front();
        }
        if (job->cancelled.load())
            continue;
        ScriptHttpAttempt attempt;
        try {
            attempt = transport_.send_attempt(job->request, job->redirects,
                                              [&] { return stop.stop_requested() || job->cancelled.load(); });
        } catch (const std::exception& error) {
            attempt = ScriptHttpReply{499, error.what()};
        }
        std::lock_guard lock(mutex_);
        if (stop.stop_requested() || !pending_.contains(job->id))
            continue;
        if (auto* next = std::get_if<framework::ConfigUriObjectPtr>(&attempt)) {
            job->request.uri = std::move(*next);
            ++job->redirects;
            try {
                queue_locked(job);
                continue;
            } catch (const std::exception& error) {
                attempt = ScriptHttpReply{499, error.what()};
            }
        }
        pending_.erase(job->id);
        if (auto* reply = std::get_if<ScriptHttpReply>(&attempt)) {
            if (const auto region = job->region.lock()) {
                std::lock_guard region_lock(region->mutex);
                region->completed.push_back({job->part, job->id, job->local_id, std::move(*reply), job->item});
            }
        }
    }
}

}
