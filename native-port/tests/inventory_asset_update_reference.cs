using System;
using System.IO;
using System.Text;
using OpenMetaverse;
using OpenSim.Framework;
namespace OpenSim.Framework
{
    class AssetBase
    {
        public UUID FullID;
        public sbyte Type;
        public byte[] Data;
    }
}
interface IClientAPI
{
    UUID AgentId { get; }
    void SendAgentAlertMessage(string message, bool modal);
    void SendAlertMessage(string message);
}
class Client : IClientAPI
{
    public UUID AgentId => Probe.Id(1);
    public void SendAgentAlertMessage(string message, bool modal) =>
        Probe.Step(7, "AGENT:" + AgentId + ":" + Probe.Hex(message) + ":" + (modal ? 1 : 0));
    public void SendAlertMessage(string message)
    {
        Probe.Step(8, "ALERT:" + AgentId + ":" + Probe.Hex(message));
        if (Probe.Bit(262144))
            Probe.Asset.FullID = Probe.Id(10);
    }
}
class Inventory
{
    public InventoryItemBase GetItem(UUID agent, UUID key)
    {
        Probe.Step(1, "GET:" + agent + ":" + key);
        return Probe.Bit(1) ? null : Probe.Item;
    }
    public bool UpdateItem(InventoryItemBase item)
    {
        Probe.Step(6, "UPDATE:" + Probe.Dump(item));
        if (Probe.Bit(131072))
        {
            Probe.Asset.FullID = Probe.Id(8);
            item.AssetID = Probe.Id(6);
        }
        return !Probe.Bit(2048);
    }
    public int GetAssetPermissions(UUID a, UUID b)
    {
        Probe.Step(11, "PERM:" + a + ":" + b);
        return 0;
    }
}
class Permissions
{
    public bool CanEditNotecard(UUID key, UUID obj, UUID agent)
    {
        Probe.Step(2, "NOTE:" + key + ":" + obj + ":" + agent);
        Probe.PermissionMutation();
        return !Probe.Bit(4);
    }
    public bool CanEditScript(UUID key, UUID obj, UUID agent)
    {
        Probe.Step(3, "SCRIPT:" + key + ":" + obj + ":" + agent);
        Probe.PermissionMutation();
        return !Probe.Bit(8);
    }
}
class Assets
{
    public string Store(AssetBase asset)
    {
        int stage = ++Probe.Stores;
        Probe.Step(5, "STORE:" + stage + ":" + Probe.Dump(asset) + ":" + Probe.Item.AssetID);
        if (stage == 2 && Probe.Failure == 13)
            throw new Exception();
        if (Probe.Bit(65536))
        {
            asset.FullID = Probe.Id(9);
            asset.Type = -1;
        }
        if (Probe.Bit(2097152) && asset.Data != null && asset.Data.Length > 0)
            asset.Data[0] = 99;
        int flags = stage == 1 ? 32 : 256;
        if (Probe.Bit(flags))
            return "";
        if (Probe.Bit(flags * 2))
            return UUID.ZeroString;
        if (Probe.Bit(flags * 4))
            return null;
        return Probe.Bit(4194304) ? UUID.ZeroString + " " : Probe.Bit(8388608) ? " " : "not-a-uuid";
    }
}
class Scene
{
    public Inventory InventoryService = new();
    public Permissions Permissions = new();
    public Assets AssetService = new();
}
class Logger
{
    public void Warn(string s) => Probe.Step(9, "WARN:" + Probe.Hex(s));
    public void Error(string s) => Probe.Step(10, "ERROR:" + Probe.Hex(s));
    public void ErrorFormat(string s, params object[] a) => Error(string.Format(s, a));
}
class ConsoleWriter : TextWriter
{
    public override Encoding Encoding => Encoding.UTF8;
    public override void WriteLine(string s) => Probe.Step(12, "CONSOLE:" + Probe.Hex(s));
}
partial class InventoryAccessModule
{
    Scene m_Scene = new();
    Logger m_log = new();
    AssetBase CreateAsset(string name, string description, sbyte type, byte[] data, string creator)
    {
        Probe.Step(4, "CREATE:" + Probe.Hex(name) + ":" + Probe.Hex(description) + ":" + type + ":" +
                          Probe.Bytes(data) + ":" + creator);
        if (Probe.Bit(524288))
        {
            Probe.Item.Name = "Mutation";
            Probe.Item.AssetType = 3;
            Probe.Item.Owner = Probe.Id(9);
            Probe.Item.AssetID = Probe.Id(5);
        }
        if (Probe.Bit(4096))
        {
            Probe.Asset = null;
            return null;
        }
        Probe.Asset = new() { FullID = Probe.Id(7), Type = type, Data = data ?? new byte[1] };
        return Probe.Asset;
    }
}
class Probe
{
    public static int Mode, Failure, Stores;
    public static InventoryItemBase Item;
    public static AssetBase Asset;
    static string Trace;
    public static bool Bit(int n) => (Mode & n) != 0;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static string Hex(string s) =>
        s == null ? "NULL" : Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Bytes(byte[] b) => b == null ? "NULL" : Convert.ToHexString(b).ToLowerInvariant();
    public static void Step(int n, string value)
    {
        Trace += "|" + value;
        if (n == Failure)
            throw new Exception();
    }
    public static string Dump(InventoryItemBase i) =>
        i == null
            ? "NULL"
            : $"{i.ID}:{i.Owner}:{i.AssetID}:{Hex(i.Name)}:{Hex(i.Description)}:{i.InvType}:{i.AssetType}:{i.CurrentPermissions}";
    public static string Dump(AssetBase a) => a == null ? "NULL" : $"{a.FullID}:{a.Type}:{Bytes(a.Data)}";
    public static void PermissionMutation()
    {
        if (Bit(1048576))
        {
            Item.Name = "Changed";
            Item.Description = "";
            Item.AssetType = 255;
            Item.InvType = 26;
            Item.CurrentPermissions = 0;
            Item.Owner = Id(9);
        }
    }
    static string Run(int kind, int mode, int inv, int type, int failure)
    {
        Mode = mode;
        Failure = failure;
        Stores = 0;
        Trace = "";
        Item = new() { ID = Id(2),
                       Owner = Id(Bit(2) ? 9 : 1),
                       AssetID = Id(4),
                       Name = Bit(32768) ? "脚本 😃" : "Stored item",
                       Description = "Description",
                       InvType = inv,
                       AssetType = type,
                       CurrentPermissions = Bit(16) ? 0u : 0xffffffff };
        byte[] data = Bit(16384) ? null : new byte[] { 65, 0, 255 };
        Asset = Bit(8192) ? null : new() { FullID = Id(7), Type = 99, Data = data };
        var writer = Console.Out;
        Console.SetOut(new ConsoleWriter());
        string status = "OK", result = "NONE";
        try
        {
            var module = new InventoryAccessModule();
            if (kind == 0)
                result = module.CapsUpdateInventoryItemAsset(new Client(), Id(2), data).ToString();
            else
                result =
                    module.UpdateInventoryItemAsset(Id(1), Bit(1) ? null : Item, Asset) ? "TRUE" : "FALSE";
        }
        catch
        {
            status = "THROW";
        }
        finally
        {
            Console.SetOut(writer);
        }
        return status + ":" + result + ":" + Dump(Item) + ":" + Dump(Asset) + ":" + Bytes(data) + Trace;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Run(a[0], a[1], a[2], a[3], a[4]));
        }
    }
}
