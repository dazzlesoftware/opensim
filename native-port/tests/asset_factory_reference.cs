// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using Nini.Config;
using OpenMetaverse;
using OpenSim.Data;
using OpenSim.Framework;
using Original = OpenSim.Services.AssetService.AssetService;
namespace log4net
{
    public interface ILog
    {
        void DebugFormat(string format, params object[] args);
        void Debug(object value);
        void WarnFormat(string format, params object[] args);
        void ErrorFormat(string format, params object[] args);
        void Error(object value, Exception e);
    }
    public class Log : ILog
    {
        public void DebugFormat(string f,
                                params object[] args) => Host.C.Event("debug:" + string.Format(f, args));
        public void Debug(object v) => Host.C.Event("debug:" + v);
        public void WarnFormat(string f,
                               params object[] args) => Host.C.Event("warn:" + string.Format(f, args));
        public void ErrorFormat(string f, params object[] args)
        {
            for (int i = 0; i < args.Length; ++i)
                if (args[i] is Exception)
                    args[i] = "";
            Host.C.Event("error:" + string.Format(f, args));
        }
        public void Error(object v, Exception e) => Host.C.Event("error:" + v);
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type type) => new Log();
    }
}
namespace OpenSim.Services.Base
{
    public class ServiceBase
    {
        public ServiceBase(IConfigSource source)
        {
        }
        public T LoadPlugin<T>(string module)
        {
            if (typeof(T) == typeof(IAssetDataPlugin))
            {
                Host.C.Event("load-store:" + module);
                return module == "missing" ? default : (T)(object)Host.C.Data;
            }
            Host.C.Event("load-loader:" + module);
            return module == "missing" || Host.C.LoaderMode == 1 ? default : (T)(object)Host.C.Loader;
        }
    }
}
namespace OpenSim.Framework
{
    public static class Util
    {
        public static string SHA1Hash(byte[] bytes)
        {
            Host.C.Event("hash:" + Host.Bytes(bytes));
            return Convert.ToHexString(SHA1.HashData(bytes)).ToLowerInvariant();
        }
    }
}
static class Host
{
    public static Context C;
    public static string Hex(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Bytes(byte[] bytes) => bytes == null ? "N"
                                                              : Convert.ToHexString(bytes).ToLowerInvariant();
    public static string Text(string s) => s == "." ? "" : Encoding.UTF8.GetString(Convert.FromHexString(s));
    public const string AssetId = "00000000-0000-0000-0000-000000000001";
    public static AssetBase Asset(string id, byte[] data) => new AssetBase { ID = id, Data = data };
}
class Context
{
    public string Trace = "", Fail;
    public int Mode, LoaderMode;
    public AssetBase Current;
    public Data Data;
    public Loader Loader;
    public void Event(string s)
    {
        Trace += s + ";";
        if (s == Fail)
            throw new Exception("injected");
    }
}
class Data : IAssetDataPlugin
{
    public void Initialise(string connection) => Host.C.Event("initialize:" + connection);
    public void Initialise()
    {
    }
    public void Dispose() => Host.C.Event("dispose");
    public string Name => "Data";
    public string Version => "0";
    public AssetBase GetAsset(UUID id)
    {
        Host.C.Event("get:" + id);
        if (Host.C.Mode == 4)
            throw new Exception("get");
        return Host.C.Current;
    }
    public bool StoreAsset(AssetBase a)
    {
        Host.C.Event("store:" + a.ID + ":" + Host.Bytes(a.Data));
        Host.C.Current = a;
        return false;
    }
    public bool[] AssetsExist(UUID[] ids) => Array.Empty<bool>();
    public List<AssetMetadata> FetchAssetMetadataSet(int start, int count) => new();
    public bool Delete(string id) => false;
}
class Loader : IAssetLoader
{
    public void ForEachDefaultXmlAsset(string args, Action<AssetBase> action)
    {
        var c = Host.C;
        c.Event("loader:" + args);
        if (c.LoaderMode == 3)
        {
            action(null);
            return;
        }
        action(Host.Asset(c.LoaderMode == 5 ? "bad" : Host.AssetId,
                          c.LoaderMode == 4 ? Array.Empty<byte>() : new byte[] { 1, 2 }));
        if (c.LoaderMode == 2)
            throw new Exception("loader");
        if (c.LoaderMode == 6)
            action(Host.Asset(Host.AssetId, new byte[] { 5 }));
    }
}
class AssetFactoryReference
{
    static FieldInfo Root =
        typeof(Original).GetField("m_RootInstance", BindingFlags.Static | BindingFlags.NonPublic);
    static string Section(Original s) => (string) typeof(OpenSim.Services.AssetService.AssetServiceBase)
                                             .GetField("m_ConfigName",
                                                       BindingFlags.Instance | BindingFlags.NonPublic)
                                             .GetValue(s);
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            if (p[0] == "h")
            {
                try
                {
                    Console.WriteLine(
                        Convert
                            .ToHexString(SHA1.HashData(p[1] == "N"   ? null
                                                       : p[1] == "." ? Array.Empty<byte>()
                                                                     : Convert.FromHexString(p[1])))
                            .ToLowerInvariant());
                }
                catch
                {
                    Console.WriteLine("EX");
                }
                continue;
            }
            var c = new Context { Mode = int.Parse(p[1]), LoaderMode = int.Parse(p[2]),
                                  Fail = p[4] == "-" ? "" : Host.Text(p[4]) };
            Host.C = c;
            c.Data = new Data();
            c.Loader = new Loader();
            if (c.Mode != 0)
                c.Current = Host.Asset(Host.AssetId, c.Mode == 3   ? null
                                                     : c.Mode == 1 ? new byte[] { 1, 2 }
                                                                   : new byte[] { 3, 4 });
            Root.SetValue(null, null);
            Original last = null;
            var config = new IniConfigSource("config-" + p[0] + ".ini");
            string output = "";
            foreach (char op in p[3])
            {
                string result = "-";
                try
                {
                    switch (op)
                    {
                    case 'I':
                        last = new Original(config);
                        break;
                    case 'E':
                        last = new Original(config, "");
                        break;
                    case 'J':
                        last = new Original(config, "Other");
                        break;
                    case 'N':
                        last = new Original(config, null);
                        break;
                    case 'C':
                        last = new Original(null);
                        break;
                    case 'R':
                        Root.SetValue(null, null);
                        break;
                    case 'G':
                        result = ((Original)Root.GetValue(null)).Get(Host.AssetId) == null ? "N" : "A";
                        break;
                    }
                }
                catch
                {
                    result = "EX";
                }
                var root = (Original)Root.GetValue(null);
                output += op + ":" + Host.Hex(result) + ":" + (root != null ? 1 : 0) +
                          (last != null ? 1 : 0) + (ReferenceEquals(root, last) ? 1 : 0) + ":" +
                          (root == null ? "N" : Host.Hex(Section(root))) + ":" +
                          (c.Current == null ? "N" : Host.Bytes(c.Current.Data)) + ";";
            }
            Console.WriteLine(output + "|" + Host.Hex(c.Trace));
            Root.SetValue(null, null);
        }
    }
}
