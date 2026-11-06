// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.IO;
using System.Globalization;
using System.Text;
using System.Reflection;
using System.Data.SQLite;
using OpenMetaverse;
using OpenSim.Framework;
using OpenSim.Data.SQLite;
namespace log4net
{
    public interface ILog
    {
        void WarnFormat(string format, params object[] args);
    }
    public class Log : ILog
    {
        public void WarnFormat(string format, params object[] args)
        {
            AssetStorageReference.Warnings += string.Format(format, args) + ";";
            if (AssetStorageReference.WarnFailure)
                throw new Exception("warn");
        }
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type type) => new Log();
    }
}
namespace OpenSim.Framework
{
    public static class DllmapConfigHelper
    {
        public static void RegisterAssembly(Assembly a)
        {
        }
    }
}
namespace OpenSim.Data
{
    public class Migration
    {
        SQLiteConnection c;
        public Migration(SQLiteConnection value, Assembly assembly, string name)
        {
            c = value;
        }
        public void Update()
        {
            using var command = c.CreateCommand();
            command.CommandText = File.ReadAllText("schema.sql");
            command.ExecuteNonQuery();
        }
    }
}
class AssetStorageReference
{
    public static string Warnings = "";
    public static bool WarnFailure;
    static string Text(string value) => value == "N" ? null
                                        : value == "."
                                            ? ""
                                            : Encoding.UTF8.GetString(Convert.FromHexString(value));
    static string Hex(string value) => Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
    static string Token(string value) => value == null ? "N" : "s" + Hex(value);
    static string Bytes(byte[] b) => b == null ? "N" : Convert.ToHexString(b).ToLowerInvariant();
    static string Snapshot(AssetMetadata m) => m == null
                                                   ? "N"
                                                   : m.FullID + ":" + Token(m.ID) + ":" + Token(m.Name) +
                                                         ":" + Token(m.Description) + ":" + m.Type + ":" +
                                                         (int)m.Flags + ":" + (m.Local ? 1 : 0) + ":" +
                                                         (m.Temporary ? 1 : 0) + ":" + Token(m.CreatorID) +
                                                         ":" + Bytes(m.SHA1);
    static string Snapshot(AssetBase a) => a == null ? "N" : Snapshot(a.Metadata) + "/" + Bytes(a.Data);
    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        var store = new SQLiteAssetData();
        string connection = "Data Source=" + args[0] + ";Pooling=False";
        store.Initialise(connection);
        AssetBase asset = new();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            string result = "-";
            Warnings = "";
            try
            {
                switch (p[0])
                {
                case "n":
                    asset = new AssetBase(Text(p[1]), Text(p[2]), sbyte.Parse(p[4]),
                                          Text(p[8])) { Description = Text(p[3]),
                                                        Flags = (AssetFlags) int.Parse(p[5]),
                                                        Local = p[6] != "0", Temporary = p[7] != "0",
                                                        Data = p[9] == "N"   ? null
                                                               : p[9] == "." ? Array.Empty<byte>()
                                                                             : Convert.FromHexString(p[9]) };
                    result = Snapshot(asset);
                    break;
                case "s":
                    result = store.StoreAsset(asset) ? "T" : "F";
                    break;
                case "g":
                    result = Snapshot(store.GetAsset(UUID.Parse(Text(p[1]))));
                    break;
                case "e":
                    var ids = new UUID[p.Length - 1];
                    for (int i = 1; i < p.Length; ++i)
                        ids[i - 1] = UUID.Parse(Text(p[i]));
                    result = "";
                    foreach (bool b in store.AssetsExist(ids))
                        result += b ? '1' : '0';
                    break;
                case "m":
                    var rows = store.FetchAssetMetadataSet(int.Parse(p[1]), int.Parse(p[2]));
                    result = rows.Count.ToString();
                    foreach (var row in rows)
                        result += "|" + Snapshot(row);
                    break;
                case "d":
                    result = store.Delete(Text(p[1])) ? "T" : "F";
                    break;
                case "a":
                    result = store.DeleteAsset(UUID.Parse(Text(p[1]))) ? "T" : "F";
                    break;
                case "c":
                    store.Dispose();
                    break;
                case "o":
                    store = new SQLiteAssetData();
                    store.Initialise(connection);
                    break;
                case "f":
                    WarnFailure = !WarnFailure;
                    break;
                case "asset":
                    result = Snapshot(asset);
                    break;
                case "raw":
                    using (var db = new SQLiteConnection(connection))
                    {
                        db.Open();
                        using var command = db.CreateCommand();
                        command.CommandText = Text(p[1]);
                        command.ExecuteNonQuery();
                    }
                    break;
                default:
                    throw new Exception("operation");
                }
            }
            catch
            {
                result = "EX";
            }
            Console.WriteLine(result + "#" + Hex(Warnings));
        }
        store.Dispose();
    }
}
