using System;
using System.Globalization;
using System.Text;
using OpenMetaverse;
using OpenSim.Framework;
namespace OpenSim.Framework
{
    static class Util
    {
        public static int UnixTimeSinceEpoch()
        {
            Reference.Step(8, "TIME");
            if (Reference.Bit(1048576))
            {
                Reference.Source.Flags = 0;
                Reference.Source.AssetID = Reference.Id(9);
            }
            return 111;
        }
    }
}
interface IClientAPI
{
    UUID AgentId { get; }
    string Name { get; }
    void SendInventoryItemCreateUpdate(InventoryItemBase i, uint callback);
}
class Client : IClientAPI
{
    public UUID AgentId => Reference.Id(1);
    public string Name => "Client";
    public void SendInventoryItemCreateUpdate(InventoryItemBase i, uint callback) =>
        Reference.Step(5, "REPLY:" + callback + ":" + Reference.Dump(i, true));
}
class LibraryRoot
{
    public UUID Owner = Reference.Id(7);
    public InventoryItemBase FindItem(UUID id)
    {
        Reference.Step(2, "LIBRARY:" + id);
        return Reference.Bit(4) ? null : Reference.Source;
    }
}
class Library
{
    public LibraryRoot LibraryRootFolder = Reference.Bit(2) ? null : new();
}
class Inventory
{
    public InventoryItemBase GetItem(UUID a, UUID key)
    {
        Reference.Step(3, "USER:" + a + ":" + key);
        return Reference.Bit(8) ? null : Reference.Source;
    }
}
class ScenePresence
{
}
class Permissions
{
    public bool CanCreateUserInventory(int type, UUID agent)
    {
        Reference.Step(12, "CREATEPERM:" + type + ":" + agent);
        return !Reference.Bit(1);
    }
    public bool BypassPermissions()
    {
        Reference.Step(9, "BYPASS");
        if (Reference.Bit(524288))
        {
            Reference.Source.Name = "Mutated";
            Reference.Source.NextPermissions = 0x4000;
            Reference.Source.Flags = 7;
        }
        return Reference.Bit(512);
    }
    public bool CanCopyUserInventory(UUID a, UUID item)
    {
        Reference.Step(10, "PERMISSION:" + a + ":" + item);
        return !Reference.Bit(1024);
    }
}
class Dialog
{
    public void SendAlertToUser(IClientAPI c, string m) => Reference.Step(6, "ALERT:" + m);
}
class Logger
{
    public void DebugFormat(string s, params object[] args) => Reference.Step(1, "DEBUG");
    public void Error(string s) => Reference.Step(11, "ERROR");
    public void WarnFormat(string s, params object[] args) => Reference.Step(7, "WARN");
}
partial class Scene
{
    public Permissions Permissions => m_permissions;
    public bool TryGetScenePresence(UUID agent, out ScenePresence p)
    {
        Reference.Step(13, "PRESENCE:" + agent);
        p = null;
        return !Reference.Bit(2);
    }
    public void Link(IClientAPI client, UUID tx, UUID folder, uint cb, string desc, string name, sbyte it,
                     sbyte at, UUID target) => HandleLinkInventoryItem(client, tx, folder, cb, desc, name, it,
                                                                       at, target);
    public Library LibraryService = Reference.Bit(1) ? null : new();
    public Inventory InventoryService = new();
    Permissions m_permissions = new();
    Dialog m_dialogModule = new();
    Logger m_log = new();
    bool AddInventoryItem(InventoryItemBase i)
    {
        Reference.Step(4, "ADD:" + Reference.Dump(i, true));
        if (Reference.Bit(262144))
        {
            i.Folder = Reference.Id(9);
            i.Flags = 1;
        }
        return !Reference.Bit(2048);
    }
}
class Reference
{
    public static int Mode, Failure;
    public static InventoryItemBase Source;
    static string Trace;
    public static bool Bit(int b) => (Mode & b) != 0;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static void Step(int n, string s)
    {
        Trace += "|" + s;
        if (n == Failure)
            throw new Exception();
    }
    static string Hex(string s) => s == null
                                       ? "NULL"
                                       : Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Dump(InventoryItemBase i, bool fresh = false) =>
        i == null
            ? "NULL"
            : $"{(fresh?(i.ID!=UUID.Zero?"NEW":"ZERO"):i.ID.ToString())}:{i.Owner}:{i.AssetID}:{Hex(i.CreatorId)}:{Hex(i.CreatorData)}:{Hex(i.Name)}:{Hex(i.Description)}:{i.BasePermissions},{i.CurrentPermissions},{i.EveryOnePermissions},{i.NextPermissions},{i.GroupPermissions},{i.Flags},{i.AssetType},{i.InvType},{i.CreationDate},{i.GroupID},{(i.GroupOwned?1:0)},{i.SalePrice},{i.SaleType},{i.Folder}";
    static string Probe(int kind, int mode, int value, int failure)
    {
        Mode = mode;
        Failure = failure;
        Trace = "";
        Source = new() { ID = Id(2),
                         Owner = Id(Bit(4194304) ? 9 : 1),
                         AssetID = Id(4),
                         CreatorId = Id(3).ToString(),
                         CreatorData = Bit(65536) ? null : "https://creator;Creator",
                         Name = Bit(32768) ? "脚本 😃" : "Stored script",
                         Description = Bit(32768) ? "説明 é" : "Description",
                         BasePermissions = 0xf00fffff,
                         CurrentPermissions =
                             (Bit(16) ? 0xffff7fffu : 0xffffffffu) & (Bit(32) ? 0xffffdfffu : 0xffffffffu),
                         EveryOnePermissions = 0x8a000,
                         NextPermissions = unchecked((uint)value),
                         GroupPermissions = 0x84000,
                         Flags = 0x12345678,
                         AssetType = value,
                         InvType = Bit(4096) ? 96 : 10,
                         CreationDate = 222,
                         GroupID = Id(5),
                         GroupOwned = true,
                         SalePrice = 99,
                         SaleType = 2,
                         Folder = Id(6) };
        string status = "OK";
        try
        {
            var scene = new Scene();
            var client = new Client();
            if (kind == 0)
                scene.CopyInventoryItem(client, unchecked((uint)value),
                                        Bit(256)   ? UUID.Zero
                                        : Bit(128) ? Id(7)
                                        : Bit(64)  ? Id(9)
                                                   : Id(1),
                                        Id(2), Id(6),
                                        Bit(8192)    ? null
                                        : Bit(16384) ? ""
                                        : Bit(32768) ? "新しい 😃"
                                                     : "Copied script");
            else if (kind == 1)
                scene.CreateNewInventoryItem(
                    client, Source.CreatorId, Source.CreatorData, Source.Folder, Source.Name,
                    Source.Description, Source.Flags, unchecked((uint)value), Source.AssetID,
                    unchecked((sbyte)Source.AssetType), unchecked((sbyte)Source.InvType),
                    Source.BasePermissions, Source.CurrentPermissions, Source.EveryOnePermissions,
                    Source.NextPermissions, Source.GroupPermissions, Source.CreationDate);
            else if (kind == 2)
                scene.CreateNewInventoryItem(
                    client, Source.CreatorId, Source.CreatorData, Source.Folder, Source.Name,
                    Source.Description, Source.Flags, unchecked((uint)value), Source.AssetID,
                    unchecked((sbyte)Source.AssetType), unchecked((sbyte)Source.InvType),
                    Source.BasePermissions, Source.CurrentPermissions, Source.EveryOnePermissions,
                    Source.NextPermissions, Source.GroupPermissions, Source.CreationDate, true);
            else if (kind == 3)
                AnimationSet.setCreateItemPermitions(Source);
            else if (kind == 4 || kind == 5)
                AnimationSet.enforceItemPermitions(Source, kind == 4);
            else if (kind == 7)
                scene.Link(client, Id(8), Id(6), unchecked((uint)value),
                           Bit(65536) ? null : Source.Description,
                           Bit(8192)    ? null
                           : Bit(16384) ? ""
                                        : Source.Name,
                           unchecked((sbyte)Source.InvType), unchecked((sbyte)value), Id(2));
            else
            {
                AnimationSet.setCreateItemPermitions(null);
                AnimationSet.enforceItemPermitions(null, true);
            }
        }
        catch
        {
            status = "THROW";
        }
        return status + ":" + Dump(Source) + Trace;
    }
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Probe(a[0], a[1], a[2], a[3]));
        }
    }
}
