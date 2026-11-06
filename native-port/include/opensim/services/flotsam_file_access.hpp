// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/services/flotsam_file_store.hpp"
#include <cstdint>
namespace opensim::services {
// The original static ExpiringKey<string> instance is shared by all module
// instances. Its owner supplies the same instance to their file backends.
struct FlotsamAccessEntries {
    virtual ~FlotsamAccessEntries() = default;
    virtual bool contains(const std::filesystem::path&) = 0;
    virtual void add(const std::filesystem::path&, int milliseconds) = 0;
    virtual void pump() = 0;
};
std::shared_ptr<FlotsamAccessEntries> flotsam_access_entries(std::function<double()> clock = {},
                                                             bool background = true);

class FlotsamFileAccess {
  public:
    using SetTime = std::function<void(const std::filesystem::path&)>;
    // Null entries preserve the original uninitialized tracker behavior.
    FlotsamFileAccess(std::shared_ptr<FlotsamAccessEntries>, SetTime);
    bool check_update(const std::filesystem::path&) const noexcept;
    void touch(const std::filesystem::path&) const noexcept;
    void note(const std::filesystem::path&, int milliseconds) const;

  private:
    std::shared_ptr<FlotsamAccessEntries> entries_;
    SetTime set_time_;
};
// Source File.SetLastAccessTime(path, DateTime.Now), retaining one runtime owner.
FlotsamFileAccess::SetTime
native_flotsam_access_time_setter(std::shared_ptr<framework::NativeInvariantDateTimeContext> datetime = {});
void native_flotsam_set_access_time(const std::filesystem::path&);
void attach_flotsam_file_access(const std::shared_ptr<FlotsamFileBackend>&,
                                const std::shared_ptr<FlotsamFileAccess>&);
}
