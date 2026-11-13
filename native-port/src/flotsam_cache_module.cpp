// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/flotsam_cache_module.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <thread>
namespace opensim::services {
namespace {
bool white(const std::optional<std::string>& text) {
    if (!text)
        return true;
    auto chars = framework::utf16_replacing(*text);
    return std::all_of(chars.begin(), chars.end(), framework::unicode_whitespace);
}
class CleanupTimer final : public FlotsamCleanupTimer {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        bool shutdown = false, armed = false;
        std::uint64_t generation = 0;
        std::chrono::milliseconds interval;
        std::chrono::steady_clock::time_point due;
        std::function<void()> callback;
        explicit State(int ms) : interval(ms) {
        }
        void run() {
            std::unique_lock lock(mutex);
            while (!shutdown) {
                if (!armed) {
                    changed.wait(lock);
                    continue;
                }
                auto version = generation;
                auto deadline = due;
                if (changed.wait_until(lock, deadline, [&] { return shutdown || generation != version; }))
                    continue;
                armed = false;
                auto handler = callback;
                lock.unlock();
                try {
                    if (handler)
                        handler();
                } catch (...) {
                }
                lock.lock();
            }
        }
    };
    std::shared_ptr<State> state_;
    std::thread thread_;

  public:
    explicit CleanupTimer(double milliseconds) {
        auto rounded = std::ceil(milliseconds);
        if (!std::isfinite(rounded) || rounded <= 0 || rounded > INT32_MAX)
            throw std::invalid_argument("Invalid cleanup timer interval");
        state_ = std::make_shared<State>(static_cast<int>(rounded));
        thread_ = std::thread([state = state_] { state->run(); });
    }
    ~CleanupTimer() override {
        {
            std::lock_guard lock(state_->mutex);
            state_->shutdown = true;
            state_->armed = false;
            ++state_->generation;
            state_->changed.notify_all();
        }
        if (thread_.joinable()) {
            if (thread_.get_id() == std::this_thread::get_id())
                thread_.detach();
            else
                thread_.join();
        }
    }
    void subscribe(std::function<void()> callback) override {
        std::lock_guard lock(state_->mutex);
        state_->callback = std::move(callback);
    }
    void start() override {
        std::lock_guard lock(state_->mutex);
        if (!state_->armed) {
            state_->armed = true;
            state_->due = std::chrono::steady_clock::now() + state_->interval;
            ++state_->generation;
            state_->changed.notify_all();
        }
    }
    void stop() override {
        std::lock_guard lock(state_->mutex);
        state_->armed = false;
        ++state_->generation;
        state_->changed.notify_all();
    }
    // System.Timers.Timer.Close stops the timer without disposing the component;
    // the source object may be started again after Close.
    void close() override {
        stop();
    }
};
}
std::shared_ptr<FlotsamCacheSharedState> shared_flotsam_cache_state() {
    static auto state = std::make_shared<FlotsamCacheSharedState>();
    return state;
}
std::shared_ptr<FlotsamCleanupTimer> native_flotsam_cleanup_timer(double milliseconds) {
    return std::make_shared<CleanupTimer>(milliseconds);
}
std::shared_ptr<FlotsamCleanupTimer> FlotsamCacheModuleBackend::make_timer(double interval) {
    return native_flotsam_cleanup_timer(interval);
}
std::shared_ptr<framework::ObjectJobQueue> FlotsamCacheModuleBackend::make_worker() {
    return std::make_shared<framework::ObjectJobEngine>(
        [](std::any value) {
            try {
                std::any_cast<std::function<void()>>(value)();
            } catch (...) {
            }
        },
        "FloatsamCacheWriter", 1000, 1);
}
FlotsamCacheModule::FlotsamCacheModule(Options options, std::shared_ptr<framework::IAssetCache> cache,
                                       std::shared_ptr<FlotsamCacheModuleBackend> backend,
                                       std::shared_ptr<FlotsamCacheSharedState> shared)
    : options_(std::move(options)), cache_(std::move(cache)), backend_(std::move(backend)),
      shared_(shared ? std::move(shared) : shared_flotsam_cache_state()) {
    if (!cache_ || !backend_)
        throw std::invalid_argument("Missing Flotsam cache module dependency");
}
void FlotsamCacheModule::add_region(std::shared_ptr<FlotsamCacheScene> scene) {
    if (!options_.enabled)
        return;
    if (!scene)
        throw std::invalid_argument("Null cache region");
    scene->register_cache(cache_);
    life_->scenes.push_back(std::move(scene));
}
void FlotsamCacheModule::stop_workers() {
    life_->cleanup_running = false;
    if (life_->timer_running) {
        life_->timer_running = false;
        life_->timer->stop();
        life_->timer->close();
    }
    if (auto worker = shared_->worker.load()) {
        worker->dispose();
        shared_->worker.store(nullptr);
    }
}
void FlotsamCacheModule::remove_region(std::shared_ptr<FlotsamCacheScene> scene) {
    if (!options_.enabled)
        return;
    if (!scene)
        throw std::invalid_argument("Null cache region");
    scene->unregister_cache(cache_);
    auto found = std::find(life_->scenes.begin(), life_->scenes.end(), scene);
    if (found != life_->scenes.end())
        life_->scenes.erase(found);
    std::lock_guard lock(life_->timer_mutex);
    if (life_->scenes.empty())
        stop_workers();
}
void FlotsamCacheModule::close() {
    if (life_->scenes.empty()) {
        std::lock_guard lock(life_->timer_mutex);
        stop_workers();
    }
}
void FlotsamCacheModule::region_loaded(std::shared_ptr<FlotsamCacheScene> scene) {
    if (!options_.enabled)
        return;
    if (!life_->service) {
        if (!scene)
            throw std::invalid_argument("Null cache region");
        life_->service = scene->request_asset_service();
    }
    std::lock_guard lock(life_->timer_mutex);
    if (!life_->timer_running && options_.file && options_.expiration_ticks > 0 &&
        options_.cleanup_ticks > 0) {
        auto interval = std::clamp(static_cast<double>(options_.cleanup_ticks) / 10000.0, -922337203685477.0,
                                   922337203685477.0);
        life_->timer = backend_->make_timer(interval);
        std::weak_ptr<FlotsamCacheLifecycle> life = life_;
        std::weak_ptr<FlotsamCacheModuleBackend> backend = backend_;
        life_->timer->subscribe([life, backend] {
            if (auto state = life.lock())
                if (auto host = backend.lock())
                    host->cleanup_elapsed(state);
        });
        life_->timer->start();
        life_->timer_running = true;
    }
    if (options_.file && !shared_->worker.load())
        shared_->worker = backend_->make_worker();
    if (!white(options_.loader)) {
        if (!scene)
            throw std::invalid_argument("Null cache region");
        auto region = scene->cache_region_id();
        if (life_->scenes.empty())
            throw std::out_of_range("No first cache region");
        if (region == life_->scenes.front()->cache_region_id()) {
            auto loader = backend_->load_loader(*options_.loader);
            if (loader) {
                auto ids = std::make_shared<std::unordered_set<std::string>>();
                loader->for_each(options_.loader_arguments,
                                 [cache = cache_, ids, shared = shared_](AssetService::Asset asset) {
                                     cache->cache(asset, true);
                                     if (!asset)
                                         throw std::invalid_argument("Null default cache asset");
                                     std::lock_guard lock(shared->defaults_mutex);
                                     ids->insert(asset->id());
                                 });
                shared_->default_assets = std::move(ids);
            }
        }
    }
}
void FlotsamCacheModule::load_default_assets(std::function<void(const std::string&)> info) {
    if (!info)
        throw std::invalid_argument("Missing default-assets logger");
    if (white(options_.loader)) {
        info("[FLOTSAM ASSET CACHE] default assets loader not defined");
        return;
    }
    auto loader = backend_->load_loader(*options_.loader);
    if (!loader) {
        info("[FLOTSAM ASSET CACHE] default assets loader not found");
        return;
    }
    info("[FLOTSAM ASSET CACHE] start loading local default assets");
    auto count = std::make_shared<std::atomic<std::int32_t>>(0);
    auto ids = std::make_shared<std::unordered_set<std::string>>();
    loader->for_each(
        options_.loader_arguments, [cache = cache_, ids, shared = shared_, count](AssetService::Asset asset) {
            cache->cache(asset, true);
            if (!asset)
                throw std::runtime_error("Object reference not set to an instance of an object.");
            {
                std::lock_guard lock(shared->defaults_mutex);
                ids->insert(asset->id());
            }
            count->fetch_add(1);
        });
    shared_->default_assets = ids;
    info("[FLOTSAM ASSET CACHE] loaded " + std::to_string(count->load()) + " local default assets");
}
void FlotsamCacheModule::delete_default_assets(std::function<void(const std::string&)> info) {
    if (!info)
        throw std::invalid_argument("Missing default-assets logger");
    if (white(options_.loader)) {
        info("[FLOTSAM ASSET CACHE] default assets loader not defined");
        return;
    }
    auto loader = backend_->load_loader(*options_.loader);
    if (!loader) {
        info("[FLOTSAM ASSET CACHE] default assets loader not found");
        return;
    }
    info("[FLOTSAM ASSET CACHE] started deleting local default assets");
    auto count = std::make_shared<std::atomic<std::int32_t>>(0);
    loader->for_each(options_.loader_arguments, [cache = cache_, count](AssetService::Asset asset) {
        if (!asset)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        cache->expire(asset->id());
        count->fetch_add(1);
    });
    shared_->default_assets = std::make_shared<std::unordered_set<std::string>>();
    info("[FLOTSAM ASSET CACHE] deleted " + std::to_string(count->load()) + " local default assets");
}
std::shared_ptr<FlotsamFileBackend>
native_flotsam_shared_file_backend(std::shared_ptr<FlotsamCacheSharedState> shared,
                                   std::function<void(const std::string&)> warn) {
    if (!shared)
        throw std::invalid_argument("Null shared cache state");
    auto backend = native_flotsam_file_backend({}, std::move(warn));
    std::weak_ptr<FlotsamCacheSharedState> weak = shared;
    backend->worker_available = [weak] {
        auto state = weak.lock();
        return state && bool(state->worker.load());
    };
    backend->enqueue = [weak](auto task) {
        auto state = weak.lock();
        auto worker = state ? state->worker.load() : nullptr;
        if (!worker)
            throw std::invalid_argument("Null shared file-cache worker");
        worker->enqueue(std::move(task));
    };
    return backend;
}
}
