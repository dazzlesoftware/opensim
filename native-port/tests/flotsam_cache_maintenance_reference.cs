using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using OpenMetaverse;
static class FakeThread
{
    public static void Sleep(int n)
    {
        Probe.Log("S:" + n);
        Probe.Hook(9);
    }
}
static class FakePath
{
    public static string Combine(string dir, string name)
    {
        Probe.Log("P");
        Probe.Hook(1);
        return System.IO.Path.Combine(dir, name);
    }
}
static class FakeDate
{
    public static System.DateTime Now
    {
        get {
            Probe.Log("N");
            Probe.Hook(3);
            return new(638712864000000000, DateTimeKind.Utc);
        }
    }
}
static class FakeFile
{
    public static void Delete(string path)
    {
        Probe.Log("DF:" + path);
        Probe.Hook(18);
        if (Probe.Real)
            System.IO.File.Delete(path);
    }
    public static bool Exists(string path)
    {
        Probe.Log("E");
        Probe.Hook(2);
        if (Probe.Profile == 8)
            throw new Exception("operation");
        return Probe.Real ? System.IO.File.Exists(path) : Probe.Exists;
    }
    public static void SetLastWriteTime(string path, System.DateTime time)
    {
        Probe.Log("T:" + time.Ticks);
        Probe.Hook(4);
        if (Probe.Real)
            System.IO.File.SetLastWriteTime(path, time);
        Probe.Ticks = time.Ticks;
    }
    public static void WriteAllText(string path, string text)
    {
        Probe.Log("W:" + text);
        if (Probe.Profile == 2)
        {
            Probe.Exists = true;
            Probe.Text = "partial";
        }
        Probe.Hook(5);
        if (Probe.Real)
            System.IO.File.WriteAllText(path, text);
        Probe.Exists = true;
        Probe.Text = text;
    }
}
class FileCounts : System.Collections.Generic.IEnumerable<string>
{
    public int Length;
    public string[] Values = new[] { "cache/one", "cache/two", "cache/status.fac" };
    public IEnumerator<string> GetEnumerator() => ((IEnumerable<string>)Values).GetEnumerator();
    System.Collections.IEnumerator System.Collections.IEnumerable.GetEnumerator() => GetEnumerator();
}
static class FakeDirectory
{
    public static bool Exists(string path)
    {
        Probe.Log("DE");
        Probe.Hook(16);
        return Probe.Real ? System.IO.Directory.Exists(path) : Probe.Profile != 4;
    }
    public static void Delete(string path, bool recursive)
    {
        Probe.Log("DD:" + path);
        Probe.Hook(17);
        if (Probe.Real)
            System.IO.Directory.Delete(path, recursive);
    }

    public static string[] GetDirectories(string path)
    {
        Probe.Log("D:" + path);
        Probe.Hook(7);
        if (Probe.Profile == 4)
            throw new DirectoryNotFoundException("missing");
        if (Probe.Real)
            return System.IO.Directory.GetDirectories(path);
        return Probe.Dirs.TryGetValue(path, out var value) ? value : Array.Empty<string>();
    }
    public static FileCounts GetFiles(string path)
    {
        Probe.Log("F:" + path);
        Probe.Hook(8);
        if (Probe.Profile == 5 && path == "cache")
            throw new Exception("files");
        return new() { Length = Probe.Real ? System.IO.Directory.GetFiles(path).Length
                                : Probe.Counts.TryGetValue(path, out int n) ? n
                                                                            : 0 };
    }
}
class Logger
{
    public void Info(string text)
    {
        Probe.Log("I:" + text);
        Probe.Hook(10);
    }
    public void Warn(string text)
    {
        Probe.Log("L:" + text);
        Probe.Hook(6);
    }
}
class Service
{
    public AssetBase Get(string id)
    {
        Probe.Log("G:" + Probe.Number(new UUID(id)));
        Probe.Hook(15);
        return null;
    }
}
class AssetBase
{
}
partial class FlotsamAssetCache
{
    string m_CacheDirectory = "cache";
    Logger m_log = new();
    Service m_AssetService = new();
    bool m_cleanupRunning
    {
        get {
            Probe.Log("R");
            Probe.Hook(12);
            return Probe.Running;
        }
    }
    string GetFileName(string id)
    {
        Probe.Log("K:" + Probe.Number(new UUID(id)));
        Probe.Hook(13);
        return "file" + Probe.Number(new UUID(id));
    }
    bool CheckUpdateFileLastAccessTime(string path)
    {
        Probe.Log("A:" + path);
        Probe.Hook(14);
        return Probe.Profile == 5 ? false : Probe.Profile == 6 ? int.Parse(path.Substring(4)) % 2 == 0 : true;
    }
    Dictionary<UUID, sbyte> GatherSceneAssets()
    {
        Probe.Log("Q");
        Probe.Hook(11);
        return Probe.Profile == 3 ? null : Probe.Assets;
    }
    public void Root(string dir)
    {
        m_CacheDirectory = dir;
    }
    public string Call(int op, bool get)
    {
        if (op == 0)
        {
            StampRegionStatusFile(Probe.Id(1));
            return "OK";
        }
        if (op == 1)
            return GetFileCacheCount(m_CacheDirectory).ToString();
        if (op == 2)
            return TouchAllSceneAssets(get).ToString();
        ClearFileCache();
        return "OK";
    }
}
static class Probe
{
    public static bool Real;
    public static int Profile;
    public static bool Exists, Running;
    public static string Text;
    public static long Ticks;
    public static Dictionary<string, string[]> Dirs = new();
    public static Dictionary<string, int> Counts = new();
    public static Dictionary<UUID, sbyte> Assets = new();
    static int point, nth, seen, cancel, cancelNth, cancelSeen, mutation, mutPoint, mutNth, mutSeen;
    static List<string> trace = new();
    public static UUID Id(int n) => new("00000000-0000-0000-0000-" + n.ToString("x12"));
    public static int Number(UUID id) => Convert.ToInt32(id.ToString().Substring(24), 16);
    public static void Log(string s) => trace.Add(s);
    public static void Hook(int p)
    {
        if (p == cancel && ++cancelSeen == cancelNth)
            Running = false;
        if (p == mutPoint && ++mutSeen == mutNth)
        {
            switch (mutation)
            {
            case 1:
                Assets.Clear();
                break;
            case 2:
                Assets.Remove(Id(2));
                break;
            case 3:
                Assets[Id(90)] = 0;
                break;
            case 4:
                if (Assets.ContainsKey(Id(3)))
                    Assets[Id(3)] = 1;
                break;
            }
        }
        if (p == point && ++seen == nth)
            throw new Exception("fault");
    }
    static string Run(string line)
    {
        var a = line.Split(' ').Select(int.Parse).ToArray();
        int op = a[0];
        Profile = a[1];
        point = a[2];
        nth = a[3];
        seen = 0;
        cancel = a[4];
        cancelNth = a[5];
        cancelSeen = 0;
        mutation = a[6];
        mutPoint = a[7];
        mutNth = a[8];
        mutSeen = 0;
        bool get = a[9] != 0;
        Running = a[10] != 0;
        Exists = Profile % 2 != 0;
        Text = Exists ? "old" : "";
        Ticks = 0;
        Dirs.Clear();
        Counts.Clear();
        Assets.Clear();
        if (Profile == 1)
        {
            var dirs = Enumerable.Range(0, 55).Select(i => "cache/d" + i).ToArray();
            Dirs["cache"] = dirs;
            foreach (var d in dirs)
                Counts[d] = 1;
        }
        else if (Profile == 2)
        {
            Dirs["cache"] = new[] { "cache/a" };
            Dirs["cache/a"] = new[] { "cache/a/a" };
            Counts["cache/a/a"] = 3;
        }
        else
        {
            Dirs["cache"] = new[] { "cache/a", "cache/b" };
            Counts["cache"] = Profile == 3 ? int.MaxValue : 3;
            Counts["cache/a"] = 2;
            Counts["cache/b"] = 3;
        }
        foreach (int n in Profile == 1 ? Enumerable.Range(1, 55) : new[] { 3, 1, 2 })
            Assets[Id(n)] = 0;
        if (Profile == 2)
            Assets.Clear();
        if (Profile == 4)
        {
            Assets.Remove(Id(1));
            Assets[Id(4)] = 0;
        }
        var cache = new FlotsamAssetCache();
        string result;
        try
        {
            result = cache.Call(op, get);
        }
        catch
        {
            result = "EX";
        }
        return result + "|" +
               Convert.ToHexString(Encoding.UTF8.GetBytes(string.Join(";", trace))).ToLowerInvariant() + "|" +
               (Running ? 1 : 0) + ":" + (Exists ? 1 : 0) + ":" + Ticks + ":" +
               Convert.ToHexString(Encoding.UTF8.GetBytes(Text)).ToLowerInvariant() + "|" +
               string.Join(",", Assets.Select(p => Number(p.Key) + ":" + p.Value));
    }
    static void Filesystem(string root, bool locked)
    {
        Real = true;
        Profile = 0;
        var cache = new FlotsamAssetCache();
        cache.Root(root);
        var status = System.IO.Path.Combine(root, "RegionStatus_" + Id(1) + ".fac");
        FileStream blocker =
            locked ? new FileStream(status, FileMode.Open, FileAccess.ReadWrite, FileShare.None) : null;
        try
        {
            cache.Call(0, false);
            cache.Call(0, false);
        }
        finally
        {
            blocker?.Dispose();
        }
        bool exists = System.IO.File.Exists(status);
        var ticks = exists ? System.IO.File.GetLastWriteTimeUtc(status).Ticks : 0;
        var data = exists ? Convert.ToHexString(System.IO.File.ReadAllBytes(status)).ToLowerInvariant() : "-";
        var warnings = string.Join(";", trace.Where(s => s.StartsWith("L:")));
        Console.WriteLine((exists ? 1 : 0) + "|" + ticks + "|" + data + "|" + cache.Call(1, false) + "|" +
                          Convert.ToHexString(Encoding.UTF8.GetBytes(warnings)).ToLowerInvariant());
    }
    public static void Main(string[] args)
    {
        if (args.Length > 0)
        {
            Filesystem(Encoding.UTF8.GetString(Convert.FromHexString(args[1])),
                       args.Length > 2 && args[2] == "1");
            return;
        }
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            trace.Clear();
            Console.WriteLine(Run(line));
        }
    }
}
