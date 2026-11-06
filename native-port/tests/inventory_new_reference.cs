using System;
using System.Text;
using OpenMetaverse;
using OpenSim.Framework;
namespace OpenSim.Framework
{
    class InventoryFolderBase
    {
        public UUID Owner;
    }
    class AssetBase
    {
        public UUID FullID;
        public sbyte Type;
    }
}
interface IClientAPI
{
    UUID AgentId { get; }
}
class Client : IClientAPI
{
    public UUID AgentId => Reference.Id(1);
}
interface IAgentAssetTransactions
{
    bool HandleItemCreationFromTransaction(IClientAPI c, UUID t, UUID f, uint cb, string d, string n, sbyte i,
                                           sbyte a, byte sub, uint next);
}
interface IEnvironmentModule
{
    UUID GetDefaultAsset(byte sub);
}
class Transactions : IAgentAssetTransactions
{
    public bool HandleItemCreationFromTransaction(IClientAPI c, UUID t, UUID f, uint cb, string d, string n,
                                                  sbyte i, sbyte a, byte sub, uint next)
    {
        Reference.Step(6, "HANDLE:" + Reference.Request(t, f, cb, d, n, i, a, sub, next));
        return Reference.Bit(128);
    }
}
class EnvironmentModule : IEnvironmentModule
{
    public UUID GetDefaultAsset(byte sub)
    {
        Reference.Step(12, "DEFAULT:" + sub);
        return Reference.Bit(2048) ? UUID.Zero : Reference.Id(8);
    }
}
class ScenePresence
{
    public Vector3 AbsolutePosition;
    public Scene Scene = new();
    public ulong RegionHandle;
}
class RegionInfo
{
    public UUID RegionID = Reference.Id(4);
}
class Permissions
{
    public bool CanCreateUserInventory(sbyte i, UUID a)
    {
        Reference.Step(2, "CAN:" + i + ":" + a);
        return !Reference.Bit(1);
    }
}
class InventoryService
{
    public InventoryFolderBase GetFolder(UUID a, UUID key)
    {
        Reference.Step(3, "FOLDER:" + a + ":" + key);
        return Reference.Bit(2) ? null : new() { Owner = Reference.Id(Reference.Bit(4) ? 9 : 1) };
    }
    public InventoryFolderBase GetFolderForType(UUID a, FolderType type)
    {
        Reference.Step(4, "TYPE:" + a + ":" + (sbyte)type);
        return Reference.Bit(8) ? null : new() { Owner = Reference.Id(Reference.Bit(16) ? 9 : 1) };
    }
}
class AssetService
{
    public string Store(AssetBase a)
    {
        Reference.Step(10, "STORE:" + (a == null ? "NULL" : a.FullID + ":" + a.Type));
        if (a != null && Reference.Bit(8192))
        {
            a.FullID = Reference.Id(9);
            a.Type = 13;
        }
        return "IGNORED";
    }
}
class Scene
{
    public RegionInfo RegionInfo = new();
    public Permissions Permissions = new();
    public InventoryService InventoryService = new();
    public AssetService AssetService = new();
    public IAgentAssetTransactions AgentTransactionsModule
    {
        get {
            Reference.Step(5, "TRANSACTIONS");
            return Reference.Bit(64) ? null : new Transactions();
        }
    }
    public bool TryGetScenePresence(UUID a, out ScenePresence p)
    {
        Reference.Step(7, "PRESENCE:" + a);
        p = Reference.Bit(512) ? null : new();
        return !Reference.Bit(256);
    }
    public T RequestModuleInterface<T>()
        where T : class
    {
        Reference.Step(11, "ENVIRONMENT");
        return Reference.Bit(1024) ? null : new EnvironmentModule() as T;
    }
    public AssetBase CreateAsset(string n, string d, sbyte type, byte[] data, UUID agent)
    {
        Reference.Step(9, "ASSET:" + Reference.Hex(n) + ":" + Reference.Hex(d) + ":" + type + ":" +
                              Convert.ToHexString(data).ToLowerInvariant() + ":" + agent);
        return Reference.Bit(4096) ? null : new() { FullID = Reference.Id(7), Type = type };
    }
    public void CreateNewInventoryItem(IClientAPI c, string creator, string creatorData, UUID folder,
                                       string n, string d, uint flags, uint cb, UUID asset, sbyte at,
                                       sbyte it, uint b, uint current, uint everyone, uint next, uint group,
                                       int date)
    {
        Reference.Step(13, "CREATE:" +
                               Reference.Request(Reference.Transaction, folder, cb, d, n, it, at,
                                                 Reference.Subtype, next) +
                               ":" + asset + ":" + flags + ":" + everyone + ":" + group + ":" + date + ":" +
                               creator + ":" + Reference.Hex(creatorData) + ":" + b + ":" + current);
    }
}
class Logger
{
    public void DebugFormat(string s, params object[] a) => Reference.Step(1, s.Contains("Requested folder")
                                                                                  ? "FALLBACK"
                                                                                  : "REQUEST");
    public void ErrorFormat(string s, params object[] a) => Reference.Step(14, s.Contains("ScenePresence")
                                                                                   ? "PRESENCE"
                                                                                   : "ENVIRONMENT");
}
partial class InventoryAccessModule
{
    Scene m_Scene = new();
    Logger m_log = new();
    protected virtual string GenerateLandmark(ScenePresence p, out string prefix, out string suffix)
    {
        Reference.Step(8, "LANDMARK:" + Reference.Id(1));
        prefix = Reference.Bit(131072) ? null : "HG ";
        suffix = Reference.Bit(131072) ? null : " @ home";
        return Reference.Bit(32768) ? null : Reference.Bit(16384) ? "é 脚 😃" : "Landmark data\n";
    }
}
class Reference
{
    public static int Mode, Failure;
    public static byte Subtype;
    public static UUID Transaction;
    static string Trace;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static bool Bit(int n) => (Mode & n) != 0;
    public static void Step(int n, string s)
    {
        Trace += "|" + s;
        if (n == Failure)
            throw new Exception();
    }
    public static string Hex(string s) =>
        s == null ? "NULL" : Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Request(UUID tx, UUID folder, uint cb, string d, string n, sbyte it, sbyte at,
                                 byte sub,
                                 uint next) => $"{tx}:{folder}:{cb}:{Hex(d)}:{Hex(n)}:{it}:{at}:{sub}:{next}";
    static string Probe(int mode, int it, int at, int sub, int next, int fail)
    {
        Mode = mode;
        Failure = fail;
        Subtype = unchecked((byte)sub);
        Transaction = Bit(32) ? UUID.Zero : Id(2);
        Trace = "";
        string result = "OK";
        try
        {
            new InventoryAccessModule().CreateNewInventoryItem(
                new Client(), Transaction, Id(3), 99, Bit(65536) ? null : "Description",
                Bit(65536)   ? null
                : Bit(16384) ? "脚本 😃"
                             : "New item",
                unchecked((sbyte)it), unchecked((sbyte)at), Subtype, unchecked((uint)next), -222);
        }
        catch
        {
            result = "THROW";
        }
        return result + Trace;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            if (line.StartsWith("L "))
            {
                var v = line.Split(' ');
                var presence = new ScenePresence { AbsolutePosition =
                                                       new(BitConverter.UInt32BitsToSingle(uint.Parse(v[1])),
                                                           BitConverter.UInt32BitsToSingle(uint.Parse(v[2])),
                                                           BitConverter.UInt32BitsToSingle(uint.Parse(v[3]))),
                                                   RegionHandle = ulong.Parse(v[4]) };
                Console.WriteLine(new BasicInventoryAccess().Generate(presence));
                continue;
            }
            var a = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Probe(a[0], a[1], a[2], a[3], a[4], a[5]));
        }
    }
}

partial class BasicInventoryAccess
{
    public string Generate(ScenePresence p)
    {
        var text = GenerateLandmark(p, out var prefix, out var suffix);
        return Reference.Hex(text) + ":" + Reference.Hex(prefix) + ":" + Reference.Hex(suffix);
    }
}
