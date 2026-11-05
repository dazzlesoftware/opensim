// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_console.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
namespace opensim::services {
namespace {
using Output = std::shared_ptr<FlotsamConsoleOutput>;
[[noreturn]] void null_reference() {
    throw FlotsamConsoleError(FlotsamConsoleError::Kind::NullReference,
                              "Object reference not set to an instance of an object.");
}
void write(const Output& output, const std::string& text) {
    if (!output)
        null_reference();
    output->output(text);
}
std::shared_ptr<FlotsamCleanupTimer> timer(const std::shared_ptr<FlotsamCacheLifecycle>& life) {
    auto receiver = life->timer;
    if (!receiver)
        null_reference();
    return receiver;
}
bool stop_timer(const std::shared_ptr<FlotsamCacheLifecycle>& life) {
    std::lock_guard lock(life->timer_mutex);
    if (!life->timer_running)
        return false;
    timer(life)->stop();
    life->timer_running = false;
    return true;
}
void finish(const std::shared_ptr<FlotsamCacheLifecycle>& life, bool was_running) {
    std::lock_guard lock(life->timer_mutex);
    if (was_running) {
        timer(life)->start();
        life->timer_running = true;
    }
    life->cleanup_running = false;
}
}
FlotsamCacheConsole::FlotsamCacheConsole(std::shared_ptr<FlotsamInstanceConfig> config,
                                         std::shared_ptr<FlotsamCacheLifecycle> life,
                                         std::shared_ptr<FlotsamConsoleBackend> backend)
    : config_(std::move(config)), life_(std::move(life)), backend_(std::move(backend)) {
    auto& b = backend_;
    if (!config_ || !life_ || !b || !b->console || !b->post || !b->memory_count || !b->negative_count ||
        !b->clear_memory || !b->clear_negative || !b->clear_files || !b->load_defaults ||
        !b->delete_defaults || !b->file_count || !b->hit_report || !b->status_files || !b->last_write ||
        !b->format_date || !b->sleep || !b->touch || !b->clean || !b->now || !b->try_parse || !b->lower ||
        !b->equals_now)
        throw std::invalid_argument("Incomplete Flotsam console dependencies");
}
void FlotsamCacheConsole::expire(const std::shared_ptr<Arguments>& args,
                                 const std::shared_ptr<FlotsamInstanceConfig>& config,
                                 const std::shared_ptr<FlotsamCacheLifecycle>& life,
                                 const std::shared_ptr<FlotsamConsoleBackend>& b, const Output& output) {
    if (!args)
        null_reference();
    if (!config || !life || !b || !b->now || !b->try_parse || !b->equals_now)
        throw std::invalid_argument("Incomplete Flotsam expire dependencies");
    if (config->file && (!b->post || !b->sleep || !b->clean))
        throw std::invalid_argument("Incomplete Flotsam file expire dependencies");

    {
        std::lock_guard lock(life->timer_mutex);
        if (life->cleanup_running) {
            write(output, "Flotsam assets check already running");
            return;
        }
        life->cleanup_running = true;
    }
    if (args->size() < 3) {
        write(output, "Invalid parameters for Expire, please specify a valid date & time");
        life->cleanup_running = false;
        return;
    }
    FlotsamConsoleBackend::Text date_text;
    if (args->size() > 3) {
        std::string joined;
        for (std::size_t i = 2; i < args->size(); ++i) {
            if (i > 2)
                joined += ' ';
            joined += (*args)[i].value_or("");
        }
        date_text = joined;
    } else
        date_text = (*args)[2];
    FlotsamConsoleBackend::Date expiration;
    if (!date_text)
        null_reference();
    if (b->equals_now(date_text))
        expiration = b->now();
    else {
        if (!b->try_parse(date_text, expiration)) {
            write(output, "expire is not a valid date & time");
            life->cleanup_running = false;
            return;
        }
        if (expiration.ticks() >= b->now().ticks()) {
            write(output, "expire date & time must be in past");
            life->cleanup_running = false;
            return;
        }
    }
    if (config->file)
        b->post(
            [b, life, expiration] {
                bool was_running = stop_timer(life);
                if (was_running)
                    b->sleep(120);
                b->clean(expiration);
                finish(life, was_running);
            },
            "ExpireSceneAssets");
    else
        write(output, "File cache not active, not clearing.");
    return;
}
void FlotsamCacheConsole::handle(const std::shared_ptr<Arguments>& args) {
    auto b = backend_;
    auto config = config_;
    auto life = life_;
    auto output = b->console();
    if (!args)
        null_reference();
    if (args->size() >= 2) {
        const auto command = (*args)[1];
        const auto cmd = command.value_or("");
        if (command == "status") {
            b->post(
                [b, config, life, output] {
                    if (config->memory)
                        write(output, "[FLOTSAM ASSET CACHE] Memory Cache: " +
                                          std::to_string(b->memory_count()) + " assets");
                    else
                        write(output, "[FLOTSAM ASSET CACHE] Memory cache disabled");
                    if (config->file) {
                        bool scanning;
                        {
                            std::lock_guard lock(life->timer_mutex);
                            scanning = life->cleanup_running;
                        }
                        if (scanning)
                            write(
                                output,
                                "[FLOTSAM ASSET CACHE] a deep scan is in progress, skipping file cache assets count");
                        else {
                            write(output, "[FLOTSAM ASSET CACHE] counting file cache assets");
                            auto count = b->file_count(config->directory);
                            write(output,
                                  "[FLOTSAM ASSET CACHE]   File Cache: " + std::to_string(count) + " assets");
                        }
                    } else
                        write(output, "[FLOTSAM ASSET CACHE] File cache disabled");
                    for (const auto& line : b->hit_report())
                        write(output, line);
                    if (config->file) {
                        write(
                            output,
                            "[FLOTSAM ASSET CACHE] Deep scans have previously been performed on the following regions:");
                        for (const auto& entry : b->status_files(config->directory, "*.fac")) {
                            if (!entry)
                                null_reference();
                            auto path = framework::utf16_replacing(*entry);
                            auto start = path.find(u'_'), end = path.find(u'.');
                            if (start != path.npos && end != path.npos && start > 0 && end > 0) {
                                if (end < start) {
                                    auto length =
                                        static_cast<std::int64_t>(end) - static_cast<std::int64_t>(start);
#ifdef _WIN32
                                    const char* newline = "\r\n";
#else
                                    const char* newline = "\n";
#endif
                                    throw FlotsamConsoleError(
                                        FlotsamConsoleError::Kind::ArgumentOutOfRange,
                                        "length ('" + std::to_string(length) +
                                            "') must be a non-negative value. (Parameter 'length')" +
                                            newline + "Actual value was " + std::to_string(length) + ".",
                                        "length", length);
                                }
                                auto region =
                                    framework::utf8(std::u16string_view(path).substr(start + 1, end - start));
                                auto stamp = b->last_write(entry);
                                write(output, "[FLOTSAM ASSET CACHE] Region: " + region + ", " +
                                                  b->format_date(stamp, "MM/dd/yyyy hh:mm:ss"));
                            }
                        }
                    }
                },
                "CacheStatus");
            return;
        }
        if (command == "clear") {
            bool memory = args->size() == 2, file = memory;
            for (const auto arg : *args) {
                if (!arg)
                    null_reference();
                if (b->lower(*arg) == "memory")
                    memory = true;
                else if (b->lower(*arg) == "file")
                    file = true;
            }
            if (memory) {
                if (config->memory) {
                    b->clear_memory();
                    write(output, "Memory cache cleared.");
                } else
                    write(output, "Memory cache not enabled.");
            }
            if (file) {
                if (config->file) {
                    b->clear_files();
                    write(output, "File cache cleared.");
                } else
                    write(output, "File cache not enabled.");
            }
            if (config->negative)
                b->clear_negative();
            return;
        }
        if (command == "clearnegatives") {
            if (config->negative) {
                auto size = b->negative_count();
                b->clear_negative();
                write(output, "Flotsam cache of negatives cleared (" + std::to_string(size) + " entries)");
            } else
                write(output, "Flotsam cache of negatives not enabled");
            return;
        }
        if (command == "assets") {
            {
                std::lock_guard lock(life->timer_mutex);
                if (life->cleanup_running) {
                    write(output, "Flotsam assets check already running");
                    return;
                }
                life->cleanup_running = true;
            }
            write(output, "Flotsam Ensuring assets are cached for all scenes.");
            b->post(
                [b, life, output] {
                    bool was_running = stop_timer(life);
                    if (was_running)
                        b->sleep(120);
                    auto count = b->touch(true);
                    finish(life, was_running);
                    write(output, "Completed check with " + std::to_string(count) + " assets.");
                },
                "TouchAllSceneAssets");
            return;
        }
        if (command == "expire") {
            expire(args, config, life, b, output);
            return;
        }
        if (command == "cachedefaultassets") {
            b->load_defaults();
            return;
        }
        if (command == "deletedefaultassets") {
            b->delete_defaults();
            return;
        }
        write(output, "Unknown command " + cmd);
        return;
    }
    if (args->size() == 1) {
        write(output, "fcache assets - Attempt a deep cache of all assets in all scenes");
        write(output, "fcache expire <datetime> - Purge assets older than the specified date & time");
        write(output, "fcache clear [file] [memory] - Remove cached assets");
        write(output, "fcache status - Display cache status");
        write(
            output,
            "fcache cachedefaultassets - loads default assets to cache replacing existent ones, this may override grid assets. Use with care");
        write(
            output,
            "fcache deletedefaultassets - deletes default local assets from cache so they can be refreshed from grid");
    }
}
}
