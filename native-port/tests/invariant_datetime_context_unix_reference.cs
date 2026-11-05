// Source-backed Unix ingestion simulation; not actual Unix runtime execution.
// Exact consumed method bodies from dotnet/runtime v8.0.31. .NET Foundation MIT.
// See third_party/dotnet_collation/LICENSE. Class identity and dependency providers
// are isolated to compile on Windows; original methods below remain unchanged.
using System;using System.Buffers.Binary;using System.Collections.Generic;using System.Diagnostics;using System.Diagnostics.CodeAnalysis;using System.Globalization;using System.Text;using System.Runtime.Serialization;
using TransitionTime=System.TimeZoneInfo.TransitionTime;
using Diagnostics=System.Diagnostics;
static class SerializationSourceDependency{public static object? GetValueNoThrow(this SerializationInfo info,string name,Type type)=>typeof(SerializationInfo).GetMethod("GetValueNoThrow",System.Reflection.BindingFlags.Instance|System.Reflection.BindingFlags.NonPublic)!.Invoke(info,new object[]{name,type});}
sealed class UnixSourceTimeZone {
string _id="";bool HasIanaId,_supportsDaylightSavingTime;TimeSpan _baseUtcOffset;AdjustmentRule[]? _adjustmentRules;string? _standardAbbrevName,_daylightAbbrevName;
static readonly TimeSpan MaxOffset=TimeSpan.FromHours(14),MinOffset=-MaxOffset;
static bool UtcOffsetOutOfRange(TimeSpan value)=>value<MinOffset||value>MaxOffset;
static readonly TransitionTime s_daylightRuleMarker=TransitionTime.CreateFixedDateRule(DateTime.MinValue.AddMilliseconds(2),1,1);
static class GregorianCalendar{public static readonly int[] DaysToMonth365={0,31,59,90,120,151,181,212,243,273,304,334,365};}
static class DateTimeOffset{public const long UnixMinSeconds=-62135596800,UnixMaxSeconds=253402300799;public static System.DateTimeOffset FromUnixTimeSeconds(long value)=>System.DateTimeOffset.FromUnixTimeSeconds(value);}
static class SR{public static string Format(string text,params object[] args)=>text;public const string ArgumentOutOfRange_UtcOffset="ArgumentOutOfRange_UtcOffset";public const string ArgumentOutOfRange_UtcOffsetAndDaylightDelta="ArgumentOutOfRange_UtcOffsetAndDaylightDelta";public const string Argument_AdjustmentRulesNoNulls="Argument_AdjustmentRulesNoNulls";public const string Argument_AdjustmentRulesOutOfOrder="Argument_AdjustmentRulesOutOfOrder";public const string Argument_DateTimeHasTimeOfDay="Argument_DateTimeHasTimeOfDay";public const string Argument_DateTimeKindMustBeUnspecifiedOrUtc="Argument_DateTimeKindMustBeUnspecifiedOrUtc";public const string Argument_OutOfOrderDateTimes="Argument_OutOfOrderDateTimes";public const string Argument_TimeSpanHasSeconds="Argument_TimeSpanHasSeconds";public const string Argument_TimeZoneInfoBadTZif="Argument_TimeZoneInfoBadTZif";public const string Argument_TimeZoneInfoInvalidTZif="Argument_TimeZoneInfoInvalidTZif";public const string Argument_TransitionTimesAreIdentical="Argument_TransitionTimesAreIdentical";public const string InvalidTimeZone_InvalidJulianDay="InvalidTimeZone_InvalidJulianDay";public const string InvalidTimeZone_NoTTInfoStructures="InvalidTimeZone_NoTTInfoStructures";public const string InvalidTimeZone_UnparsablePosixMDateString="InvalidTimeZone_UnparsablePosixMDateString";public const string Serialization_InvalidData="Serialization_InvalidData";}
private static bool IsUtcAlias (string id)
        {
            switch ((ushort)id[0])
            {
                case 69: // e
                case 101: // E
                    return string.Equals(id, "Etc/UTC", StringComparison.OrdinalIgnoreCase) ||
                           string.Equals(id, "Etc/UCT", StringComparison.OrdinalIgnoreCase) ||
                           string.Equals(id, "Etc/Universal", StringComparison.OrdinalIgnoreCase) ||
                           string.Equals(id, "Etc/Zulu", StringComparison.OrdinalIgnoreCase);
                case 85: // u
                case 117: // U
                    return string.Equals(id, "UCT", StringComparison.OrdinalIgnoreCase) ||
                           string.Equals(id, "UTC", StringComparison.OrdinalIgnoreCase) ||
                           string.Equals(id, "Universal", StringComparison.OrdinalIgnoreCase);
                case 90: // z
                case 122: // Z
                    return string.Equals(id, "Zulu", StringComparison.OrdinalIgnoreCase);
            }

            return false;
        }
private UnixSourceTimeZone(byte[] data, string id, bool dstDisabled)
        {
            _id = id;

            HasIanaId = true;

            if (IsUtcAlias(id))
            {
                _baseUtcOffset = TimeSpan.Zero;
                _adjustmentRules = Array.Empty<AdjustmentRule>();
                return;
            }

            DateTime[] dts;
            byte[] typeOfLocalTime;
            TZifType[] transitionType;
            string zoneAbbreviations;
            string? futureTransitionsPosixFormat;

            // parse the raw TZif bytes; this method can throw ArgumentException when the data is malformed.
            TZif_ParseRaw(data, out dts, out typeOfLocalTime, out transitionType, out zoneAbbreviations, out futureTransitionsPosixFormat);

            // find the best matching baseUtcOffset and display strings based on the current utcNow value.
            // NOTE: read the Standard and Daylight display strings from the tzfile now in case they can't be loaded later
            // from the globalization data.
            DateTime utcNow = DateTime.UtcNow;
            for (int i = 0; i < dts.Length && dts[i] <= utcNow; i++)
            {
                int type = typeOfLocalTime[i];
                if (!transitionType[type].IsDst)
                {
                    _baseUtcOffset = transitionType[type].UtcOffset;
                    _standardAbbrevName = TZif_GetZoneAbbreviation(zoneAbbreviations, transitionType[type].AbbreviationIndex);
                }
                else
                {
                    _daylightAbbrevName = TZif_GetZoneAbbreviation(zoneAbbreviations, transitionType[type].AbbreviationIndex);
                }
            }

            if (dts.Length == 0)
            {
                // time zones like Africa/Bujumbura and Etc/GMT* have no transition times but still contain
                // TZifType entries that may contain a baseUtcOffset and display strings
                for (int i = 0; i < transitionType.Length; i++)
                {
                    if (!transitionType[i].IsDst)
                    {
                        _baseUtcOffset = transitionType[i].UtcOffset;
                        _standardAbbrevName = TZif_GetZoneAbbreviation(zoneAbbreviations, transitionType[i].AbbreviationIndex);
                    }
                    else
                    {
                        _daylightAbbrevName = TZif_GetZoneAbbreviation(zoneAbbreviations, transitionType[i].AbbreviationIndex);
                    }
                }
            }

            // TZif supports seconds-level granularity with offsets but TimeZoneInfo only supports minutes since it aligns
            // with DateTimeOffset, SQL Server, and the W3C XML Specification
            if (_baseUtcOffset.Ticks % TimeSpan.TicksPerMinute != 0)
            {
                _baseUtcOffset = new TimeSpan(_baseUtcOffset.Hours, _baseUtcOffset.Minutes, 0);
            }

            if (!dstDisabled)
            {
                // only create the adjustment rule if DST is enabled
                TZif_GenerateAdjustmentRules(out _adjustmentRules, _baseUtcOffset, dts, typeOfLocalTime, transitionType, futureTransitionsPosixFormat);
            }

            ValidateTimeZoneInfo(_id, _baseUtcOffset, _adjustmentRules, out _supportsDaylightSavingTime);
        }
private static void TZif_GenerateAdjustmentRules(out AdjustmentRule[]? rules, TimeSpan baseUtcOffset, DateTime[] dts, byte[] typeOfLocalTime,
            TZifType[] transitionType, string? futureTransitionsPosixFormat)
        {
            rules = null;

            if (dts.Length > 0)
            {
                int index = 0;
                List<AdjustmentRule> rulesList = new List<AdjustmentRule>();

                while (index <= dts.Length)
                {
                    TZif_GenerateAdjustmentRule(ref index, baseUtcOffset, rulesList, dts, typeOfLocalTime, transitionType, futureTransitionsPosixFormat);
                }

                rules = rulesList.ToArray();
                if (rules != null && rules.Length == 0)
                {
                    rules = null;
                }
            }
        }
private static void TZif_GenerateAdjustmentRule(ref int index, TimeSpan timeZoneBaseUtcOffset, List<AdjustmentRule> rulesList, DateTime[] dts,
            byte[] typeOfLocalTime, TZifType[] transitionTypes, string? futureTransitionsPosixFormat)
        {
            // To generate AdjustmentRules, use the following approach:
            // The first AdjustmentRule will go from DateTime.MinValue to the first transition time greater than DateTime.MinValue.
            // Each middle AdjustmentRule wil go from dts[index-1] to dts[index].
            // The last AdjustmentRule will go from dts[dts.Length-1] to Datetime.MaxValue.

            // 0. Skip any DateTime.MinValue transition times. In newer versions of the tzfile, there
            // is a "big bang" transition time, which is before the year 0001. Since any times before year 0001
            // cannot be represented by DateTime, there is no reason to make AdjustmentRules for these unrepresentable time periods.
            // 1. If there are no DateTime.MinValue times, the first AdjustmentRule goes from DateTime.MinValue
            // to the first transition and uses the first standard transitionType (or the first transitionType if none of them are standard)
            // 2. Create an AdjustmentRule for each transition, i.e. from dts[index - 1] to dts[index].
            // This rule uses the transitionType[index - 1] and the whole AdjustmentRule only describes a single offset - either
            // all daylight savings, or all standard time.
            // 3. After all the transitions are filled out, the last AdjustmentRule is created from either:
            //   a. a POSIX-style timezone description ("futureTransitionsPosixFormat"), if there is one or
            //   b. continue the last transition offset until DateTime.Max

            while (index < dts.Length && dts[index] == DateTime.MinValue)
            {
                index++;
            }

            if (rulesList.Count == 0 && index < dts.Length)
            {
                TZifType transitionType = TZif_GetEarlyDateTransitionType(transitionTypes);
                DateTime endTransitionDate = dts[index];

                TimeSpan transitionOffset = TZif_CalculateTransitionOffsetFromBase(transitionType.UtcOffset, timeZoneBaseUtcOffset);
                TimeSpan daylightDelta = transitionType.IsDst ? transitionOffset : TimeSpan.Zero;
                TimeSpan baseUtcDelta = transitionType.IsDst ? TimeSpan.Zero : transitionOffset;

                AdjustmentRule r = AdjustmentRule.CreateAdjustmentRule(
                        DateTime.MinValue,
                        endTransitionDate.AddTicks(-1),
                        daylightDelta,
                        default,
                        default,
                        baseUtcDelta,
                        noDaylightTransitions: true);

                if (!IsValidAdjustmentRuleOffset(timeZoneBaseUtcOffset, r))
                {
                    NormalizeAdjustmentRuleOffset(timeZoneBaseUtcOffset, ref r);
                }

                rulesList.Add(r);
            }
            else if (index < dts.Length)
            {
                DateTime startTransitionDate = dts[index - 1];
                TZifType startTransitionType = transitionTypes[typeOfLocalTime[index - 1]];

                DateTime endTransitionDate = dts[index];

                TimeSpan transitionOffset = TZif_CalculateTransitionOffsetFromBase(startTransitionType.UtcOffset, timeZoneBaseUtcOffset);
                TimeSpan daylightDelta = startTransitionType.IsDst ? transitionOffset : TimeSpan.Zero;
                TimeSpan baseUtcDelta = startTransitionType.IsDst ? TimeSpan.Zero : transitionOffset;

                TransitionTime dstStart;
                if (startTransitionType.IsDst)
                {
                    // the TransitionTime fields are not used when AdjustmentRule.NoDaylightTransitions == true.
                    // However, there are some cases in the past where DST = true, and the daylight savings offset
                    // now equals what the current BaseUtcOffset is.  In that case, the AdjustmentRule.DaylightOffset
                    // is going to be TimeSpan.Zero.  But we still need to return 'true' from AdjustmentRule.HasDaylightSaving.
                    // To ensure we always return true from HasDaylightSaving, make a "special" dstStart that will make the logic
                    // in HasDaylightSaving return true.
                    dstStart = s_daylightRuleMarker;
                }
                else
                {
                    dstStart = default;
                }

                AdjustmentRule r = AdjustmentRule.CreateAdjustmentRule(
                        startTransitionDate,
                        endTransitionDate.AddTicks(-1),
                        daylightDelta,
                        dstStart,
                        default,
                        baseUtcDelta,
                        noDaylightTransitions: true);

                if (!IsValidAdjustmentRuleOffset(timeZoneBaseUtcOffset, r))
                {
                    NormalizeAdjustmentRuleOffset(timeZoneBaseUtcOffset, ref r);
                }

                rulesList.Add(r);
            }
            else
            {
                // create the AdjustmentRule that will be used for all DateTimes after the last transition

                // NOTE: index == dts.Length
                DateTime startTransitionDate = dts[index - 1];

                AdjustmentRule? r = !string.IsNullOrEmpty(futureTransitionsPosixFormat) ?
                    TZif_CreateAdjustmentRuleForPosixFormat(futureTransitionsPosixFormat, startTransitionDate, timeZoneBaseUtcOffset) :
                    null;

                if (r == null)
                {
                    // just use the last transition as the rule which will be used until the end of time

                    TZifType transitionType = transitionTypes[typeOfLocalTime[index - 1]];
                    TimeSpan transitionOffset = TZif_CalculateTransitionOffsetFromBase(transitionType.UtcOffset, timeZoneBaseUtcOffset);
                    TimeSpan daylightDelta = transitionType.IsDst ? transitionOffset : TimeSpan.Zero;
                    TimeSpan baseUtcDelta = transitionType.IsDst ? TimeSpan.Zero : transitionOffset;

                    r = AdjustmentRule.CreateAdjustmentRule(
                        startTransitionDate,
                        DateTime.MaxValue,
                        daylightDelta,
                        default,
                        default,
                        baseUtcDelta,
                        noDaylightTransitions: true);
                }

                if (!IsValidAdjustmentRuleOffset(timeZoneBaseUtcOffset, r))
                {
                    NormalizeAdjustmentRuleOffset(timeZoneBaseUtcOffset, ref r);
                }

                rulesList.Add(r);
            }

            index++;
        }
private static TimeSpan TZif_CalculateTransitionOffsetFromBase(TimeSpan transitionOffset, TimeSpan timeZoneBaseUtcOffset)
        {
            TimeSpan result = transitionOffset - timeZoneBaseUtcOffset;

            // TZif supports seconds-level granularity with offsets but TimeZoneInfo only supports minutes since it aligns
            // with DateTimeOffset, SQL Server, and the W3C XML Specification
            if (result.Ticks % TimeSpan.TicksPerMinute != 0)
            {
                result = new TimeSpan(result.Hours, result.Minutes, 0);
            }

            return result;
        }
private static TZifType TZif_GetEarlyDateTransitionType(TZifType[] transitionTypes)
        {
            foreach (TZifType transitionType in transitionTypes)
            {
                if (!transitionType.IsDst)
                {
                    return transitionType;
                }
            }

            if (transitionTypes.Length > 0)
            {
                return transitionTypes[0];
            }

            throw new InvalidTimeZoneException(SR.InvalidTimeZone_NoTTInfoStructures);
        }
private static AdjustmentRule? TZif_CreateAdjustmentRuleForPosixFormat(string posixFormat, DateTime startTransitionDate, TimeSpan timeZoneBaseUtcOffset)
        {
            if (TZif_ParsePosixFormat(posixFormat,
                out _,
                out ReadOnlySpan<char> standardOffset,
                out ReadOnlySpan<char> daylightSavingsName,
                out ReadOnlySpan<char> daylightSavingsOffset,
                out ReadOnlySpan<char> start,
                out ReadOnlySpan<char> startTime,
                out ReadOnlySpan<char> end,
                out ReadOnlySpan<char> endTime))
            {
                // a valid posixFormat has at least standardName and standardOffset

                TimeSpan? parsedBaseOffset = TZif_ParseOffsetString(standardOffset);
                if (parsedBaseOffset.HasValue)
                {
                    TimeSpan baseOffset = parsedBaseOffset.GetValueOrDefault().Negate(); // offsets are backwards in POSIX notation
                    baseOffset = TZif_CalculateTransitionOffsetFromBase(baseOffset, timeZoneBaseUtcOffset);

                    // having a daylightSavingsName means there is a DST rule
                    if (!daylightSavingsName.IsEmpty)
                    {
                        TimeSpan? parsedDaylightSavings = TZif_ParseOffsetString(daylightSavingsOffset);
                        TimeSpan daylightSavingsTimeSpan;
                        if (!parsedDaylightSavings.HasValue)
                        {
                            // default DST to 1 hour if it isn't specified
                            daylightSavingsTimeSpan = new TimeSpan(1, 0, 0);
                        }
                        else
                        {
                            daylightSavingsTimeSpan = parsedDaylightSavings.GetValueOrDefault().Negate(); // offsets are backwards in POSIX notation
                            daylightSavingsTimeSpan = TZif_CalculateTransitionOffsetFromBase(daylightSavingsTimeSpan, timeZoneBaseUtcOffset);
                            daylightSavingsTimeSpan = TZif_CalculateTransitionOffsetFromBase(daylightSavingsTimeSpan, baseOffset);
                        }

                        TransitionTime? dstStart = TZif_CreateTransitionTimeFromPosixRule(start, startTime);
                        TransitionTime? dstEnd = TZif_CreateTransitionTimeFromPosixRule(end, endTime);

                        if (dstStart == null || dstEnd == null)
                        {
                            return null;
                        }

                        return AdjustmentRule.CreateAdjustmentRule(
                            startTransitionDate,
                            DateTime.MaxValue,
                            daylightSavingsTimeSpan,
                            dstStart.GetValueOrDefault(),
                            dstEnd.GetValueOrDefault(),
                            baseOffset,
                            noDaylightTransitions: false);
                    }
                    else
                    {
                        // if there is no daylightSavingsName, the whole AdjustmentRule should be with no transitions - just the baseOffset
                        return AdjustmentRule.CreateAdjustmentRule(
                               startTransitionDate,
                               DateTime.MaxValue,
                               TimeSpan.Zero,
                               default,
                               default,
                               baseOffset,
                               noDaylightTransitions: true);
                    }
                }
            }

            return null;
        }
private static TimeSpan? TZif_ParseOffsetString(ReadOnlySpan<char> offset)
        {
            TimeSpan? result = null;

            if (offset.Length > 0)
            {
                bool negative = offset[0] == '-';
                if (negative || offset[0] == '+')
                {
                    offset = offset.Slice(1);
                }

                // Try parsing just hours first.
                // Note, TimeSpan.TryParseExact "%h" can't be used here because some time zones using values
                // like "26" or "144" and TimeSpan parsing would turn that into 26 or 144 *days* instead of hours.
                int hours;
                if (int.TryParse(offset, out hours))
                {
                    result = new TimeSpan(hours, 0, 0);
                }
                else
                {
                    TimeSpan parsedTimeSpan;
                    if (TimeSpan.TryParseExact(offset, "g", CultureInfo.InvariantCulture, out parsedTimeSpan))
                    {
                        result = parsedTimeSpan;
                    }
                }

                if (result.HasValue && negative)
                {
                    result = result.GetValueOrDefault().Negate();
                }
            }

            return result;
        }
private static DateTime ParseTimeOfDay(ReadOnlySpan<char> time)
        {
            DateTime timeOfDay;
            TimeSpan? timeOffset = TZif_ParseOffsetString(time);
            if (timeOffset.HasValue)
            {
                // This logic isn't correct and can't be corrected until https://github.com/dotnet/runtime/issues/14966 is fixed.
                // Some time zones use time values like, "26", "144", or "-2".
                // This allows the week to sometimes be week 4 and sometimes week 5 in the month.
                // For now, strip off any 'days' in the offset, and just get the time of day correct
                timeOffset = new TimeSpan(timeOffset.GetValueOrDefault().Hours, timeOffset.GetValueOrDefault().Minutes, timeOffset.GetValueOrDefault().Seconds);
                if (timeOffset.GetValueOrDefault() < TimeSpan.Zero)
                {
                    timeOfDay = new DateTime(1, 1, 2, 0, 0, 0);
                }
                else
                {
                    timeOfDay = new DateTime(1, 1, 1, 0, 0, 0);
                }

                timeOfDay += timeOffset.GetValueOrDefault();
            }
            else
            {
                // default to 2AM.
                timeOfDay = new DateTime(1, 1, 1, 2, 0, 0);
            }

            return timeOfDay;
        }
private static TransitionTime? TZif_CreateTransitionTimeFromPosixRule(ReadOnlySpan<char> date, ReadOnlySpan<char> time)
        {
            if (date.IsEmpty)
            {
                return null;
            }

            if (date[0] == 'M')
            {
                // Mm.w.d
                // This specifies day d of week w of month m. The day d must be between 0(Sunday) and 6.The week w must be between 1 and 5;
                // week 1 is the first week in which day d occurs, and week 5 specifies the last d day in the month. The month m should be between 1 and 12.

                int month;
                int week;
                DayOfWeek day;
                if (!TZif_ParseMDateRule(date, out month, out week, out day))
                {
                    throw new InvalidTimeZoneException(SR.Format(SR.InvalidTimeZone_UnparsablePosixMDateString, date.ToString()));
                }

                return TransitionTime.CreateFloatingDateRule(ParseTimeOfDay(time), month, week, day);
            }
            else
            {
                if (date[0] != 'J')
                {
                    // should be n Julian day format.
                    // This specifies the Julian day, with n between 0 and 365. February 29 is counted in leap years.
                    //
                    // n would be a relative number from the beginning of the year. which should handle if the
                    // the year is a leap year or not.
                    //
                    // In leap year, n would be counted as:
                    //
                    // 0                30 31              59 60              90      335            365
                    // |-------Jan--------|-------Feb--------|-------Mar--------|....|-------Dec--------|
                    //
                    // while in non leap year we'll have
                    //
                    // 0                30 31              58 59              89      334            364
                    // |-------Jan--------|-------Feb--------|-------Mar--------|....|-------Dec--------|
                    //
                    // For example if n is specified as 60, this means in leap year the rule will start at Mar 1,
                    // while in non leap year the rule will start at Mar 2.
                    //
                    // This n Julian day format is very uncommon and mostly  used for convenience to specify dates like January 1st
                    // which we can support without any major modification to the Adjustment rules. We'll support this rule  for day
                    // numbers less than 59 (up to Feb 28). Otherwise we'll skip this POSIX rule.
                    // We've never encountered any time zone file using this format for days beyond Feb 28.

                    if (int.TryParse(date, out int julianDay) && julianDay < 59)
                    {
                        int d, m;
                        if (julianDay <= 30) // January
                        {
                            m = 1;
                            d = julianDay + 1;
                        }
                        else // February
                        {
                            m = 2;
                            d = julianDay - 30;
                        }

                        return TransitionTime.CreateFixedDateRule(ParseTimeOfDay(time), m, d);
                    }

                    // Since we can't support this rule, return null to indicate to skip the POSIX rule.
                    return null;
                }

                // Julian day
                TZif_ParseJulianDay(date, out int month, out int day);
                return TransitionTime.CreateFixedDateRule(ParseTimeOfDay(time), month, day);
            }
        }
private static void TZif_ParseJulianDay(ReadOnlySpan<char> date, out int month, out int day)
        {
            // Jn
            // This specifies the Julian day, with n between 1 and 365.February 29 is never counted, even in leap years.
            Debug.Assert(!date.IsEmpty);
            Debug.Assert(date[0] == 'J');
            month = day = 0;

            int index = 1;

            if ((uint)index >= (uint)date.Length || !char.IsAsciiDigit(date[index]))
            {
                throw new InvalidTimeZoneException(SR.InvalidTimeZone_InvalidJulianDay);
            }

            int julianDay = 0;

            do
            {
                julianDay = julianDay * 10 + (int)(date[index] - '0');
                index++;
            } while ((uint)index < (uint)date.Length && char.IsAsciiDigit(date[index]));

            ReadOnlySpan<int> days = GregorianCalendar.DaysToMonth365;

            if (julianDay == 0 || julianDay > days[days.Length - 1])
            {
                throw new InvalidTimeZoneException(SR.InvalidTimeZone_InvalidJulianDay);
            }

            int i = 1;
            while (i < days.Length && julianDay > days[i])
            {
                i++;
            }

            Debug.Assert(i > 0 && i < days.Length);

            month = i;
            day = julianDay - days[i - 1];
        }
private static bool TZif_ParseMDateRule(ReadOnlySpan<char> dateRule, out int month, out int week, out DayOfWeek dayOfWeek)
        {
            if (dateRule[0] == 'M')
            {
                int monthWeekDotIndex = dateRule.IndexOf('.');
                if (monthWeekDotIndex > 0)
                {
                    ReadOnlySpan<char> weekDaySpan = dateRule.Slice(monthWeekDotIndex + 1);
                    int weekDayDotIndex = weekDaySpan.IndexOf('.');
                    if (weekDayDotIndex > 0)
                    {
                        if (int.TryParse(dateRule.Slice(1, monthWeekDotIndex - 1), out month) &&
                            int.TryParse(weekDaySpan.Slice(0, weekDayDotIndex), out week) &&
                            int.TryParse(weekDaySpan.Slice(weekDayDotIndex + 1), out int day))
                        {
                            dayOfWeek = (DayOfWeek)day;
                            return true;
                        }
                    }
                }
            }

            month = 0;
            week = 0;
            dayOfWeek = default;
            return false;
        }
private static bool TZif_ParsePosixFormat(
            ReadOnlySpan<char> posixFormat,
            out ReadOnlySpan<char> standardName,
            out ReadOnlySpan<char> standardOffset,
            out ReadOnlySpan<char> daylightSavingsName,
            out ReadOnlySpan<char> daylightSavingsOffset,
            out ReadOnlySpan<char> start,
            out ReadOnlySpan<char> startTime,
            out ReadOnlySpan<char> end,
            out ReadOnlySpan<char> endTime)
        {
            daylightSavingsOffset = null;
            start = null;
            startTime = null;
            end = null;
            endTime = null;

            int index = 0;
            standardName = TZif_ParsePosixName(posixFormat, ref index);
            standardOffset = TZif_ParsePosixOffset(posixFormat, ref index);

            daylightSavingsName = TZif_ParsePosixName(posixFormat, ref index);
            if (!daylightSavingsName.IsEmpty)
            {
                daylightSavingsOffset = TZif_ParsePosixOffset(posixFormat, ref index);

                if (index < posixFormat.Length && posixFormat[index] == ',')
                {
                    index++;
                    TZif_ParsePosixDateTime(posixFormat, ref index, out start, out startTime);

                    if (index < posixFormat.Length && posixFormat[index] == ',')
                    {
                        index++;
                        TZif_ParsePosixDateTime(posixFormat, ref index, out end, out endTime);
                    }
                }
            }

            return !standardName.IsEmpty && !standardOffset.IsEmpty;
        }
private static ReadOnlySpan<char> TZif_ParsePosixName(ReadOnlySpan<char> posixFormat, scoped ref int index)
        {
            bool isBracketEnclosed = index < posixFormat.Length && posixFormat[index] == '<';
            if (isBracketEnclosed)
            {
                // move past the opening bracket
                index++;

                ReadOnlySpan<char> result = TZif_ParsePosixString(posixFormat, ref index, c => c == '>');

                // move past the closing bracket
                if (index < posixFormat.Length && posixFormat[index] == '>')
                {
                    index++;
                }

                return result;
            }
            else
            {
                return TZif_ParsePosixString(
                    posixFormat,
                    ref index,
                    c => char.IsDigit(c) || c == '+' || c == '-' || c == ',');
            }
        }
private static void TZif_ParsePosixDateTime(ReadOnlySpan<char> posixFormat, scoped ref int index, out ReadOnlySpan<char> date, out ReadOnlySpan<char> time)
        {
            time = null;

            date = TZif_ParsePosixDate(posixFormat, ref index);
            if (index < posixFormat.Length && posixFormat[index] == '/')
            {
                index++;
                time = TZif_ParsePosixTime(posixFormat, ref index);
            }
        }
private static ReadOnlySpan<char> TZif_ParsePosixString(ReadOnlySpan<char> posixFormat, scoped ref int index, Func<char, bool> breakCondition)
        {
            int startIndex = index;
            for (; index < posixFormat.Length; index++)
            {
                char current = posixFormat[index];
                if (breakCondition(current))
                {
                    break;
                }
            }

            return posixFormat.Slice(startIndex, index - startIndex);
        }
private static string TZif_GetZoneAbbreviation(string zoneAbbreviations, int index)
        {
            int lastIndex = zoneAbbreviations.IndexOf('\0', index);
            return lastIndex > 0 ?
                zoneAbbreviations.Substring(index, lastIndex - index) :
                zoneAbbreviations.Substring(index);
        }
private static void TZif_ParseRaw(byte[] data, out DateTime[] dts, out byte[] typeOfLocalTime, out TZifType[] transitionType,
                                          out string zoneAbbreviations, out string? futureTransitionsPosixFormat)
        {
            futureTransitionsPosixFormat = null;

            // read in the 44-byte TZ header containing the count/length fields
            //
            int index = 0;
            TZifHead t = new TZifHead(data, index);
            index += TZifHead.Length;

            int timeValuesLength = 4; // the first version uses 4-bytes to specify times
            if (t.Version != TZVersion.V1)
            {
                // move index past the V1 information to read the V2 information
                index += (int)((timeValuesLength * t.TimeCount) + t.TimeCount + (6 * t.TypeCount) + ((timeValuesLength + 4) * t.LeapCount) + t.IsStdCount + t.IsGmtCount + t.CharCount);

                // read the V2 header
                t = new TZifHead(data, index);
                index += TZifHead.Length;
                timeValuesLength = 8; // the second version uses 8-bytes
            }

            // initialize the containers for the rest of the TZ data
            dts = new DateTime[t.TimeCount];
            typeOfLocalTime = new byte[t.TimeCount];
            transitionType = new TZifType[t.TypeCount];

            // read in the UTC transition points and convert them to Windows
            //
            for (int i = 0; i < t.TimeCount; i++)
            {
                long unixTime = TZif_ToUnixTime(data, index, t.Version);
                dts[i] = TZif_UnixTimeToDateTime(unixTime);
                index += timeValuesLength;
            }

            // read in the Type Indices; there is a 1:1 mapping of UTC transition points to Type Indices
            // these indices directly map to the array index in the transitionType array below
            //
            for (int i = 0; i < t.TimeCount; i++)
            {
                typeOfLocalTime[i] = data[index];
                index++;
            }

            // read in the Type table.  Each 6-byte entry represents
            // {UtcOffset, IsDst, AbbreviationIndex}
            //
            // each AbbreviationIndex is a character index into the zoneAbbreviations string below
            //
            for (int i = 0; i < t.TypeCount; i++)
            {
                transitionType[i] = new TZifType(data, index);
                index += 6;
            }

            // read in the Abbreviation ASCII string.  This string will be in the form:
            // "PST\0PDT\0PWT\0\PPT"
            //
            Encoding enc = Encoding.UTF8;
            zoneAbbreviations = enc.GetString(data, index, (int)t.CharCount);
            index += (int)t.CharCount;

            // skip ahead of the Leap-Seconds Adjustment data.  In a future release, consider adding
            // support for Leap-Seconds
            //
            index += (int)(t.LeapCount * (timeValuesLength + 4)); // skip the leap second transition times

            // read in the Standard Time table.  There should be a 1:1 mapping between Type-Index and Standard
            // Time table entries.
            //
            // TRUE     =     transition time is standard time
            // FALSE    =     transition time is wall clock time
            // ABSENT   =     transition time is wall clock time
            //
            index += (int)Math.Min(t.IsStdCount, t.TypeCount);

            // read in the GMT Time table.  There should be a 1:1 mapping between Type-Index and GMT Time table
            // entries.
            //
            // TRUE     =     transition time is UTC
            // FALSE    =     transition time is local time
            // ABSENT   =     transition time is local time
            //
            index += (int)Math.Min(t.IsGmtCount, t.TypeCount);

            if (t.Version != TZVersion.V1)
            {
                // read the POSIX-style format, which should be wrapped in newlines with the last newline at the end of the file
                if (data[index++] == '\n' && data[data.Length - 1] == '\n')
                {
                    futureTransitionsPosixFormat = enc.GetString(data, index, data.Length - index - 1);
                }
            }
        }
private static void NormalizeAdjustmentRuleOffset(TimeSpan baseUtcOffset, [NotNull] ref AdjustmentRule adjustmentRule)
        {
            // Certain time zones such as:
            //       Time Zone  start date  end date    offset
            // -----------------------------------------------------
            // America/Yakutat  0001-01-01  1867-10-18   14:41:00
            // America/Yakutat  1867-10-18  1900-08-20   14:41:00
            // America/Sitka    0001-01-01  1867-10-18   14:58:00
            // America/Sitka    1867-10-18  1900-08-20   14:58:00
            // Asia/Manila      0001-01-01  1844-12-31  -15:56:00
            // Pacific/Guam     0001-01-01  1845-01-01  -14:21:00
            // Pacific/Saipan   0001-01-01  1845-01-01  -14:21:00
            //
            // have larger offset than currently supported by framework.
            // If for whatever reason we find that time zone exceeding max
            // offset of 14h this function will truncate it to the max valid offset.
            // Updating max offset may cause problems with interacting with SQL server
            // which uses SQL DATETIMEOFFSET field type which was originally designed to be
            // bit-for-bit compatible with DateTimeOffset.

            TimeSpan utcOffset = GetUtcOffset(baseUtcOffset, adjustmentRule);

            // utc base offset delta increment
            TimeSpan adjustment = TimeSpan.Zero;

            if (utcOffset > MaxOffset)
            {
                adjustment = MaxOffset - utcOffset;
            }
            else if (utcOffset < MinOffset)
            {
                adjustment = MinOffset - utcOffset;
            }

            if (adjustment != TimeSpan.Zero)
            {
                adjustmentRule = AdjustmentRule.CreateAdjustmentRule(
                    adjustmentRule.DateStart,
                    adjustmentRule.DateEnd,
                    adjustmentRule.DaylightDelta,
                    adjustmentRule.DaylightTransitionStart,
                    adjustmentRule.DaylightTransitionEnd,
                    adjustmentRule.BaseUtcOffsetDelta + adjustment,
                    adjustmentRule.NoDaylightTransitions);
            }
        }
private readonly struct TZifType
        {
            public const int Length = 6;

            public readonly TimeSpan UtcOffset;
            public readonly bool IsDst;
            public readonly byte AbbreviationIndex;

            public TZifType(byte[] data, int index)
            {
                if (data == null || data.Length < index + Length)
                {
                    throw new ArgumentException(SR.Argument_TimeZoneInfoInvalidTZif, nameof(data));
                }
                UtcOffset = new TimeSpan(0, 0, TZif_ToInt32(data, index + 00));
                IsDst = (data[index + 4] != 0);
                AbbreviationIndex = data[index + 5];
            }
        }
private readonly struct TZifHead
        {
            public const int Length = 44;

            public readonly uint Magic; // TZ_MAGIC "TZif"
            public readonly TZVersion Version; // 1 byte for a \0 or 2 or 3
            // public byte[15] Reserved; // reserved for future use
            public readonly uint IsGmtCount; // number of transition time flags
            public readonly uint IsStdCount; // number of transition time flags
            public readonly uint LeapCount; // number of leap seconds
            public readonly uint TimeCount; // number of transition times
            public readonly uint TypeCount; // number of local time types
            public readonly uint CharCount; // number of abbreviated characters

            public TZifHead(byte[] data, int index)
            {
                if (data == null || data.Length < Length)
                {
                    throw new ArgumentException("bad data", nameof(data));
                }

                Magic = (uint)TZif_ToInt32(data, index + 00);

                if (Magic != 0x545A6966)
                {
                    // 0x545A6966 = {0x54, 0x5A, 0x69, 0x66} = "TZif"
                    throw new ArgumentException(SR.Argument_TimeZoneInfoBadTZif, nameof(data));
                }

                byte version = data[index + 04];
                Version =
                    version == '2' ? TZVersion.V2 :
                    version == '3' ? TZVersion.V3 :
                    TZVersion.V1;  // default/fallback to V1 to guard against future, unsupported version numbers

                // skip the 15 byte reserved field

                // don't use the BitConverter class which parses data
                // based on the Endianness of the machine architecture.
                // this data is expected to always be in "standard byte order",
                // regardless of the machine it is being processed on.

                IsGmtCount = (uint)TZif_ToInt32(data, index + 20);
                IsStdCount = (uint)TZif_ToInt32(data, index + 24);
                LeapCount = (uint)TZif_ToInt32(data, index + 28);
                TimeCount = (uint)TZif_ToInt32(data, index + 32);
                TypeCount = (uint)TZif_ToInt32(data, index + 36);
                CharCount = (uint)TZif_ToInt32(data, index + 40);
            }
        }
private enum TZVersion : byte
        {
            V1 = 0,
            V2,
            V3,
            // when adding more versions, ensure all the logic using TZVersion is still correct
        }
private static ReadOnlySpan<char> TZif_ParsePosixOffset(ReadOnlySpan<char> posixFormat, scoped ref int index) =>
            TZif_ParsePosixString(posixFormat, ref index, c => !char.IsDigit(c) && c != '+' && c != '-' && c != ':');
private static ReadOnlySpan<char> TZif_ParsePosixDate(ReadOnlySpan<char> posixFormat, scoped ref int index) =>
            TZif_ParsePosixString(posixFormat, ref index, c => c == '/' || c == ',');
private static ReadOnlySpan<char> TZif_ParsePosixTime(ReadOnlySpan<char> posixFormat, scoped ref int index) =>
            TZif_ParsePosixString(posixFormat, ref index, c => c == ',');
private static short TZif_ToInt16(ReadOnlySpan<byte> value)
            => BinaryPrimitives.ReadInt16BigEndian(value);
private static int TZif_ToInt32(byte[] value, int startIndex)
            => BinaryPrimitives.ReadInt32BigEndian(value.AsSpan(startIndex));
private static int TZif_ToInt32(ReadOnlySpan<byte> value)
            => BinaryPrimitives.ReadInt32BigEndian(value);
private static long TZif_ToInt64(byte[] value, int startIndex)
            => BinaryPrimitives.ReadInt64BigEndian(value.AsSpan(startIndex));
private static long TZif_ToUnixTime(byte[] value, int startIndex, TZVersion version) =>
            version != TZVersion.V1 ?
                TZif_ToInt64(value, startIndex) :
                TZif_ToInt32(value, startIndex);
private static DateTime TZif_UnixTimeToDateTime(long unixTime) =>
            unixTime < DateTimeOffset.UnixMinSeconds ? DateTime.MinValue :
            unixTime > DateTimeOffset.UnixMaxSeconds ? DateTime.MaxValue :
            DateTimeOffset.FromUnixTimeSeconds(unixTime).UtcDateTime;
private static void ValidateTimeZoneInfo(string id, TimeSpan baseUtcOffset, AdjustmentRule[]? adjustmentRules, out bool adjustmentRulesSupportDst)
        {
            ArgumentException.ThrowIfNullOrEmpty(id);

            if (UtcOffsetOutOfRange(baseUtcOffset))
            {
                throw new ArgumentOutOfRangeException(nameof(baseUtcOffset), SR.ArgumentOutOfRange_UtcOffset);
            }

            if (baseUtcOffset.Ticks % TimeSpan.TicksPerMinute != 0)
            {
                throw new ArgumentException(SR.Argument_TimeSpanHasSeconds, nameof(baseUtcOffset));
            }

            adjustmentRulesSupportDst = false;

            //
            // "adjustmentRules" can either be null or a valid array of AdjustmentRule objects.
            // A valid array is one that does not contain any null elements and all elements
            // are sorted in chronological order
            //

            if (adjustmentRules != null && adjustmentRules.Length != 0)
            {
                adjustmentRulesSupportDst = true;
                AdjustmentRule? current = null;
                for (int i = 0; i < adjustmentRules.Length; i++)
                {
                    AdjustmentRule? prev = current;
                    current = adjustmentRules[i];

                    if (current == null)
                    {
                        throw new InvalidTimeZoneException(SR.Argument_AdjustmentRulesNoNulls);
                    }

                    if (!IsValidAdjustmentRuleOffset(baseUtcOffset, current))
                    {
                        throw new InvalidTimeZoneException(SR.ArgumentOutOfRange_UtcOffsetAndDaylightDelta);
                    }

                    if (prev != null && current.DateStart <= prev.DateEnd)
                    {
                        // verify the rules are in chronological order and the DateStart/DateEnd do not overlap
                        throw new InvalidTimeZoneException(SR.Argument_AdjustmentRulesOutOfOrder);
                    }
                }
            }
        }
private static TimeSpan GetUtcOffset(TimeSpan baseUtcOffset, AdjustmentRule adjustmentRule)
        {
            return baseUtcOffset
                + adjustmentRule.BaseUtcOffsetDelta
                + (adjustmentRule.HasDaylightSaving ? adjustmentRule.DaylightDelta : TimeSpan.Zero);
        }
private static bool IsValidAdjustmentRuleOffset(TimeSpan baseUtcOffset, AdjustmentRule adjustmentRule)
        {
            TimeSpan utcOffset = GetUtcOffset(baseUtcOffset, adjustmentRule);
            return !UtcOffsetOutOfRange(utcOffset);
        }
public sealed class AdjustmentRule : IEquatable<AdjustmentRule?>, ISerializable, IDeserializationCallback
        {
            private static readonly TimeSpan DaylightDeltaAdjustment = TimeSpan.FromHours(24.0);
            private static readonly TimeSpan MaxDaylightDelta = TimeSpan.FromHours(12.0);
            private readonly DateTime _dateStart;
            private readonly DateTime _dateEnd;
            private readonly TimeSpan _daylightDelta;
            private readonly TransitionTime _daylightTransitionStart;
            private readonly TransitionTime _daylightTransitionEnd;
            private readonly TimeSpan _baseUtcOffsetDelta;   // delta from the default Utc offset (utcOffset = defaultUtcOffset + _baseUtcOffsetDelta)
            private readonly bool _noDaylightTransitions;

            public DateTime DateStart => _dateStart;

            public DateTime DateEnd => _dateEnd;

            public TimeSpan DaylightDelta => _daylightDelta;

            public TransitionTime DaylightTransitionStart => _daylightTransitionStart;

            public TransitionTime DaylightTransitionEnd => _daylightTransitionEnd;

            /// <summary>
            /// Gets the time difference with the base UTC offset for the time zone during the adjustment-rule period.
            /// </summary>
            public TimeSpan BaseUtcOffsetDelta => _baseUtcOffsetDelta;

            /// <summary>
            /// Gets a value indicating that this AdjustmentRule fixes the time zone offset
            /// from DateStart to DateEnd without any daylight transitions in between.
            /// </summary>
            internal bool NoDaylightTransitions => _noDaylightTransitions;

            internal bool HasDaylightSaving =>
                DaylightDelta != TimeSpan.Zero ||
                (DaylightTransitionStart != default && DaylightTransitionStart.TimeOfDay != DateTime.MinValue) ||
                (DaylightTransitionEnd != default && DaylightTransitionEnd.TimeOfDay != DateTime.MinValue.AddMilliseconds(1));

            public bool Equals([NotNullWhen(true)] AdjustmentRule? other) =>
                other != null &&
                _dateStart == other._dateStart &&
                _dateEnd == other._dateEnd &&
                _daylightDelta == other._daylightDelta &&
                _baseUtcOffsetDelta == other._baseUtcOffsetDelta &&
                _daylightTransitionEnd.Equals(other._daylightTransitionEnd) &&
                _daylightTransitionStart.Equals(other._daylightTransitionStart);

            /// <summary>Indicates whether the current instance is equal to another instance.</summary>
            /// <param name="obj">An instance to compare with this instance.</param>
            /// <returns>true if the current instance is equal to the other instance; otherwise, false.</returns>
            public override bool Equals([NotNullWhen(true)] object? obj) =>
                obj is AdjustmentRule other && Equals(other);

            public override int GetHashCode() => _dateStart.GetHashCode();

            private AdjustmentRule(
                DateTime dateStart,
                DateTime dateEnd,
                TimeSpan daylightDelta,
                TransitionTime daylightTransitionStart,
                TransitionTime daylightTransitionEnd,
                TimeSpan baseUtcOffsetDelta,
                bool noDaylightTransitions)
            {
                ValidateAdjustmentRule(dateStart, dateEnd, daylightDelta,
                       daylightTransitionStart, daylightTransitionEnd, noDaylightTransitions);

                _dateStart = dateStart;
                _dateEnd = dateEnd;
                _daylightDelta = daylightDelta;
                _daylightTransitionStart = daylightTransitionStart;
                _daylightTransitionEnd = daylightTransitionEnd;
                _baseUtcOffsetDelta = baseUtcOffsetDelta;
                _noDaylightTransitions = noDaylightTransitions;
            }

            /// <summary>
            /// Creates a new adjustment rule for a particular time zone.
            /// </summary>
            /// <param name="dateStart">The effective date of the adjustment rule. If the value is <c>DateTime.MinValue.Date</c>, this is the first adjustment rule in effect for a time zone.</param>
            /// <param name="dateEnd">The last date that the adjustment rule is in force. If the value is <c>DateTime.MaxValue.Date</c>, the adjustment rule has no end date.</param>
            /// <param name="daylightDelta">The time change that results from the adjustment. This value is added to the time zone's <see cref="P:System.TimeZoneInfo.BaseUtcOffset" /> and <see cref="P:System.TimeZoneInfo.BaseUtcOffsetDelta" /> properties to obtain the correct daylight offset from Coordinated Universal Time (UTC). This value can range from -14 to 14.</param>
            /// <param name="daylightTransitionStart">The start of daylight saving time.</param>
            /// <param name="daylightTransitionEnd">The end of daylight saving time.</param>
            /// <param name="baseUtcOffsetDelta">The time difference with the base UTC offset for the time zone during the adjustment-rule period.</param>
            /// <returns>The new adjustment rule.</returns>
            public static AdjustmentRule CreateAdjustmentRule(
                DateTime dateStart,
                DateTime dateEnd,
                TimeSpan daylightDelta,
                TransitionTime daylightTransitionStart,
                TransitionTime daylightTransitionEnd,
                TimeSpan baseUtcOffsetDelta)
            {
                return new AdjustmentRule(
                    dateStart,
                    dateEnd,
                    daylightDelta,
                    daylightTransitionStart,
                    daylightTransitionEnd,
                    baseUtcOffsetDelta,
                    noDaylightTransitions: false);
            }

            public static AdjustmentRule CreateAdjustmentRule(
                DateTime dateStart,
                DateTime dateEnd,
                TimeSpan daylightDelta,
                TransitionTime daylightTransitionStart,
                TransitionTime daylightTransitionEnd)
            {
                return new AdjustmentRule(
                    dateStart,
                    dateEnd,
                    daylightDelta,
                    daylightTransitionStart,
                    daylightTransitionEnd,
                    baseUtcOffsetDelta: TimeSpan.Zero,
                    noDaylightTransitions: false);
            }

            internal static AdjustmentRule CreateAdjustmentRule(
                DateTime dateStart,
                DateTime dateEnd,
                TimeSpan daylightDelta,
                TransitionTime daylightTransitionStart,
                TransitionTime daylightTransitionEnd,
                TimeSpan baseUtcOffsetDelta,
                bool noDaylightTransitions)
            {
                AdjustDaylightDeltaToExpectedRange(ref daylightDelta, ref baseUtcOffsetDelta);
                return new AdjustmentRule(
                    dateStart,
                    dateEnd,
                    daylightDelta,
                    daylightTransitionStart,
                    daylightTransitionEnd,
                    baseUtcOffsetDelta,
                    noDaylightTransitions);
            }

            //
            // When Windows sets the daylight transition start Jan 1st at 12:00 AM, it means the year starts with the daylight saving on.
            // We have to special case this value and not adjust it when checking if any date is in the daylight saving period.
            //
            internal bool IsStartDateMarkerForBeginningOfYear() =>
                !NoDaylightTransitions &&
                DaylightTransitionStart.Month == 1 && DaylightTransitionStart.Day == 1 &&
                DaylightTransitionStart.TimeOfDay.TimeOfDay.Ticks < TimeSpan.TicksPerSecond; // < 12:00:01 AM

            //
            // When Windows sets the daylight transition end Jan 1st at 12:00 AM, it means the year ends with the daylight saving on.
            // We have to special case this value and not adjust it when checking if any date is in the daylight saving period.
            //
            internal bool IsEndDateMarkerForEndOfYear() =>
                !NoDaylightTransitions &&
                DaylightTransitionEnd.Month == 1 && DaylightTransitionEnd.Day == 1 &&
                DaylightTransitionEnd.TimeOfDay.TimeOfDay.Ticks < TimeSpan.TicksPerSecond; // < 12:00:01 AM

            /// <summary>
            /// Helper function that performs all of the validation checks for the factory methods and deserialization callback.
            /// </summary>
            private static void ValidateAdjustmentRule(
                DateTime dateStart,
                DateTime dateEnd,
                TimeSpan daylightDelta,
                TransitionTime daylightTransitionStart,
                TransitionTime daylightTransitionEnd,
                bool noDaylightTransitions)
            {
                if (dateStart.Kind != DateTimeKind.Unspecified && dateStart.Kind != DateTimeKind.Utc)
                {
                    throw new ArgumentException(SR.Argument_DateTimeKindMustBeUnspecifiedOrUtc, nameof(dateStart));
                }

                if (dateEnd.Kind != DateTimeKind.Unspecified && dateEnd.Kind != DateTimeKind.Utc)
                {
                    throw new ArgumentException(SR.Argument_DateTimeKindMustBeUnspecifiedOrUtc, nameof(dateEnd));
                }

                if (daylightTransitionStart.Equals(daylightTransitionEnd) && !noDaylightTransitions)
                {
                    throw new ArgumentException(SR.Argument_TransitionTimesAreIdentical, nameof(daylightTransitionEnd));
                }

                if (dateStart > dateEnd)
                {
                    throw new ArgumentException(SR.Argument_OutOfOrderDateTimes, nameof(dateStart));
                }

                // This cannot use UtcOffsetOutOfRange to account for the scenario where Samoa moved across the International Date Line,
                // which caused their current BaseUtcOffset to be +13. But on the other side of the line it was UTC-11 (+1 for daylight).
                // So when trying to describe DaylightDeltas for those times, the DaylightDelta needs
                // to be -23 (what it takes to go from UTC+13 to UTC-10)
                if (daylightDelta.TotalHours < -23.0 || daylightDelta.TotalHours > 14.0)
                {
                    throw new ArgumentOutOfRangeException(nameof(daylightDelta), daylightDelta, SR.ArgumentOutOfRange_UtcOffset);
                }

                if (daylightDelta.Ticks % TimeSpan.TicksPerMinute != 0)
                {
                    throw new ArgumentException(SR.Argument_TimeSpanHasSeconds, nameof(daylightDelta));
                }

                if (dateStart != DateTime.MinValue && dateStart.Kind == DateTimeKind.Unspecified && dateStart.TimeOfDay != TimeSpan.Zero)
                {
                    throw new ArgumentException(SR.Argument_DateTimeHasTimeOfDay, nameof(dateStart));
                }

                if (dateEnd != DateTime.MaxValue && dateEnd.Kind == DateTimeKind.Unspecified && dateEnd.TimeOfDay != TimeSpan.Zero)
                {
                    throw new ArgumentException(SR.Argument_DateTimeHasTimeOfDay, nameof(dateEnd));
                }
            }

            /// <summary>
            /// Ensures the daylight delta is within [-12, 12] hours
            /// </summary>>
            private static void AdjustDaylightDeltaToExpectedRange(ref TimeSpan daylightDelta, ref TimeSpan baseUtcOffsetDelta)
            {
                if (daylightDelta > MaxDaylightDelta)
                {
                    daylightDelta -= DaylightDeltaAdjustment;
                    baseUtcOffsetDelta += DaylightDeltaAdjustment;
                }
                else if (daylightDelta < -MaxDaylightDelta)
                {
                    daylightDelta += DaylightDeltaAdjustment;
                    baseUtcOffsetDelta -= DaylightDeltaAdjustment;
                }

                Diagnostics.Debug.Assert(daylightDelta <= MaxDaylightDelta && daylightDelta >= -MaxDaylightDelta,
                                                "DaylightDelta should not ever be more than 24h");
            }

            void IDeserializationCallback.OnDeserialization(object? sender)
            {
                // OnDeserialization is called after each instance of this class is deserialized.
                // This callback method performs AdjustmentRule validation after being deserialized.

                try
                {
                    ValidateAdjustmentRule(_dateStart, _dateEnd, _daylightDelta,
                                           _daylightTransitionStart, _daylightTransitionEnd, _noDaylightTransitions);
                }
                catch (ArgumentException e)
                {
                    throw new SerializationException(SR.Serialization_InvalidData, e);
                }
            }

            void ISerializable.GetObjectData(SerializationInfo info, StreamingContext context)
            {
                ArgumentNullException.ThrowIfNull(info);

                info.AddValue("DateStart", _dateStart); // Do not rename (binary serialization)
                info.AddValue("DateEnd", _dateEnd); // Do not rename (binary serialization)
                info.AddValue("DaylightDelta", _daylightDelta); // Do not rename (binary serialization)
                info.AddValue("DaylightTransitionStart", _daylightTransitionStart); // Do not rename (binary serialization)
                info.AddValue("DaylightTransitionEnd", _daylightTransitionEnd); // Do not rename (binary serialization)
                info.AddValue("BaseUtcOffsetDelta", _baseUtcOffsetDelta); // Do not rename (binary serialization)
                info.AddValue("NoDaylightTransitions", _noDaylightTransitions); // Do not rename (binary serialization)
            }

            private AdjustmentRule(SerializationInfo info, StreamingContext context)
            {
                ArgumentNullException.ThrowIfNull(info);

                _dateStart = (DateTime)info.GetValue("DateStart", typeof(DateTime))!; // Do not rename (binary serialization)
                _dateEnd = (DateTime)info.GetValue("DateEnd", typeof(DateTime))!; // Do not rename (binary serialization)
                _daylightDelta = (TimeSpan)info.GetValue("DaylightDelta", typeof(TimeSpan))!; // Do not rename (binary serialization)
                _daylightTransitionStart = (TransitionTime)info.GetValue("DaylightTransitionStart", typeof(TransitionTime))!; // Do not rename (binary serialization)
                _daylightTransitionEnd = (TransitionTime)info.GetValue("DaylightTransitionEnd", typeof(TransitionTime))!; // Do not rename (binary serialization)

                object? o = info.GetValueNoThrow("BaseUtcOffsetDelta", typeof(TimeSpan)); // Do not rename (binary serialization)
                if (o != null)
                {
                    _baseUtcOffsetDelta = (TimeSpan)o;
                }

                o = info.GetValueNoThrow("NoDaylightTransitions", typeof(bool)); // Do not rename (binary serialization)
                if (o != null)
                {
                    _noDaylightTransitions = (bool)o;
                }
            }
        }
private static UnixSourceTimeZone? GetTimeZoneFromTzData(byte[]? rawData, string id)
        {
            if (rawData != null)
            {
                try
                {
                    return new UnixSourceTimeZone(rawData, id, dstDisabled: false); // create a TimeZoneInfo instance from the TZif data w/ DST support
                }
                catch (ArgumentException) { }
                catch (InvalidTimeZoneException) { }

                try
                {
                    return new UnixSourceTimeZone(rawData, id, dstDisabled: true); // create a TimeZoneInfo instance from the TZif data w/o DST support
                }
                catch (ArgumentException) { }
                catch (InvalidTimeZoneException) { }
            }
            return null;
        }
private UnixSourceTimeZone(){_id="UTC";}
public static void WriteModel(byte[] data,string id,bool disabled,bool local=false){var z=local?(GetTimeZoneFromTzData(data,id)??new UnixSourceTimeZone()):new UnixSourceTimeZone(data,id,disabled);var rules=z._adjustmentRules??Array.Empty<AdjustmentRule>();var create=typeof(System.TimeZoneInfo.AdjustmentRule).GetMethod("CreateAdjustmentRule",System.Reflection.BindingFlags.Static|System.Reflection.BindingFlags.NonPublic,null,new[]{typeof(DateTime),typeof(DateTime),typeof(TimeSpan),typeof(TransitionTime),typeof(TransitionTime),typeof(TimeSpan),typeof(bool)},null)!;var bcl=new System.TimeZoneInfo.AdjustmentRule[rules.Length];for(int i=0;i<rules.Length;i++){var r=rules[i];bcl[i]=(System.TimeZoneInfo.AdjustmentRule)create.Invoke(null,new object[]{r.DateStart,r.DateEnd,r.DaylightDelta,r.DaylightTransitionStart,r.DaylightTransitionEnd,r.BaseUtcOffsetDelta,r.NoDaylightTransitions})!;}Program.zone=System.TimeZoneInfo.CreateCustomTimeZone(id,z._baseUtcOffset,id,id,id,bcl,false);Console.WriteLine($"ZONE {z._baseUtcOffset.Ticks} {rules.Length}");foreach(var r in rules)Console.WriteLine($"{Program.RawDate(r.DateStart)} {Program.RawDate(r.DateEnd)} {r.DaylightDelta.Ticks} {r.BaseUtcOffsetDelta.Ticks} {(r.NoDaylightTransitions?1:0)} {Program.Transition(r.DaylightTransitionStart)} {Program.Transition(r.DaylightTransitionEnd)}");Console.WriteLine("END");}
}
