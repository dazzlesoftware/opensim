// Copyright (c) Contributors, http://opensimulator.org/
// Consumed TimeZoneInfo methods ported from dotnet/runtime v8.0.31.
// .NET Foundation, MIT license; see third_party/dotnet_collation/LICENSE.
#include "opensim/framework/invariant_datetime_context.hpp"
#include <algorithm>
#include <chrono>
#include <optional>
#include <stdexcept>
namespace opensim::framework {
namespace {
constexpr std::int64_t maximum_ticks = 3155378975999999999ll, day_ticks = 864000000000ll;
struct TimeSpan {
    std::int64_t Ticks = 0;
    explicit TimeSpan(std::int64_t value = 0) : Ticks(value) {
    }
    TimeSpan Negate() const {
        return TimeSpan(-Ticks);
    }
    friend auto operator<=>(TimeSpan, TimeSpan) = default;
    TimeSpan operator-() const {
        return TimeSpan(-Ticks);
    }
    friend TimeSpan operator+(TimeSpan a, TimeSpan b) {
        return TimeSpan(a.Ticks + b.Ticks);
    }
    TimeSpan& operator+=(TimeSpan b) {
        Ticks += b.Ticks;
        return *this;
    }
};
struct DateTime {
    std::int64_t Ticks = 0;
    int Kind = 0;
    bool ambiguous = false;
    DateTime() = default;
    explicit DateTime(std::int64_t ticks, int kind = 0, bool ambiguity = false)
        : Ticks(ticks), Kind(kind), ambiguous(ambiguity) {
        if (ticks < 0 || ticks > maximum_ticks)
            throw std::out_of_range("DateTime ticks");
    }
    DateTime(int y, int m, int d, int h = 0, int minute = 0, int second = 0)
        : DateTime(date_ticks(y, m, d) + (std::int64_t(h) * 3600 + minute * 60 + second) * 10000000ll) {
    }
    explicit DateTime(AssetDateTime value)
        : DateTime(value.ticks(), value.kind(), (value.serialized_data() >> 62) == 3) {
    }
    static int DaysInMonth(int y, int m) {
        using namespace std::chrono;
        return unsigned(year_month_day_last{year(y) / month_day_last{month(static_cast<unsigned>(m))}}.day());
    }
    static std::int64_t date_ticks(int y, int m, int d) {
        using namespace std::chrono;
        if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || d > DaysInMonth(y, m))
            throw std::out_of_range("DateTime calendar");
        return (sys_days{year(y) / static_cast<unsigned>(m) / static_cast<unsigned>(d)} -
                sys_days{year(1) / 1 / 1})
                   .count() *
               day_ticks;
    }
    std::chrono::year_month_day parts() const {
        using namespace std::chrono;
        return year_month_day{sys_days{year(1) / 1 / 1} + days(Ticks / day_ticks)};
    }
    int Year() const {
        return int(parts().year());
    }
    int Month() const {
        return unsigned(parts().month());
    }
    int Day() const {
        return unsigned(parts().day());
    }
    int DayOfWeek() const {
        return static_cast<int>((Ticks / day_ticks + 1) % 7);
    }
    DateTime Date() const {
        return DateTime(Ticks / day_ticks * day_ticks, Kind, ambiguous);
    }
    TimeSpan TimeOfDay() const {
        return TimeSpan(Ticks % day_ticks);
    }
    DateTime AddTicks(std::int64_t value) const {
        return DateTime(Ticks + value, Kind, ambiguous);
    }
    DateTime AddYears(int value) const {
        auto y = Year() + value;
        if (y < 1 || y > 9999)
            throw std::out_of_range("DateTime AddYears");
        return DateTime(date_ticks(y, Month(), std::min(Day(), DaysInMonth(y, Month()))) + Ticks % day_ticks,
                        Kind, ambiguous);
    }
    DateTime AddDays(int value) const {
        return AddTicks(std::int64_t(value) * day_ticks);
    }
    bool IsAmbiguousDaylightSavingTime() const {
        return ambiguous;
    }
    friend auto operator<=>(DateTime a, DateTime b) {
        return a.Ticks <=> b.Ticks;
    }
    friend DateTime operator+(DateTime a, TimeSpan b) {
        return a.AddTicks(b.Ticks);
    }
    friend DateTime operator-(DateTime a, TimeSpan b) {
        return a.AddTicks(-b.Ticks);
    }
};
const DateTime min_date{}, max_date{maximum_ticks};
struct TransitionTime {
    DateTime TimeOfDay;
    int Month = 1, Week = 1, Day = 1, DayOfWeek = 0;
    bool IsFixedDateRule = true;
    explicit TransitionTime(InvariantTimeZoneTransition t)
        : TimeOfDay(t.time_of_day_ticks), Month(t.month), Week(t.week), Day(t.day), DayOfWeek(t.day_of_week),
          IsFixedDateRule(t.fixed_date) {
    }
    TransitionTime() = default;
};
struct Rule {
    DateTime DateStart, DateEnd;
    TimeSpan DaylightDelta, BaseUtcOffsetDelta;
    TransitionTime DaylightTransitionStart, DaylightTransitionEnd;
    bool NoDaylightTransitions = false, HasDaylightSaving = false;
    explicit Rule(InvariantTimeZoneRule value)
        : DateStart(value.start), DateEnd(value.end), DaylightDelta(value.daylight_delta_ticks),
          BaseUtcOffsetDelta(value.base_offset_delta_ticks), DaylightTransitionStart(value.daylight_start),
          DaylightTransitionEnd(value.daylight_end), NoDaylightTransitions(value.no_daylight_transitions) {
        HasDaylightSaving =
            DaylightDelta.Ticks != 0 ||
            (DaylightTransitionStart.Month != 0 && DaylightTransitionStart.TimeOfDay.Ticks != 0) ||
            (DaylightTransitionEnd.Month != 0 && DaylightTransitionEnd.TimeOfDay.Ticks != 10000);
    }
    bool IsStartDateMarkerForBeginningOfYear() const;
    bool IsEndDateMarkerForEndOfYear() const;
};
using AdjustmentRule = std::shared_ptr<const Rule>;
struct DaylightTimeStruct {
    DateTime Start, End;
    TimeSpan Delta;
    DaylightTimeStruct() = default;
    DaylightTimeStruct(DateTime start, DateTime end, TimeSpan delta) : Start(start), End(end), Delta(delta) {
    }
};
class TimeZoneInfo {
  public:
    TimeSpan BaseUtcOffset, _baseUtcOffset;
    std::vector<AdjustmentRule> _adjustmentRules;
    const DateTime s_maxDateOnly = DateTime(9999, 12, 31), s_minDateOnly = DateTime(1, 1, 2);
    TimeZoneInfo(std::int64_t base, std::vector<InvariantTimeZoneRule> rules)
        : BaseUtcOffset(base), _baseUtcOffset(base) {
        for (auto& r : rules)
            _adjustmentRules.push_back(std::make_shared<Rule>(r));
    }
    AdjustmentRule GetPreviousAdjustmentRule(AdjustmentRule rule, std::optional<int> ruleIndex) {

        if (ruleIndex.has_value() && 0 < ruleIndex.value_or(0) &&
            ruleIndex.value_or(0) < static_cast<int>(_adjustmentRules.size())) {
            return _adjustmentRules[ruleIndex.value_or(0) - 1];
        }

        AdjustmentRule result = rule;
        for (int i = 1; i < static_cast<int>(_adjustmentRules.size()); i++) {

            if (rule == _adjustmentRules[i]) {
                result = _adjustmentRules[i - 1];
                break;
            }
        }
        return result;
    }
    AdjustmentRule GetAdjustmentRuleForTime(DateTime dateTime, std::optional<int>& ruleIndex) {

        AdjustmentRule result = GetAdjustmentRuleForTime(dateTime, false, ruleIndex);

        return result;
    }
    AdjustmentRule GetAdjustmentRuleForTime(DateTime dateTime, bool dateTimeisUtc,
                                            std::optional<int>& ruleIndex) {

        if (static_cast<int>(_adjustmentRules.size()) == 0) {
            ruleIndex = std::nullopt;
            return {};
        }

        DateTime date = dateTimeisUtc ? (dateTime + BaseUtcOffset).Date() : dateTime.Date();

        int low = 0;
        int high = static_cast<int>(_adjustmentRules.size()) - 1;

        while (low <= high) {
            int median = low + ((high - low) >> 1);

            AdjustmentRule rule = _adjustmentRules[median];
            AdjustmentRule previousRule = median > 0 ? _adjustmentRules[median - 1] : rule;

            int compareResult =
                CompareAdjustmentRuleToDateTime(rule, previousRule, dateTime, date, dateTimeisUtc);
            if (compareResult == 0) {
                ruleIndex = median;
                return rule;
            } else if (compareResult < 0) {
                low = median + 1;
            } else {
                high = median - 1;
            }
        }

        ruleIndex = std::nullopt;
        return {};
    }
    int CompareAdjustmentRuleToDateTime(AdjustmentRule rule, AdjustmentRule previousRule, DateTime dateTime,
                                        DateTime dateOnly, bool dateTimeisUtc) {

        bool isAfterStart;
        if (rule->DateStart.Kind == 1) {
            DateTime dateTimeToCompare = dateTimeisUtc ? dateTime :

                                                       ConvertToUtc(dateTime, previousRule->DaylightDelta,
                                                                    previousRule->BaseUtcOffsetDelta);

            isAfterStart = dateTimeToCompare >= rule->DateStart;
        } else {

            isAfterStart = dateOnly >= rule->DateStart;
        }

        if (!isAfterStart) {
            return 1;
        }

        bool isBeforeEnd;
        if (rule->DateEnd.Kind == 1) {
            DateTime dateTimeToCompare =
                dateTimeisUtc ? dateTime
                              : ConvertToUtc(dateTime, rule->DaylightDelta, rule->BaseUtcOffsetDelta);

            isBeforeEnd = dateTimeToCompare <= rule->DateEnd;
        } else {

            isBeforeEnd = dateOnly <= rule->DateEnd;
        }

        return isBeforeEnd ? 0 : -1;
    }
    DateTime ConvertToFromUtc(DateTime dateTime, TimeSpan daylightDelta, TimeSpan baseUtcOffsetDelta,
                              bool convertToUtc) {

        TimeSpan offset = BaseUtcOffset + daylightDelta + baseUtcOffsetDelta;
        if (convertToUtc) {
            offset = offset.Negate();
        }

        std::int64_t ticks = dateTime.Ticks + offset.Ticks;

        return ticks > max_date.Ticks ? max_date : ticks < min_date.Ticks ? min_date : DateTime(ticks);
    }
    DaylightTimeStruct GetDaylightTime(int year, AdjustmentRule rule, std::optional<int> ruleIndex) {

        TimeSpan delta = rule->DaylightDelta;
        DateTime startTime;
        DateTime endTime;
        if (rule->NoDaylightTransitions) {

            AdjustmentRule previousRule = GetPreviousAdjustmentRule(rule, ruleIndex);
            startTime = ConvertFromUtc(rule->DateStart, previousRule->DaylightDelta,
                                       previousRule->BaseUtcOffsetDelta);

            endTime = ConvertFromUtc(rule->DateEnd, rule->DaylightDelta, rule->BaseUtcOffsetDelta);
        } else {
            startTime = TransitionTimeToDateTime(year, rule->DaylightTransitionStart);
            endTime = TransitionTimeToDateTime(year, rule->DaylightTransitionEnd);
        }
        return DaylightTimeStruct(startTime, endTime, delta);
    }
    bool GetIsDaylightSavings(DateTime time, AdjustmentRule rule, DaylightTimeStruct daylightTime) {

        if (!rule) {
            return false;
        }

        DateTime startTime;
        DateTime endTime;

        if (time.Kind == 2) {

            startTime = rule->IsStartDateMarkerForBeginningOfYear()
                            ? DateTime(daylightTime.Start.Year(), 1, 1, 0, 0, 0)
                            : daylightTime.Start + daylightTime.Delta;

            endTime = rule->IsEndDateMarkerForEndOfYear()
                          ? DateTime(daylightTime.End.Year() + 1, 1, 1, 0, 0, 0).AddTicks(-1)
                          : daylightTime.End;
        } else {

            bool invalidAtStart = rule->DaylightDelta > TimeSpan();

            startTime = rule->IsStartDateMarkerForBeginningOfYear()
                            ? DateTime(daylightTime.Start.Year(), 1, 1, 0, 0, 0)
                            : daylightTime.Start + (invalidAtStart ? rule->DaylightDelta : TimeSpan());

            endTime = rule->IsEndDateMarkerForEndOfYear()
                          ? DateTime(daylightTime.End.Year() + 1, 1, 1, 0, 0, 0).AddTicks(-1)
                          : daylightTime.End + (invalidAtStart ? -rule->DaylightDelta : TimeSpan());
        }

        bool isDst = CheckIsDst(startTime, time, endTime, false, rule);

        if (isDst && time.Kind == 2) {

            if (GetIsAmbiguousTime(time, rule, daylightTime)) {
                isDst = time.IsAmbiguousDaylightSavingTime();
            }
        }

        return isDst;
    }
    TimeSpan GetDaylightSavingsStartOffsetFromUtc(TimeSpan baseUtcOffset, AdjustmentRule rule,
                                                  std::optional<int> ruleIndex) {

        if (rule->NoDaylightTransitions) {

            AdjustmentRule previousRule = GetPreviousAdjustmentRule(rule, ruleIndex);
            return baseUtcOffset + previousRule->BaseUtcOffsetDelta + previousRule->DaylightDelta;
        } else {
            return baseUtcOffset + rule->BaseUtcOffsetDelta;
        }
    }
    TimeSpan GetDaylightSavingsEndOffsetFromUtc(TimeSpan baseUtcOffset, AdjustmentRule rule) {

        return baseUtcOffset + rule->BaseUtcOffsetDelta + rule->DaylightDelta;
    }
    bool GetIsDaylightSavingsFromUtc(DateTime time, int year, TimeSpan utc, AdjustmentRule rule,
                                     std::optional<int> ruleIndex, bool& isAmbiguousLocalDst,
                                     TimeZoneInfo& zone) {

        isAmbiguousLocalDst = false;

        if (!rule) {
            return false;
        }

        DaylightTimeStruct daylightTime = zone.GetDaylightTime(year, rule, ruleIndex);

        bool ignoreYearAdjustment = false;
        TimeSpan dstStartOffset = zone.GetDaylightSavingsStartOffsetFromUtc(utc, rule, ruleIndex);
        DateTime startTime;
        if (rule->IsStartDateMarkerForBeginningOfYear() && daylightTime.Start.Year() > min_date.Year()) {
            if (TryGetStartOfDstIfYearEndWithDst(daylightTime.Start.Year() - 1, utc, zone, startTime)) {
                ignoreYearAdjustment = true;
            } else {
                startTime = DateTime(daylightTime.Start.Year(), 1, 1, 0, 0, 0) - dstStartOffset;
            }
        } else {
            startTime = daylightTime.Start - dstStartOffset;
        }

        TimeSpan dstEndOffset = GetDaylightSavingsEndOffsetFromUtc(utc, rule);
        DateTime endTime;
        if (rule->IsEndDateMarkerForEndOfYear() && daylightTime.End.Year() < max_date.Year()) {
            if (TryGetEndOfDstIfYearStartWithDst(daylightTime.End.Year() + 1, utc, zone, endTime)) {
                ignoreYearAdjustment = true;
            } else {
                endTime = DateTime(daylightTime.End.Year() + 1, 1, 1, 0, 0, 0).AddTicks(-1) - dstEndOffset;
            }
        } else {
            endTime = daylightTime.End - dstEndOffset;
        }

        DateTime ambiguousStart;
        DateTime ambiguousEnd;
        if (daylightTime.Delta.Ticks > 0) {
            ambiguousStart = endTime - daylightTime.Delta;
            ambiguousEnd = endTime;
        } else {
            ambiguousStart = startTime;
            ambiguousEnd = startTime - daylightTime.Delta;
        }

        bool isDst = CheckIsDst(startTime, time, endTime, ignoreYearAdjustment, rule);

        if (isDst) {
            isAmbiguousLocalDst = (time >= ambiguousStart && time < ambiguousEnd);

            if (!isAmbiguousLocalDst && ambiguousStart.Year() != ambiguousEnd.Year()) {

                DateTime ambiguousStartModified;
                DateTime ambiguousEndModified;
                try {
                    ambiguousStartModified = ambiguousStart.AddYears(1);
                    ambiguousEndModified = ambiguousEnd.AddYears(1);
                    isAmbiguousLocalDst = (time >= ambiguousStartModified && time < ambiguousEndModified);
                } catch (const std::out_of_range&) {
                }

                if (!isAmbiguousLocalDst) {
                    try {
                        ambiguousStartModified = ambiguousStart.AddYears(-1);
                        ambiguousEndModified = ambiguousEnd.AddYears(-1);
                        isAmbiguousLocalDst = (time >= ambiguousStartModified && time < ambiguousEndModified);
                    } catch (const std::out_of_range&) {
                    }
                }
            }
        }

        return isDst;
    }
    bool TryGetEndOfDstIfYearStartWithDst(int nextYear, TimeSpan utc, TimeZoneInfo& zone, DateTime& dstEnd) {
        std::optional<int> nextYearRuleIndex{};

        AdjustmentRule nextYearRule =
            zone.GetAdjustmentRuleForTime(DateTime(nextYear, 1, 1), nextYearRuleIndex);

        if (!nextYearRule) {
            dstEnd = {};
            return false;
        }

        DaylightTimeStruct nextdaylightTime;

        if (nextYearRule->IsStartDateMarkerForBeginningOfYear()) {

            if (nextYearRule->IsEndDateMarkerForEndOfYear()) {
                dstEnd = DateTime(nextYear, 12, 31) - utc - nextYearRule->BaseUtcOffsetDelta -
                         nextYearRule->DaylightDelta;
            } else {

                nextdaylightTime = zone.GetDaylightTime(nextYear, nextYearRule, nextYearRuleIndex);
                dstEnd = nextdaylightTime.End - utc - nextYearRule->BaseUtcOffsetDelta -
                         nextYearRule->DaylightDelta;
            }

            return true;
        }

        nextdaylightTime = zone.GetDaylightTime(nextYear, nextYearRule, nextYearRuleIndex);

        if (nextdaylightTime.End < nextdaylightTime.Start && !nextYearRule->IsEndDateMarkerForEndOfYear()) {

            dstEnd =
                nextdaylightTime.End - utc - nextYearRule->BaseUtcOffsetDelta - nextYearRule->DaylightDelta;
            return true;
        }

        dstEnd = {};
        return false;
    }
    bool TryGetStartOfDstIfYearEndWithDst(int previousYear, TimeSpan utc, TimeZoneInfo& zone,
                                          DateTime& dstStart) {
        std::optional<int> previousYearRuleIndex{};

        AdjustmentRule previousYearRule =
            zone.GetAdjustmentRuleForTime(DateTime(previousYear, 12, 31), previousYearRuleIndex);

        if (!previousYearRule) {
            dstStart = {};
            return false;
        }

        DaylightTimeStruct previousDaylightTime =
            zone.GetDaylightTime(previousYear, previousYearRule, previousYearRuleIndex);

        if (previousYearRule->IsEndDateMarkerForEndOfYear() ||
            previousDaylightTime.Start > previousDaylightTime.End) {
            dstStart = previousDaylightTime.Start - utc - previousYearRule->BaseUtcOffsetDelta;
            return true;
        }

        dstStart = {};
        return false;
    }
    bool CheckIsDst(DateTime startTime, DateTime time, DateTime endTime, bool ignoreYearAdjustment,
                    AdjustmentRule rule) {

        if (!ignoreYearAdjustment && !rule->NoDaylightTransitions) {
            int startTimeYear = startTime.Year();
            int endTimeYear = endTime.Year();

            if (startTimeYear != endTimeYear) {
                endTime = endTime.AddYears(startTimeYear - endTimeYear);
            }

            int timeYear = time.Year();

            if (startTimeYear != timeYear) {
                time = time.AddYears(startTimeYear - timeYear);
            }
        }

        if (startTime > endTime) {

            return time < endTime || time >= startTime;
        } else if (rule->NoDaylightTransitions) {

            return time >= startTime && time <= endTime;
        } else {

            return time >= startTime && time < endTime;
        }
    }
    bool GetIsAmbiguousTime(DateTime time, AdjustmentRule rule, DaylightTimeStruct daylightTime) {

        bool isAmbiguous = false;
        if (!rule || rule->DaylightDelta == TimeSpan()) {
            return isAmbiguous;
        }

        DateTime startAmbiguousTime;
        DateTime endAmbiguousTime;

        if (rule->DaylightDelta > TimeSpan()) {
            if (rule->IsEndDateMarkerForEndOfYear()) {
                return false;
            }
            startAmbiguousTime = daylightTime.End;
            endAmbiguousTime = daylightTime.End - rule->DaylightDelta;
        } else {
            if (rule->IsStartDateMarkerForBeginningOfYear()) {
                return false;
            }
            startAmbiguousTime = daylightTime.Start;
            endAmbiguousTime = daylightTime.Start + rule->DaylightDelta;
        }

        isAmbiguous = (time >= endAmbiguousTime && time < startAmbiguousTime);

        if (!isAmbiguous && startAmbiguousTime.Year() != endAmbiguousTime.Year()) {

            DateTime startModifiedAmbiguousTime;
            DateTime endModifiedAmbiguousTime;
            try {
                startModifiedAmbiguousTime = startAmbiguousTime.AddYears(1);
                endModifiedAmbiguousTime = endAmbiguousTime.AddYears(1);
                isAmbiguous = (time >= endModifiedAmbiguousTime && time < startModifiedAmbiguousTime);
            } catch (const std::out_of_range&) {
            }

            if (!isAmbiguous) {
                try {
                    startModifiedAmbiguousTime = startAmbiguousTime.AddYears(-1);
                    endModifiedAmbiguousTime = endAmbiguousTime.AddYears(-1);
                    isAmbiguous = (time >= endModifiedAmbiguousTime && time < startModifiedAmbiguousTime);
                } catch (const std::out_of_range&) {
                }
            }
        }
        return isAmbiguous;
    }
    bool GetIsInvalidTime(DateTime time, AdjustmentRule rule, DaylightTimeStruct daylightTime) {

        bool isInvalid = false;
        if (!rule || rule->DaylightDelta == TimeSpan()) {
            return isInvalid;
        }

        DateTime startInvalidTime;
        DateTime endInvalidTime;

        if (rule->DaylightDelta < TimeSpan()) {

            if (rule->IsEndDateMarkerForEndOfYear())
                return false;

            startInvalidTime = daylightTime.End;
            endInvalidTime = daylightTime.End - rule->DaylightDelta;
        } else {

            if (rule->IsStartDateMarkerForBeginningOfYear())
                return false;

            startInvalidTime = daylightTime.Start;
            endInvalidTime = daylightTime.Start + rule->DaylightDelta;
        }

        isInvalid = (time >= startInvalidTime && time < endInvalidTime);

        if (!isInvalid && startInvalidTime.Year() != endInvalidTime.Year()) {

            DateTime startModifiedInvalidTime;
            DateTime endModifiedInvalidTime;
            try {
                startModifiedInvalidTime = startInvalidTime.AddYears(1);
                endModifiedInvalidTime = endInvalidTime.AddYears(1);
                isInvalid = (time >= startModifiedInvalidTime && time < endModifiedInvalidTime);
            } catch (const std::out_of_range&) {
            }

            if (!isInvalid) {
                try {
                    startModifiedInvalidTime = startInvalidTime.AddYears(-1);
                    endModifiedInvalidTime = endInvalidTime.AddYears(-1);
                    isInvalid = (time >= startModifiedInvalidTime && time < endModifiedInvalidTime);
                } catch (const std::out_of_range&) {
                }
            }
        }
        return isInvalid;
    }
    TimeSpan GetUtcOffset(DateTime time, TimeZoneInfo& zone) {
        std::optional<int> ruleIndex{};

        TimeSpan baseOffset = zone.BaseUtcOffset;
        AdjustmentRule rule = zone.GetAdjustmentRuleForTime(time, ruleIndex);

        if (bool(rule)) {
            baseOffset += rule->BaseUtcOffsetDelta;
            if (rule->HasDaylightSaving) {
                DaylightTimeStruct daylightTime = zone.GetDaylightTime(time.Year(), rule, ruleIndex);
                bool isDaylightSavings = GetIsDaylightSavings(time, rule, daylightTime);
                baseOffset += (isDaylightSavings ? rule->DaylightDelta : TimeSpan());
            }
        }

        return baseOffset;
    }
    TimeSpan GetUtcOffsetFromUtc(DateTime time, TimeZoneInfo& zone, bool& isDaylightSavings,
                                 bool& isAmbiguousLocalDst) {

        isDaylightSavings = false;
        isAmbiguousLocalDst = false;
        TimeSpan baseOffset = zone.BaseUtcOffset;
        int year;
        std::optional<int> ruleIndex;
        AdjustmentRule rule;

        if (time > s_maxDateOnly) {
            rule = zone.GetAdjustmentRuleForTime(max_date, ruleIndex);
            year = 9999;
        } else if (time < s_minDateOnly) {
            rule = zone.GetAdjustmentRuleForTime(min_date, ruleIndex);
            year = 1;
        } else {
            rule = zone.GetAdjustmentRuleForTime(time, true, ruleIndex);

            DateTime targetTime = time + baseOffset;
            year = targetTime.Year();
        }

        if (bool(rule)) {
            baseOffset += rule->BaseUtcOffsetDelta;
            if (rule->HasDaylightSaving) {
                isDaylightSavings = GetIsDaylightSavingsFromUtc(time, year, zone._baseUtcOffset, rule,
                                                                ruleIndex, isAmbiguousLocalDst, zone);
                baseOffset += (isDaylightSavings ? rule->DaylightDelta : TimeSpan());
            }
        }

        return baseOffset;
    }
    DateTime TransitionTimeToDateTime(int year, TransitionTime transitionTime) {

        DateTime value;
        TimeSpan timeOfDay = transitionTime.TimeOfDay.TimeOfDay();

        if (transitionTime.IsFixedDateRule) {

            int day = transitionTime.Day;

            if (day > 28) {
                int daysInMonth = DateTime::DaysInMonth(year, transitionTime.Month);
                if (day > daysInMonth) {
                    day = daysInMonth;
                }
            }

            value = DateTime(year, transitionTime.Month, day) + timeOfDay;
        } else {
            if (transitionTime.Week <= 4) {

                value = DateTime(year, transitionTime.Month, 1) + timeOfDay;

                int dayOfWeek = (int)value.DayOfWeek();
                int delta = (int)transitionTime.DayOfWeek - dayOfWeek;
                if (delta < 0) {
                    delta += 7;
                }
                delta += 7 * (transitionTime.Week - 1);

                if (delta > 0) {
                    value = value.AddDays(delta);
                }
            } else {

                int daysInMonth = DateTime::DaysInMonth(year, transitionTime.Month);
                value = DateTime(year, transitionTime.Month, daysInMonth) + timeOfDay;

                int dayOfWeek = (int)value.DayOfWeek();
                int delta = dayOfWeek - (int)transitionTime.DayOfWeek;
                if (delta < 0) {
                    delta += 7;
                }

                if (delta > 0) {
                    value = value.AddDays(-delta);
                }
            }
        }
        return value;
    }
    DateTime ConvertToUtc(DateTime d, TimeSpan daylight, TimeSpan base) {
        return ConvertToFromUtc(d, daylight, base, true);
    }
    DateTime ConvertFromUtc(DateTime d, TimeSpan daylight, TimeSpan base) {
        return ConvertToFromUtc(d, daylight, base, false);
    }
};
bool Rule::IsStartDateMarkerForBeginningOfYear() const {
    return !NoDaylightTransitions && DaylightTransitionStart.Month == 1 && DaylightTransitionStart.Day == 1 &&
           DaylightTransitionStart.TimeOfDay.TimeOfDay().Ticks < 10000000;
}
bool Rule::IsEndDateMarkerForEndOfYear() const {
    return !NoDaylightTransitions && DaylightTransitionEnd.Month == 1 && DaylightTransitionEnd.Day == 1 &&
           DaylightTransitionEnd.TimeOfDay.TimeOfDay().Ticks < 10000000;
}
}
struct InvariantTimeZoneRules::State {
    mutable TimeZoneInfo zone;
    State(std::int64_t offset, std::vector<InvariantTimeZoneRule> rules) : zone(offset, std::move(rules)) {
    }
};
InvariantTimeZoneRules::InvariantTimeZoneRules(std::int64_t offset, std::vector<InvariantTimeZoneRule> rules)
    : state_(std::make_shared<State>(offset, std::move(rules))) {
}
bool InvariantTimeZoneRules::utc_is_daylight(std::int64_t ticks) const {
    bool daylight = false, ambiguous = false;
    auto& zone = state_->zone;
    zone.GetUtcOffsetFromUtc(DateTime(ticks), zone, daylight, ambiguous);
    return daylight;
}
InvariantDateTimeParseContext::UtcOffset InvariantTimeZoneRules::utc_offset(std::int64_t ticks) const {
    bool daylight = false, ambiguous = false;
    auto& z = state_->zone;
    return {z.GetUtcOffsetFromUtc(DateTime(ticks), z, daylight, ambiguous).Ticks, ambiguous};
}
InvariantDateTimeParseContext::UtcOffset InvariantTimeZoneRules::current_year_offset(
    std::int64_t ticks,
    std::function<std::shared_ptr<const InvariantTimeZoneRules>()> historical_local) const {
    auto& z = state_->zone;
    DateTime time(ticks);
    bool ambiguous = false;
    auto offset = z.BaseUtcOffset;
    if (!z._adjustmentRules.empty()) {
        auto r = z._adjustmentRules[0];
        offset += r->BaseUtcOffsetDelta;
        if (r->HasDaylightSaving) {
            auto local = historical_local();
            auto& historical = local->state_->zone;
            if (historical.GetIsDaylightSavingsFromUtc(time, time.Year(), z.BaseUtcOffset, r, std::nullopt,
                                                       ambiguous, historical))
                offset += r->DaylightDelta;
        }
    }
    return {offset.Ticks, ambiguous};
}
std::int64_t InvariantTimeZoneRules::local_offset_no_throw(AssetDateTime date) const {
    auto& z = state_->zone;
    if (date.kind() == 1)
        return utc_offset(date.ticks()).ticks;
    return z.GetUtcOffset(DateTime(date), z).Ticks;
}
}
