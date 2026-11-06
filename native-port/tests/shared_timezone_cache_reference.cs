// Actual pinned BCL TimeZoneInfo/CachedData; no cache/rule reimplementation.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Globalization;
class Program
{
    static string D(string x) =>
        x == "-" ? null
        : x == "."
            ? ""
            : new string(Enumerable.Range(0, x.Length / 4)
                             .Select(i => (char)(Convert.ToInt32(x.Substring(i * 4, 2), 16) |
                                                 (Convert.ToInt32(x.Substring(i * 4 + 2, 2), 16) << 8)))
                             .ToArray());
    static string H(string x) => x == null       ? "-"
                                 : x.Length == 0 ? "."
                                                 : string.Concat(x.Select(c => ((int)c & 255).ToString("x2") +
                                                                               ((int)c >> 8).ToString("x2")));
    static string Run(string row)
    {
        var zones = new Dictionary<int, TimeZoneInfo>();
        var caches = new Dictionary<int, object>();
        var identities = new List<TimeZoneInfo>();
        var output = new List<string>();
        var ct = typeof(TimeZoneInfo).GetNestedType("CachedData", BindingFlags.NonPublic);
        var lp =
            ct.GetProperty("Local", BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
        var offset = typeof(TimeZoneInfo)
                         .GetMethods(BindingFlags.NonPublic | BindingFlags.Static)
                         .Single(m => m.Name == "GetUtcOffsetFromUtc" && m.GetParameters().Length == 4);
        foreach (var token in row.Split(','))
        {
            var a = token.Split(':');
            string result = a[0];
            try
            {
                switch (a[0])
                {
                case "E":
                    Environment.SetEnvironmentVariable(D(a[1]), D(a[2]));
                    break;
                case "C":
                    TimeZoneInfo.ClearCachedData();
                    break;
                case "PC":
                    caches[int.Parse(a[1])] = Activator.CreateInstance(ct, true);
                    break;
                case "U": {
                    var z = int.Parse(a[1]) < 0 ? TimeZoneInfo.Local
                                                : (TimeZoneInfo)lp.GetValue(caches[int.Parse(a[1])]);
                    var date = new DateTime(long.Parse(a[2]), DateTimeKind.Utc);
                    var p = new object[] { date, z, false, false };
                    long ticks = ((TimeSpan)offset.Invoke(null, p)).Ticks;
                    result = "Q:" + ticks + ":" + ((bool)p[2] ? 1 : 0) + ":" + ((bool)p[3] ? 1 : 0) + ":" +
                             (z.IsDaylightSavingTime(date) ? 1 : 0);
                    break;
                }
                case "D":
                    result = "D:" + (zones[int.Parse(a[1])].IsDaylightSavingTime(
                                         DateTimeOffset.FromUnixTimeSeconds(long.Parse(a[2])).UtcDateTime)
                                         ? 1
                                         : 0);
                    break;
                case "N":
                    caches[int.Parse(a[1])] = Activator.CreateInstance(ct, true);
                    break;
                case "L": {
                    var z = a.Length == 3 ? (TimeZoneInfo)lp.GetValue(caches[int.Parse(a[2])])
                                          : TimeZoneInfo.Local;
                    zones[int.Parse(a[1])] = z;
                    int id = identities.FindIndex(t => ReferenceEquals(t, z));
                    if (id < 0)
                    {
                        id = identities.Count;
                        identities.Add(z);
                    }
                    result = "Z:" + id;
                    break;
                }
                case "F": {
                    var z = TimeZoneInfo.FindSystemTimeZoneById(D(a[2]));
                    zones[int.Parse(a[1])] = z;
                    int id = identities.FindIndex(t => ReferenceEquals(t, z));
                    if (id < 0)
                    {
                        id = identities.Count;
                        identities.Add(z);
                    }
                    result = "Z:" + id;
                    break;
                }
                case "I":
                    result = "I:" + H(zones[int.Parse(a[1])].Id);
                    break;
                case "R":
                    result = "R:" + (ReferenceEquals(zones[int.Parse(a[1])], zones[int.Parse(a[2])]) ? 1 : 0);
                    break;
                case "Q": {
                    var z = zones[int.Parse(a[1])];
                    var date = new DateTime(long.Parse(a[2]), DateTimeKind.Utc);
                    var p = new object[] { date, z, false, false };
                    long ticks = ((TimeSpan)offset.Invoke(null, p)).Ticks;
                    result = "Q:" + ticks + ":" + ((bool)p[2] ? 1 : 0) + ":" + ((bool)p[3] ? 1 : 0) + ":" +
                             (z.IsDaylightSavingTime(date) ? 1 : 0);
                    break;
                }
                case "ALL":
                    result = "ALL:" + TimeZoneInfo.GetSystemTimeZones().Count;
                    break;
                default:
                    throw new Exception("operation");
                }
            }
            catch (Exception e)
            {
                if (e is TargetInvocationException t && t.InnerException != null)
                    e = t.InnerException;
                result = "EX:" + e.GetType().FullName + ":" + H(e.Message);
            }
            output.Add(result);
        }
        return string.Join("|", output);
    }
    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        if (args[0] == "--meta")
        {
            Console.WriteLine(Environment.Version + "|" + typeof(object).Assembly.Location + "|" +
                              typeof(object)
                                  .Assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>()
                                  .InformationalVersion);
            return;
        }
        if (args[0] == "--probe")
        {
            string row;
            while ((row = Console.ReadLine()) != null)
                Console.WriteLine(Run(row));
            return;
        }
        Console.WriteLine(Run(args[0]));
    }
}
