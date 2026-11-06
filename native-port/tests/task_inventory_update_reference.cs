using System;
using System.Collections.Generic;
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
}
interface IClientAPI
{
    UUID AgentId { get; }
    string Name { get; }
    bool IsGroupMember(UUID g);
    ulong GetGroupPowers(UUID g);
}
class Client : IClientAPI
{
    public UUID AgentId => Reference.Id(1);
    public string Name => "Client";
    public bool IsGroupMember(UUID g)
    {
        Reference.Step(18, "MEMBER");
        return Reference.Bit(65536);
    }
    public ulong GetGroupPowers(UUID g)
    {
        Reference.Step(19, "POWERS");
        return Reference.Bit(131072) ? (ulong)GroupPowers.ObjectManipulate : 0;
    }
}
class InventoryItemBase
{
    public UUID ID = Reference.Id(2);
    public string Name = "Drop";
    public uint CurrentPermissions = Reference.Bit(1024) ? 0u : 0x8000u;
}
class Inventory
{
    public InventoryItemBase GetItem(UUID a, UUID k)
    {
        Reference.Step(5, "USER");
        return Reference.Bit(8) ? null : new();
    }
}
class LibraryRoot
{
    public InventoryItemBase FindItem(UUID k)
    {
        Reference.Step(6, "LIBRARY");
        return Reference.Bit(64) ? null : new();
    }
}
class Library
{
    public LibraryRoot LibraryRootFolder = Reference.Bit(32) ? null : new();
}
class Permissions
{
    public bool CanDropInObjectInv(InventoryItemBase i, IClientAPI c, SceneObjectPart p)
    {
        Reference.Step(7, "DROP");
        return !Reference.Bit(128);
    }
    public bool CanEditObject(Group g, IClientAPI c)
    {
        Reference.Step(8, "EDITOBJECT");
        return !Reference.Bit(256);
    }
    public bool BypassPermissions()
    {
        Reference.Step(13, "BYPASS");
        return Reference.Bit(512);
    }
    public bool CanEditObjectInventory(UUID p, UUID a)
    {
        Reference.Step(15, "EDITINV");
        return !Reference.Bit(2048);
    }
    public bool IsGod(UUID a)
    {
        Reference.Step(17, "GOD");
        return Reference.Bit(4096);
    }
}
class Group
{
    public void AddInventoryItem(UUID a, uint local, InventoryItemBase item, UUID copy, bool mod)
    {
        Reference.Step(9, "ADD:" + (copy != UUID.Zero ? 1 : 0) + ":" + (mod ? 1 : 0));
        if (Reference.Bit(33554432))
            item.CurrentPermissions = 0;
    }
}
class PartInventory
{
    public TaskInventoryItem GetInventoryItem(UUID key)
    {
        Reference.Step(4, "GETTASK");
        return Reference.Bit(4) ? null : Reference.Current;
    }
    public bool UpdateInventoryItem(TaskInventoryItem i)
    {
        Reference.Step(20, "UPDATE:" + Reference.Dump(i));
        return !Reference.Bit(262144);
    }
}
class SceneObjectPart
{
    public UUID UUID = Reference.Id(3), OwnerID = Reference.Bit(8192) ? Reference.Id(9) : Reference.Id(1);
    public Group ParentGroup = new();
    public PartInventory Inventory = new();
    public void SendPropertiesToClient(IClientAPI c) => Reference.Step(12, "PROPERTIES");
}
interface IInventoryAccessModule
{
    void FetchRemoteHGItemAssets(UUID a, InventoryItemBase i);
}
class Access : IInventoryAccessModule
{
    public void FetchRemoteHGItemAssets(UUID a, InventoryItemBase i) => Reference.Step(11, "REMOTE");
}
class Transactions
{
    public void HandleTaskItemUpdateFromTransaction(IClientAPI c, SceneObjectPart p, UUID t,
                                                    TaskInventoryItem i)
    {
        Reference.Step(16, "TRANSACTION");
        if (Reference.Bit(4194304))
        {
            i.BasePermissions = 0x1234;
            i.Flags = 0x555;
        }
    }
}
class Logger
{
    public void Error(string s) => Reference.Step(1, "ERROR");
    public void Warn(string s) => Reference.Step(2, "WARN");
    public void Info(string s) => Reference.Step(10, "INFO");
}
partial class Scene
{
    public Inventory InventoryService = new();
    public Library LibraryService = Reference.Bit(16) ? null : new();
    public Permissions Permissions = new();
    public Transactions AgentTransactionsModule = Reference.Bit(1048576) ? new() : null;
    Logger m_log = new();
    SceneObjectPart GetSceneObjectPart(uint local)
    {
        Reference.Step(3, "PART:" + local);
        return Reference.Bit(2) ? null : new();
    }
    T RequestModuleInterface<T>()
        where T : class => Reference.Bit(2097152) ? new Access() as T : null;
    void RemoveInventoryItem(IClientAPI c, List<UUID> keys) => Reference.Step(14, "REMOVE:" + keys[0]);
}
class Reference
{
    public static int Mode, Failure;
    public static TaskInventoryItem Current;
    static string Trace;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static bool Bit(int value) => (Mode & value) != 0;
    public static void Step(int p, string s)
    {
        Trace += "|" + s;
        if (p == Failure)
            throw new Exception();
    }
    public static string Dump(TaskInventoryItem i) =>
        i == null
            ? "NULL"
            : $"{i.BasePermissions},{i.CurrentPermissions},{i.EveryonePermissions},{i.GroupPermissions},{i.NextPermissions},{i.Flags},{i.AssetID},{i.OwnerID},{i.GroupID}";
    static string Probe(int mode, int mask, int failure)
    {
        Mode = mode;
        Failure = failure;
        Trace = "";
        uint[] masks = { 0, 0xffffffff, 0x9e000, 0x1, 0x12345678, 0x80000, 0x7fffffff, 0x80000000 };
        uint b = masks[mask & 7];
        Current = new() { ItemID = Id(2),
                          BasePermissions = b,
                          CurrentPermissions = b ^ 0x2000u,
                          EveryonePermissions = b ^ 0x4000u,
                          GroupPermissions = b ^ 0x8000u,
                          NextPermissions = b ^ 0x10000u,
                          Flags = 0x12345678,
                          AssetID = Id(4),
                          OwnerID = Id(1) };
        var incoming = new TaskInventoryItem { ItemID = Bit(1) ? UUID.Zero : Id(2),
                                               BasePermissions = b ^ 0x40000u,
                                               CurrentPermissions = b ^ 0x10000u,
                                               EveryonePermissions = b ^ 0x20000u,
                                               GroupPermissions = b ^ 0x80000u,
                                               NextPermissions = b ^ 0x100000u,
                                               Flags = 0xffffffff,
                                               AssetID = Id(5),
                                               OwnerID = Bit(16384) ? UUID.Zero : Id(1),
                                               GroupID = Bit(32768) ? Id(6) : UUID.Zero };
        if (Bit(8388608))
            incoming = Current;
        if (Bit(16777216))
            incoming = null;
        string result = "OK";
        try
        {
            new Scene().UpdateTaskInventory(new Client(), Bit(524288) ? Id(7) : UUID.Zero, incoming, 123);
        }
        catch
        {
            result = "THROW";
        }
        return result + ":" + Dump(Current) + ":" + Dump(incoming) + Trace;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = Array.ConvertAll(line.Split(' '), int.Parse);
            Console.WriteLine(Probe(a[0], a[1], a[2]));
        }
    }
}
