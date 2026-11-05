// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/time_span.hpp"
#include <cmath>
#include <limits>
namespace opensim::framework {
TimeSpanError::TimeSpanError(Kind kind)
    : runtime_error(kind == Kind::ArgumentNaN ? "TimeSpan does not accept floating point Not-a-Number values."
                                              : "TimeSpan overflowed because the duration is too long."),
      kind_(kind) {}
std::int32_t TimeSpanError::hresult() const noexcept {
    return kind_ == Kind::ArgumentNaN ? -2147024809 : -2146233066;
}
std::int64_t timespan_from_hours(double hours) {
    if (std::isnan(hours))
        throw TimeSpanError(TimeSpanError::Kind::ArgumentNaN);
    const double ticks = hours * 36000000000.0;
    constexpr double limit = 9223372036854775808.0;
    if (ticks > limit || ticks < -limit || std::isnan(ticks))
        throw TimeSpanError(TimeSpanError::Kind::Overflow);
    // The rounded Double representation of Int64.MaxValue is +2^63. CLR's
    // IntervalFromDoubleTicks explicitly maps that otherwise invalid cast to Max.
    if (ticks == limit)
        return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(ticks);
}
double timespan_total_hours(std::int64_t ticks) noexcept {
    return static_cast<double>(ticks) / 36000000000.0;
}
}
