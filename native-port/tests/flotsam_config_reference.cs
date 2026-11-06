using System;
using System.IO;
using System.Globalization;
using Nini.Config;
using OpenSim.Framework;
namespace OpenSim.Framework
{
    class AssetBase
    {
    }
    static class Util
    {
        public static double GetTimeStampMS() => 0;
    }
}
class Logger
{
    public void Info(string s)
    {
    }
    public void Debug(string s)
    {
    }
}
class Commands
{
    public void AddCommand(string a, bool b, string c, string d, string e, Action<string, string[]> f)
    {
    }
}
class ConsoleStub
{
    public Commands Commands = new();
}
static class MainConsole
{
    public static ConsoleStub Instance = new();
}
partial class FlotsamAssetCache
{
    static Logger m_log = new();
    bool m_Enabled = false, m_FileCacheEnabled = true, m_MemoryCacheEnabled = false,
         m_negativeCacheEnabled = true;
    string m_CacheDirectory = "assetcache", m_assetLoader, m_assetLoaderArgs;
    double m_MemoryExpiration = .016;
    int m_negativeExpiration = 120, m_LogLevel = 0;
    ulong m_HitRateDisplay = 100;
    TimeSpan m_FileExpiration = TimeSpan.FromHours(48), m_FileExpirationCleanupTimer = TimeSpan.FromHours(1);
    const double m_DefaultFileExpiration = 48;
    const string m_ModuleName = "FlotsamAssetCache";
    static bool m_updateFileTimeOnCacheHit = false;
    static int m_CacheDirectoryTiers = 1, m_CacheDirectoryTierLen = 3, m_CacheWarnAt = 30000;
    ExpiringCacheOS<string, AssetBase> m_MemoryCache;
    ExpiringKey<string> m_negativeCache;
    static ExpiringKey<string> m_lastFileAccessTimeChange;
    public string Name => m_ModuleName;
    void HandleConsoleCommand(string m, string[] a)
    {
    }
    public string Snapshot() => BitConverter.DoubleToUInt64Bits(m_MemoryExpiration) +
                                "|" + m_negativeExpiration;
}
static class Program
{
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            try
            {
                var cfg = new IniConfigSource();
                cfg.AddConfig("Modules").Set("AssetCaching", "FlotsamAssetCache");
                var a = cfg.AddConfig("AssetCache");
                a.Set("FileCacheEnabled", "false");
                a.Set("MemoryCacheEnabled", "true");
                if (p[0] != ".")
                    a.Set("MemoryCacheTimeout", p[0]);
                if (p[1] != ".")
                    a.Set("NegativeCacheTimeout", p[1]);
                var cache = new FlotsamAssetCache();
                cache.Initialise(cfg);
                Console.WriteLine(cache.Snapshot());
            }
            catch
            {
                Console.WriteLine("ERR");
            }
        }
    }
}
