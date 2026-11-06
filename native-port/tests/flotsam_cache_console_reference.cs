using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text;
using WaitCallback = System.Threading.WaitCallback;
class HookCulture : CultureInfo
{
    readonly TextInfo original = CultureInfo.InvariantCulture.TextInfo;
    public HookCulture() : base("")
    {
    }
    public override TextInfo TextInfo
    {
        get {
            Probe.LowerHook();
            return original;
        }
    }
}
class Output
{
    public void OutputLine(string format, params object[] values)
    {
        Probe.Log("O:" + string.Format(CultureInfo.CurrentCulture, format, values));
        Probe.Hook(2);
    }
}
interface ICommandConsole
{
    void Output(string format, params object[] values);
}
class ConsoleModel : ICommandConsole
{
    public void Output(string format, params object[] values)
    {
        Probe.Log("O:" + string.Format(CultureInfo.CurrentCulture, format, values));
        Probe.Hook(2);
    }
}
static class MainConsole
{
    public static ICommandConsole Instance
    {
        get {
            Probe.Log("CON");
            Probe.Hook(1);
            return Probe.NullConsole ? null : new ConsoleModel();
        }
    }
}
static class WorkManager
{
    public static void RunInThreadPool(WaitCallback callback, object state, string name, bool flag)
    {
        Probe.Log("POST:" + name + ":" + (flag ? 1 : 0));
        Probe.Hook(3);
        Action job = () => callback(state);
        if (Probe.Dispatch != 0)
            job();
        else
            Probe.Jobs.Enqueue(job);
    }
}
static class FakeThread
{
    public static void Sleep(int ms)
    {
        Probe.Log("SLEEP:" + ms);
        Probe.Hook(14);
    }
}
static class FakeDirectory
{
    public static string[] GetFiles(string path, string pattern)
    {
        Probe.Log("FILES:" + Probe.Show(path) + ":" + pattern);
        Probe.Hook(7);
        return Probe.Paths();
    }
}
static class FakeFile
{
    public static FakeDate GetLastWriteTime(string path)
    {
        Probe.Log("WRITE:" + Probe.Show(path));
        Probe.Hook(8);
        return new(new System.DateTime(2024, 10, 30, 13, 14, 15, DateTimeKind.Local));
    }
}
struct FakeDate
{
    public System.DateTime Value;
    public FakeDate(System.DateTime value)
    {
        Value = value;
    }
    public static FakeDate Now
    {
        get {
            Probe.Log("NOW");
            Probe.Hook(17);
            return new(new System.DateTime(Probe.NowTicks, DateTimeKind.Local));
        }
    }
    public static bool TryParse(string value, out FakeDate result)
    {
        Probe.Log("PARSE:" + Probe.Show(value));
        Probe.Hook(18);
        bool ok = System.DateTime.TryParse(value, out var parsed);
        result = new(parsed);
        return ok;
    }
    public string ToString(string format)
    {
        Probe.Log("FORMAT:" + Value.Ticks + ":" + format);
        Probe.Hook(9);
        return Value.ToString(format);
    }
    public static bool operator >=(FakeDate a, FakeDate b) => a.Value >= b.Value;
    public static bool operator <=(FakeDate a, FakeDate b) => a.Value <= b.Value;
}
class Timer
{
    public void Stop()
    {
        Probe.Log("STOP");
        Probe.Hook(21);
        Probe.Active = false;
    }
    public void Start()
    {
        Probe.Log("START");
        Probe.Hook(22);
        Probe.Active = true;
    }
}
class Memory
{
    public int Count
    {
        get {
            Probe.Log("MEM");
            Probe.Hook(4);
            return Probe.Memory;
        }
    }
    public void Clear()
    {
        Probe.Log("CM");
        Probe.Hook(10);
        Probe.Memory = 0;
    }
}
class Negatives
{
    public int Count
    {
        get {
            Probe.Log("NEG");
            Probe.Hook(11);
            return Probe.Negative;
        }
    }
    public void Clear()
    {
        Probe.Log("CN");
        Probe.Hook(12);
        Probe.Negative = 0;
    }
}
partial class FlotsamAssetCache
{
    public bool m_MemoryCacheEnabled, m_FileCacheEnabled, m_negativeCacheEnabled, m_timerRunning,
        m_cleanupRunning;
    public string m_CacheDirectory = "assetcache";
    public Timer m_CacheCleanTimer = new();
    object timerLock = new();
    Memory m_MemoryCache = new();
    Negatives m_negativeCache = new();
    int GetFileCacheCount(string path)
    {
        Probe.Log("COUNT:" + Probe.Show(path));
        Probe.Hook(5);
        return Probe.Files;
    }
    List<string> GenerateCacheHitReport()
    {
        Probe.Log("REPORT");
        Probe.Hook(6);
        return new() { "hit one", "hit two" };
    }
    void ClearFileCache()
    {
        Probe.Log("CF");
        Probe.Hook(13);
        Probe.Files = 0;
        Probe.FileMode = 3;
    }
    int TouchAllSceneAssets(bool get)
    {
        Probe.Log("TOUCH:" + (get ? 1 : 0));
        Probe.Hook(15);
        return Probe.Touch;
    }
    void DoCleanExpiredFiles(FakeDate date)
    {
        Probe.Log("CLEAN:" + date.Value.Ticks + ":" + (int)date.Value.Kind);
        Probe.Hook(16);
    }
    void HandleLoadDefaultAssets()
    {
        Probe.Log("LOAD");
        Probe.Hook(19);
        ++Probe.Memory;
    }
    void HandleDeleteDefaultAssets()
    {
        Probe.Log("DELETE");
        Probe.Hook(20);
        Probe.Memory = 0;
    }
    public void Call(string[] args) => HandleConsoleCommand("Assets", args);
}
static class Probe
{
    public static FlotsamAssetCache Module;
    public static Queue<Action> Jobs = new();
    public static int Memory = 7, Negative = 3, Files = 11, Touch = 13, FileMode, Dispatch;
    public static bool NullConsole, Active;
    public static long NowTicks = 638712864000000000;
    static int point, nth = 1, seen, mutPoint, mutNth = 1, mutSeen, mutation, lowerAt, lowerSeen;
    static string lowerCurrent, lowerLater;
    static string[] currentArgs;
    static bool insideClear;
    static List<string> trace = new();
    public static string Show(string value) =>
        value == null ? "~" : Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
    public static string Decode(string value) => value == "~"
                                                     ? null
                                                     : Encoding.UTF8.GetString(Convert.FromHexString(value));
    public static void Log(string value) => trace.Add(value);
    public static void LowerHook()
    {
        if (!insideClear || lowerAt == 0)
            return;
        Log("LOWER:" + (++lowerSeen));
        if (lowerSeen == lowerAt && currentArgs != null && currentArgs.Length > 3)
        {
            currentArgs[2] = lowerCurrent;
            currentArgs[3] = lowerLater;
        }
    }
    public static void Hook(int p)
    {
        if (p == mutPoint && ++mutSeen == mutNth)
        {
            switch (mutation)
            {
            case 1:
                Module.m_FileCacheEnabled = !Module.m_FileCacheEnabled;
                Module.m_MemoryCacheEnabled = !Module.m_MemoryCacheEnabled;
                Module.m_negativeCacheEnabled = !Module.m_negativeCacheEnabled;
                break;
            case 2:
                Module.m_cleanupRunning = false;
                break;
            case 3:
                Module.m_timerRunning = false;
                break;
            case 4:
                Module.m_timerRunning = true;
                break;
            case 5:
                Module.m_CacheDirectory = "cache.bad";
                break;
            case 6:
                Module.m_CacheCleanTimer = null;
                break;
            }
        }
        if (p == point && ++seen == nth)
            throw new Exception("fault");
    }
    public static string[] Paths() => FileMode switch {
        1 => new[] { "cache.bad/RegionStatus_uuid.fac" },
        2 => new[] { "cache_under/RegionStatus_uuid.fac" },
        3 => Array.Empty<string>(),
        4 => new string[] { null },
        5 => new[] { "é漢/RegionStatus_uuid.fac" },
        _ => new[] { "cache/RegionStatus_uuid.fac", "cache/no-marker", "cache/a.fac" }
    };
    static void Flags(int n)
    {
        Module.m_MemoryCacheEnabled = (n & 1) != 0;
        Module.m_FileCacheEnabled = (n & 2) != 0;
        Module.m_negativeCacheEnabled = (n & 4) != 0;
    }
    static void Reset(int flags, int timer, int cleanup)
    {
        Jobs.Clear();
        currentArgs = null;
        insideClear = false;
        lowerAt = lowerSeen = 0;
        point = seen = mutPoint = mutSeen = mutation = 0;
        nth = mutNth = 1;
        Memory = 7;
        Negative = 3;
        Files = 11;
        Touch = 13;
        FileMode = Dispatch = 0;
        NullConsole = Active = false;
        NowTicks = 638712864000000000;
        Module = new();
        Flags(flags);
        Module.m_timerRunning = timer != 0;
        Module.m_cleanupRunning = cleanup != 0;
        Active = timer != 0;
    }
    static string Run(string line)
    {
        trace.Clear();
        var a = line.Split(' ');
        int n = int.Parse(a[1]);
        string result;
        try
        {
            switch (a[0])
            {
            case "reset":
                Reset(n, int.Parse(a[2]), int.Parse(a[3]));
                result = "reset";
                break;
            case "fail":
                point = n;
                nth = int.Parse(a[2]);
                seen = 0;
                result = "fail";
                break;
            case "mutate":
                mutPoint = n;
                mutNth = int.Parse(a[2]);
                mutation = int.Parse(a[3]);
                mutSeen = 0;
                result = "mutate";
                break;
            case "lower":
                lowerAt = n;
                lowerSeen = 0;
                lowerCurrent = Decode(a[2]);
                lowerLater = Decode(a[3]);
                result = "lower";
                break;
            case "flags":
                Flags(n);
                result = "flags";
                break;
            case "files":
                FileMode = n;
                result = "files";
                break;
            case "dispatch":
                Dispatch = n;
                result = "dispatch";
                break;
            case "console":
                NullConsole = n != 0;
                result = "console";
                break;
            case "timer":
                Module.m_CacheCleanTimer = n != 0 ? new Timer() : null;
                result = "timer";
                break;
            case "life":
                Module.m_timerRunning = n != 0;
                Module.m_cleanupRunning = a[2] != "0";
                result = "life";
                break;
            case "call":
                currentArgs = n < 0 ? null : a.Skip(2).Take(n).Select(Decode).ToArray();
                insideClear = currentArgs != null && currentArgs.Length > 1 && currentArgs[1] == "clear";
                try
                {
                    Module.Call(currentArgs);
                }
                finally
                {
                    insideClear = false;
                }
                result = "call";
                break;
            case "drain":
                int count = 0;
                while (Jobs.Count != 0 && (n == 0 || count < n))
                {
                    var job = Jobs.Dequeue();
                    ++count;
                    job();
                }
                result = "drain";
                break;
            default:
                throw new Exception("operation");
            }
        }
        catch (Exception error)
        {
            result = "EX:" + error.GetType().FullName + ":" + error.HResult.ToString("X8") + ":" +
                     Show((error as ArgumentException)?.ParamName) + ":" +
                     ((error as ArgumentOutOfRangeException)?.ActualValue?.ToString() ?? "~") + ":" +
                     Show(error.Message);
        }
        return result + "|" + Show(string.Join(";", trace)) + "|" + (Module.m_MemoryCacheEnabled ? 1 : 0) +
               (Module.m_FileCacheEnabled ? 1 : 0) + (Module.m_negativeCacheEnabled ? 1 : 0) + "," +
               (Module.m_timerRunning ? 1 : 0) + (Module.m_cleanupRunning ? 1 : 0) + "," + (Active ? 1 : 0) +
               "," + (Module.m_CacheCleanTimer == null ? 0 : 1) + "," + Memory + "," + Negative + "," +
               Files + "," + FileMode + "," + Jobs.Count + "," + Show(Module.m_CacheDirectory);
    }
    public static void Main()
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        CultureInfo.CurrentCulture = new HookCulture();
        Reset(7, 1, 0);
        string line;
        while ((line = Console.ReadLine()) != null)
            Console.WriteLine(Run(line));
    }
}
