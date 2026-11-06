using System;
using System.IO;
using System.Text;
using OpenMetaverse;
using OpenSim.Framework;
namespace log4net
{
    class Marker
    {
    }
}
namespace OpenSim.Framework
{
    static class Util
    {
        public static int UnixTimeSinceEpoch() => 111;
    }
    class AssetBase
    {
        public UUID FullID;
        public sbyte Type;
        public byte[] Data;
    }
}
class Client
{
    public UUID AgentId => Probe.Id(1);
    public void SendAgentAlertMessage(string s, bool modal) => Probe.Step(12, "AGENT:" + Probe.Hex(s) + ":" +
                                                                                  (modal ? 1 : 0));
    public void SendAlertMessage(string s)
    {
        Probe.Step(11, "ALERT:" + Probe.Hex(s));
        if (Probe.Bit(1048576))
        {
            Probe.Item.Name = "Alert edit";
            Probe.Item.Type = 13;
            Probe.Item.AssetID = Probe.Id(6);
        }
    }
}
class ScenePresence
{
    public Client ControllingClient
    {
        get {
            Probe.Step(3, "CLIENT");
            return Probe.Bit(32768) ? null : new();
        }
    }
}
interface IInventoryAccessModule
{
    UUID CapsUpdateInventoryItemAsset(Client c, UUID item, byte[] data);
}
class Access : IInventoryAccessModule
{
    public UUID CapsUpdateInventoryItemAsset(Client c, UUID item, byte[] data)
    {
        Probe.Step(4, "MODULE:" + (c == null ? "NULL" : c.AgentId.ToString()) + ":" + item + ":" +
                          Probe.Bytes(data));
        if (Probe.Bit(131072))
            Probe.Item.AssetID = Probe.Id(5);
        return Probe.Bit(65536) ? UUID.Zero : Probe.Id(9);
    }
}
class Group
{
    public bool IsDeleted = Probe.Bit(32);
}
class PartInventory
{
    public TaskInventoryItem GetInventoryItem(UUID key)
    {
        Probe.Step(8, "ITEM:" + key);
        return Probe.Bit(64) ? null : Probe.Item;
    }
    public void UpdateInventoryItem(TaskInventoryItem item)
    {
        Probe.Step(15, "UPDATE:" + Probe.Dump(item));
        if (Probe.Bit(16384))
        {
            Probe.Asset.FullID = Probe.Id(11);
            item.AssetID = Probe.Id(12);
        }
    }
}
class SceneObjectPart
{
    public Group group = new();
    public Group ParentGroup
    {
        get {
            Probe.Step(6, "GROUP");
            return Probe.Bit(16) ? null : group;
        }
    }
    public PartInventory Inventory
    {
        get {
            Probe.Step(7, "INVENTORY");
            if (++Probe.InventoryGets == 2 && Probe.Failure == 19)
                throw new Exception();
            return new();
        }
    }
}
class Permissions
{
    public bool CanEditObjectInventory(UUID obj, UUID avatar)
    {
        Probe.Step(9, "EDIT:" + obj + ":" + avatar);
        if (Probe.Bit(262144))
        {
            Probe.Item.InvType = 26;
            Probe.Item.CurrentPermissions = 0;
        }
        return !Probe.Bit(128);
    }
    public bool CanEditNotecard(UUID item, UUID obj, UUID avatar)
    {
        Probe.Step(10, "NOTE:" + item + ":" + obj + ":" + avatar);
        if (Probe.Bit(524288))
        {
            Probe.Item.Name = "Note edit";
            Probe.Item.Type = 255;
            Probe.Item.InvType = 10;
            Probe.Item.OwnerID = Probe.Id(9);
            Probe.Item.CurrentPermissions = 0;
            Probe.Part.group.IsDeleted = true;
        }
        return !Probe.Bit(256);
    }
}
class InventoryService
{
    public int GetAssetPermissions(UUID a, UUID key)
    {
        Probe.Step(16, "PERM:" + a + ":" + key);
        return 0;
    }
}
class AssetService
{
    public string Store(AssetBase a)
    {
        Probe.Step(14, "STORE:" + Probe.Dump(a) + ":" + Probe.Item.AssetID);
        if (Probe.Bit(8192))
        {
            a.FullID = Probe.Id(10);
            Probe.Item.AssetID = Probe.Id(8);
        }
        return Probe.Bit(4096) ? UUID.ZeroString : "stored";
    }
}
class Logger
{
    public void ErrorFormat(string s,
                            params object[] a) => Probe.Step(17, "ERROR:" + Probe.Hex(string.Format(s, a)));
}
class Writer : TextWriter
{
    public override Encoding Encoding => Encoding.UTF8;
    public override void WriteLine(string s) => Probe.Step(18, "CONSOLE:" + Probe.Hex(s));
}
partial class Scene
{
    Logger m_log = new();
    public Permissions Permissions = new();
    public InventoryService InventoryService = new();
    public AssetService AssetService = new();
    bool TryGetScenePresence(UUID key, out ScenePresence p)
    {
        Probe.Step(1, "PRESENCE:" + key);
        p = Probe.Bit(2) ? null : new();
        return !Probe.Bit(1);
    }
    T RequestModuleInterface<T>()
        where T : class
    {
        Probe.Step(2, "LOOKUP");
        return Probe.Bit(4) ? null : new Access() as T;
    }
    SceneObjectPart GetSceneObjectPart(UUID key)
    {
        Probe.Step(5, "PART:" + key);
        return Probe.Bit(8) ? null : Probe.Part;
    }
    AssetBase CreateAsset(string name, string description, sbyte type, byte[] data, UUID creator)
    {
        Probe.Step(13, "CREATE:" + Probe.Hex(name) + ":" + Probe.Hex(description) + ":" + type + ":" +
                           Probe.Bytes(data) + ":" + creator);
        if (Probe.Bit(131072))
        {
            Probe.Item.Name = "Mutation";
            Probe.Item.Type = 3;
            Probe.Item.AssetID = Probe.Id(5);
        }
        if (Probe.Bit(2048))
            return Probe.Asset = null;
        return Probe.Asset = new() { FullID = Probe.Id(7), Type = type, Data = data ?? new byte[1] };
    }
}
class Probe
{
    public static int Mode, Failure, InventoryGets;
    public static TaskInventoryItem Item;
    public static SceneObjectPart Part;
    public static AssetBase Asset;
    static string Trace;
    public static bool Bit(int n) => (Mode & n) != 0;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static string Hex(string s) =>
        s == null ? "NULL" : Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Bytes(byte[] d) => d == null ? "NULL" : Convert.ToHexString(d).ToLowerInvariant();
    public static void Step(int n, string s)
    {
        Trace += "|" + s;
        if (n == Failure)
            throw new Exception();
    }
    public static string Dump(TaskInventoryItem i) =>
        i == null
            ? "NULL"
            : $"{i.ItemID}:{i.OwnerID}:{i.AssetID}:{Hex(i.Name)}:{Hex(i.Description)}:{i.Type}:{i.InvType}:{i.CurrentPermissions}";
    public static string Dump(AssetBase a) => a == null ? "NULL" : $"{a.FullID}:{a.Type}:{Bytes(a.Data)}";
    static string Run(int agent, int mode, int inv, int type, int fail)
    {
        Mode = mode;
        Failure = fail;
        Trace = "";
        InventoryGets = 0;
        Asset = null;
        Item = new() { ItemID = Id(2),
                       OwnerID = Id(9),
                       AssetID = Id(4),
                       Name = Bit(2097152)   ? null
                              : Bit(4194304) ? "脚本 😃"
                                             : "Task item",
                       Description = Bit(2097152) ? null : "Description",
                       InvType = inv,
                       Type = type,
                       CurrentPermissions = Bit(512) ? 0u : 0xffffffff };
        Part = new();
        var old = Console.Out;
        Console.SetOut(new Writer());
        string status = "OK", result = "NONE";
        try
        {
            result = new Scene()
                         .CapsUpdateItemAsset(Id(1), Id(2), agent != 0 ? UUID.Zero : Id(3),
                                              Bit(1024) ? null : new byte[] { 65, 0, 255 })
                         .ToString();
        }
        catch
        {
            status = "THROW";
        }
        finally
        {
            Console.SetOut(old);
        }
        return status + ":" + result + ":" + Dump(Item) + ":" + Dump(Asset) + Trace;
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
