// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <stdexcept>
namespace opensim::framework {
// .NET8 TimeSpan.FromHours/TotalHours numeric boundary. Every Int64 tick value
// is valid. NaN is an ArgumentException; out-of-range/Infinity is Overflow.
class TimeSpanError final : public std::runtime_error {
  public:
    enum class Kind { ArgumentNaN, Overflow };
    explicit TimeSpanError(Kind);
    Kind kind() const noexcept {
        return kind_;
    }
    std::int32_t hresult() const noexcept;

  private:
    Kind kind_;
};
std::int64_t timespan_from_hours(double hours);
double timespan_total_hours(std::int64_t ticks) noexcept;
}
