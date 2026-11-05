// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <any>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <exception>
namespace opensim::framework {
class ObjectJobQueue {
  public:
    virtual ~ObjectJobQueue() = default;
    virtual bool enqueue(std::any job) = 0;
    virtual std::size_t count() const = 0;
    virtual void cancel() = 0;
    virtual void dispose() = 0;
};
class ObjectJobEngine : public ObjectJobQueue {
  public:
    using Callback = std::function<void(std::any)>;
    using Error = std::function<void(const std::string&, const std::exception&)>;
    ObjectJobEngine(Callback callback, std::string name, int hold_ms = 1000, int concurrency = 1,
                    Error error = {});
    ~ObjectJobEngine() override;
    bool enqueue(std::any job) override;
    std::size_t count() const override;
    void cancel() override;
    void dispose() override;
    std::size_t active_threads() const;

  private:
    struct State;
    struct Thread;
    static void run(std::shared_ptr<State> state);
    std::shared_ptr<State> state_;
    std::vector<std::unique_ptr<Thread>> threads_;
};
}
