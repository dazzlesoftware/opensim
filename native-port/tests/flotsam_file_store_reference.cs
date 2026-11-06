using System;
using System.IO;
using System.Text;
using System.Threading;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Reflection;

#if REAL_FILES
using AssetBase=OpenSim.Framework.AssetBase;
#else
class AssetBase
{
    public string ID = "asset";
}

#endif
class Logger
{
    public void Warn(string s)
    {
        Probe.Add("L:" + Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant());
        Probe.Fail(17);
    }
}

static class osStringBuilderCache
{
    public static StringBuilder Acquire() => new();
    public static string GetStringAndRelease(StringBuilder s) => s.ToString();
}

class Worker
{
    public List<object> Jobs = new();
    public bool Enqueue(object job)
    {
        Probe.Add("Q");
        Probe.Fail(13);
        Jobs.Add(job);
        return true;
    }
}

class Writing
{
    public HashSet<string> Set = new();
    public bool Add(string p)
    {
        bool added = Set.Add(p);
        if ((Probe.Mode & 128) != 0)
            FlotsamAssetCache.Worker = null;
        return added;
    }

    public bool Remove(string p) => Set.Remove(p);
    public bool Contains(string p) => Set.Contains(p);
}

class Access
{
    public void Add(string s, int ms)
    {
        Probe.Add("N" + ms);
        Probe.Fail(9);
    }
}

static class HarnessPath
{
    public static char DirectorySeparatorChar => System.IO.Path.DirectorySeparatorChar;

    public static string Combine(string a, string b) => System.IO.Path.Combine(a, b);
    public static string GetDirectoryName(string s) => System.IO.Path.GetDirectoryName(s);
    public static string GetRandomFileName()
    {
#if REAL_FILES
 return System.IO.Path.GetRandomFileName();
#else
        Probe.Add("P");
        Probe.Fail(2);
        return "temp.tmp";
#endif
    }
}

partial class FlotsamAssetCache
{
    struct WriteAssetInfo
    {
        public string filename;
        public AssetBase asset;
        public bool replace;
    }

    static Logger m_log = new();
    static Writing m_CurrentlyWriting = new();
    static Worker m_assetFileWriteWorker;
    static bool m_updateFileTimeOnCacheHit;
    static Access m_lastFileAccessTimeChange;
    static int m_CacheDirectoryTiers = 1, m_CacheDirectoryTierLen = 3;
    string m_CacheDirectory = "cache";
    char[] m_InvalidChars;
    ulong m_RequestsForInprogress, m_DiskHits;
    public FlotsamAssetCache()
    {
        var chars = new List<char>();
        chars.AddRange(System.IO.Path.GetInvalidPathChars());
        chars.AddRange(System.IO.Path.GetInvalidFileNameChars());
        m_InvalidChars = chars.ToArray();
    }

    public static Worker Worker { get => m_assetFileWriteWorker; set => m_assetFileWriteWorker = value; }

    static void UpdateFileLastAccessTime(string s)
    {
        Probe.Add("T");
        Probe.Fail(16);
    }

    public void Setup(int mode, int tiers = 1, int len = 3)
    {
        m_CacheDirectoryTiers = Math.Clamp(tiers, 1, 3);
        m_CacheDirectoryTierLen = Math.Clamp(len, 1, 4);
        m_CurrentlyWriting = new();
        m_assetFileWriteWorker = (mode & 32) != 0 ? null : new Worker();
        m_updateFileTimeOnCacheHit = (mode & 4) != 0;
        m_lastFileAccessTimeChange = (mode & 16) != 0 ? new() : null;
    }

    public void Root(string s)
    {
        m_CacheDirectory = s;
    }

    public string Name(string id) => GetFileName(id);
    public void Seed(string id)
    {
        m_CurrentlyWriting.Set.Add(GetFileName(id));
    }

    public int Count => m_CurrentlyWriting.Set.Count;
    public string Stats => m_RequestsForInprogress + "," + m_DiskHits;

    public void Put(string id, AssetBase asset, bool replace) => UpdateFileCache(id, asset, replace);
    public AssetBase Get(string id) => GetFromFileCache(id);
    public bool Check(string id) => CheckFromFileCache(id);
    public void Drain(Worker worker)
    {
        if (worker != null)
            while (worker.Jobs.Count > 0)
            {
                var job = worker.Jobs[0];
                worker.Jobs.RemoveAt(0);
                ProcessWrites(job);
            }
    }
}

#if !REAL_FILES
class FakeStream : Stream
{
    public override bool CanRead => true;
    public override bool CanSeek => true;
    public override bool CanWrite => true;

    public override long Length
    {
        get
        {
            Probe.Add("H");
            Probe.Fail(14);
            return (Probe.Mode & 512) != 0 ? 0 : 1;
        }
    }

    public override long Position { get; set; }

    public override void Flush()
    {
        Probe.Add("F");
        Probe.Fail(7);
    }

    protected override void Dispose(bool disposing)
    {
        Probe.Add("X");
        Probe.Fail(8);
        base.Dispose(disposing);
    }

    public override int Read(byte[] b, int o, int c) => 0;
    public override long Seek(long o, SeekOrigin s) => 0;
    public override void SetLength(long l)
    {
    }

    public override void Write(byte[] b, int o, int c)
    {
    }
}

static class FakeFile
{
    public static bool Exists(string p)
    {
        Probe.Add("E");
        Probe.Fail(1);
        return Probe.Target != "";
    }

    public static FakeStream Open(string p, FileMode mode)
    {
        Probe.Add("O");
        Probe.Fail(5);
        Probe.Temp = "empty";
        return new();
    }

    public static FakeStream Open(string p, FileMode mode, FileAccess access, FileShare share)
    {
        Probe.Add("O");
        Probe.Fail(5);
        return new();
    }

    public static void Delete(string p)
    {
        bool temporary = System.IO.Path.GetFileName(p) == "temp.tmp";
        Probe.Add(temporary ? "r" : "R");
        Probe.Fail(temporary ? 12 : 10);
        if (temporary)
            Probe.Temp = "";
        else
            Probe.Target = "";
    }

    public static void Move(string a, string b)
    {
        Probe.Add("M");
        Probe.Fail(11);
        if (Probe.Temp == "" || Probe.Target != "")
            throw new IOException("move");
        Probe.Target = Probe.Temp;
        Probe.Temp = "";
    }
}

static class FakeDirectory
{
    public static bool Exists(string p)
    {
        Probe.Add("D");
        Probe.Fail(3);
        return Probe.Directory;
    }

    public static void CreateDirectory(string p)
    {
        Probe.Add("C");
        Probe.Fail(4);
        Probe.Directory = true;
    }
}

class FakeFormatter
{
    public void Serialize(Stream s, object a)
    {
        Probe.Add("S");
        Probe.Fail(6);
        Probe.Temp = "new";
        if ((Probe.Mode & 256) != 0)
            Probe.Target = "race";
    }

    public object Deserialize(Stream s)
    {
        Probe.Add("B");
        Probe.Fail(15);
        return (Probe.Mode & 1024) != 0 ? null : new AssetBase();
    }
}

#endif
static class Probe
{
    public static int Mode, Point, Kind;
    public static string Trace, Target, Temp;
    public static bool Directory;
    public static void Add(string s)
    {
        Trace += "|" + s;
    }

    public static void Fail(int p)
    {
        if (p != Point)
            return;
        throw Kind switch
        {
            0 => new IOException("failure"),
            1 => new UnauthorizedAccessException("failure"),
            2 => new System.Runtime.Serialization.SerializationException("failure"),
            4 => new FileNotFoundException("failure"),
            5 => new DirectoryNotFoundException("failure"),
            _ => new Exception("failure")};
    }

    public static string Run(int op, int mode, int point, int kind, int shortId)
    {
        Mode = mode;
        Point = point;
        Kind = kind;
        Trace = "";
        Target = (mode & 1) != 0 ? "old" : "";
        Temp = "";
        Directory = (mode & 8) != 0;
        var cache = new FlotsamAssetCache();
        cache.Setup(mode);
        var worker = FlotsamAssetCache.Worker;
        string id = shortId != 0 ? "x" : "abcdefghijklmnop", result = "done";
        try
        {
            if ((mode & 64) != 0)
                cache.Seed(id);
            if (op == 0 || op == 3)
            {
                cache.Put(id, new AssetBase(), (mode & 2) != 0);
                if (op == 3)
                {
                    var a = cache.Get(id);
                    result = a == null ? "-" : a.ID;
                }
                else
                    cache.Drain(worker);
            }
            else if (op == 1)
            {
                var a = cache.Get(id);
                result = a == null ? "-" : a.ID;
            }
            else if (op == 2)
                result = cache.Check(id) ? "1" : "0";
        }
        catch
        {
            result = "EX";
        }

        return result + Trace + "|state:" + cache.Count + "," + (worker?.Jobs.Count ?? 0) + "," + (Directory ? 1 : 0) + "," + Target + "," + Temp + "," + cache.Stats;
    }
}

static class FileProgram
{
    static string Decode(string s) => Encoding.UTF8.GetString(Convert.FromHexString(s));
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            try
            {
#if REAL_FILES
 if(p[0]=="emit"){var a=(AssetBase)typeof(Program).GetMethod("Make",BindingFlags.NonPublic|BindingFlags.Static).Invoke(null,new object[]{int.Parse(p[1])});var bytes=(byte[])typeof(Program).GetMethod("Serialize",BindingFlags.NonPublic|BindingFlags.Static).Invoke(null,new object[]{a});Console.WriteLine(Convert.ToHexString(bytes).ToLowerInvariant());continue;}if(p[0]=="decode"){using var stream=new MemoryStream(Convert.FromHexString(p[1]));var a=(AssetBase)new System.Runtime.Serialization.Formatters.Binary.BinaryFormatter().Deserialize(stream);Console.WriteLine((string)typeof(Program).GetMethod("Snapshot",BindingFlags.NonPublic|BindingFlags.Static).Invoke(null,new object[]{a}));continue;}
 Probe.Mode=Probe.Point=Probe.Kind=0;Probe.Trace="";var cache=new FlotsamAssetCache();cache.Setup(0);cache.Root(Decode(p[1]));var id=Decode(p[2]);if(p[0]=="put"){var a=(AssetBase)typeof(Program).GetMethod("Make",BindingFlags.NonPublic|BindingFlags.Static).Invoke(null,new object[]{int.Parse(p[3])});cache.Put(id,a,p[4]=="1");cache.Drain(FlotsamAssetCache.Worker);Console.WriteLine(cache.Check(id)?"OK":"ERR");}else{var a=cache.Get(id);Console.WriteLine(a==null?"NULL":(string)typeof(Program).GetMethod("Snapshot",BindingFlags.NonPublic|BindingFlags.Static).Invoke(null,new object[]{a}));}
#else
                if (p[0] == "path")
                {
                    var cache = new FlotsamAssetCache();
                    cache.Setup(0, int.Parse(p[1]), int.Parse(p[2]));
                    Console.WriteLine(Convert.ToHexString(MemoryMarshal.AsBytes(cache.Name(Decode(p[3])).AsSpan())).ToLowerInvariant());
                }
                else
                    Console.WriteLine(Probe.Run(int.Parse(p[1]), int.Parse(p[2]), int.Parse(p[3]), int.Parse(p[4]), int.Parse(p[5])));
#endif
            }
            catch
            {
                Console.WriteLine("EX");
            }
        }
    }
}
