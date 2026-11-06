// Actual BCL8, no Console or managed file I/O before each command-line scenario.
using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Reflection;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
class Program
{
    static string Decode(string s) =>
        s == "-" ? null
        : s == "."
            ? ""
            : new string(Enumerable.Range(0, s.Length / 4)
                             .Select(i => (char)(Convert.ToInt32(s.Substring(4 * i, 2), 16) |
                                                 (Convert.ToInt32(s.Substring(4 * i + 2, 2), 16) << 8)))
                             .ToArray());
    static string Encode(string s) => s == null ? "-"
                                      : s.Length == 0
                                          ? "."
                                          : string.Concat(s.Select(c => ((int)c & 255).ToString("x2") +
                                                                        ((int)c >> 8).ToString("x2")));
    [DllImport("libc", EntryPoint = "setenv")]
    static extern int SetRaw(byte[] key, byte[] value, int overwrite);
    [DllImport("libc", EntryPoint = "unsetenv")]
    static extern int UnsetRaw(byte[] key);
    static byte[] Bytes(string s) => Encoding.UTF8.GetBytes(s + "\0");
    [MethodImpl(MethodImplOptions.NoInlining)]
    static string Policy()
    {
        var p = typeof(FileStream)
                    .Assembly.GetType("Microsoft.Win32.SafeHandles.SafeFileHandle")
                    .GetProperty("DisableFileLocking", BindingFlags.Static | BindingFlags.NonPublic);
        return p == null ? "NA" : ((bool)p.GetValue(null) ? "1" : "0");
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static bool BooleanValue(string value)
    {
        Environment.SetEnvironmentVariable("OPENSIM_POLICY_BOOLEAN", value);
        var method = typeof(object)
                         .Assembly.GetType("System.AppContextConfigHelper")
                         .GetMethod("GetBooleanConfig", BindingFlags.NonPublic | BindingFlags.Static, null,
                                    new[] { typeof(string), typeof(string), typeof(bool) }, null);
        return (bool)method.Invoke(
            null, new object[] { "Opensim.TestPolicy.Unset", "OPENSIM_POLICY_BOOLEAN", false });
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Open(string path, int mode, int share)
    {
        using var file = new FileStream(path, (FileMode)mode, mode == 3 ? FileAccess.Read : FileAccess.Write,
                                        (FileShare)share, 4096, FileOptions.None);
    }
    [MethodImpl(MethodImplOptions.NoInlining)]
    static long Offset(long ticks) =>
        TimeZoneInfo.Local.GetUtcOffset(new DateTime(ticks, DateTimeKind.Utc)).Ticks;
    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        if (args[0] == "--bool")
        {
            string row;
            while ((row = Console.ReadLine()) != null)
                Console.WriteLine(BooleanValue(Decode(row)) ? "1" : "0");
            return;
        }
        if (args[0] == "--runtime")
        {
            Console.WriteLine(Environment.Version + "|" + typeof(object).Assembly.Location + "|" +
                              RuntimeInformation.FrameworkDescription + "|" +
                              RuntimeInformation.OSDescription);
            return;
        }
        var results = new System.Collections.Generic.List<string>();
        foreach (var token in args[0].Split(','))
        {
            var a = token.Split(':');
            string result = a[0];
            try
            {
                switch (a[0])
                {
                case "env":
                    Environment.SetEnvironmentVariable(Decode(a[1]), Decode(a[2]));
                    break;
                case "raw":
                    if (Decode(a[2]) == null)
                        UnsetRaw(Bytes(Decode(a[1])));
                    else
                        SetRaw(Bytes(Decode(a[1])), Bytes(Decode(a[2])), 1);
                    break;
                case "factory": {
                    Func<string, int, int, FileStream> provider = (path, mode, share) =>
                        new FileStream(path, (FileMode)mode, mode == 3 ? FileAccess.Read : FileAccess.Write,
                                       (FileShare)share);
                    GC.KeepAlive(provider);
                    break;
                }
                case "context":
                    GC.KeepAlive(new object());
                    break;
                case "open":
                    Open(Decode(a[1]), int.Parse(a[2]), int.Parse(a[3]));
                    break;
                case "p":
                    result = "P:" + Policy();
                    break;
                case "sw":
                    AppContext.SetSwitch("System.IO.DisableFileLocking", a[1] == "1");
                    break;
                case "clear":
                    TimeZoneInfo.ClearCachedData();
                    break;
                case "offset":
                    result = "O:" + Offset(long.Parse(a[1]));
                    break;
                case "dst":
                    result = "D:" + (TimeZoneInfo.Local.IsDaylightSavingTime(
                                         DateTimeOffset.FromUnixTimeSeconds(long.Parse(a[1])).UtcDateTime)
                                         ? "1"
                                         : "0");
                    break;
                case "find":
                    result =
                        "D:" + (TimeZoneInfo.FindSystemTimeZoneById(Decode(a[1]))
                                        .IsDaylightSavingTime(
                                            DateTimeOffset.FromUnixTimeSeconds(long.Parse(a[2])).UtcDateTime)
                                    ? "1"
                                    : "0");
                    break;
                default:
                    throw new Exception("operation");
                }
            }
            catch (Exception e)
            {
                result = "EX:" + e.GetType().FullName + ":" +
                         unchecked((uint)e.HResult).ToString(CultureInfo.InvariantCulture) + ":" +
                         Encode(e.Message) + ":" + Encode(e is ArgumentException x ? x.ParamName : null);
            }
            results.Add(result);
        }
        Console.WriteLine(string.Join("|", results));
    }
}
