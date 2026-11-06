using System;
using System.Collections.Generic;
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
            Reference.Step(2, "TIME");
            return 111;
        }
    }
}
interface IClientAPI
{
    UUID AgentId { get; }
    string Name { get; }
    void SendBulkUpdateInventory(InventoryItemBase i);
}
class Client : IClientAPI
{
    public UUID AgentId => Reference.Id(1);
    public string Name => "Client";
    public void SendBulkUpdateInventory(InventoryItemBase i)
    {
        Reference.Step(5, "BULK:" + Reference.Dump(i));
        if (Reference.Kind == 1 && Reference.Bit(2048))
            Reference.List.Add(new());
    }
}
class Inventory
{
    public InventoryItemBase GetItem(UUID a, UUID k)
    {
        Reference.Step(1, "GET:" + k);
        if (Reference.Kind == 1)
        {
            if (Reference.Bit(1024))
                Reference.List.Add(new());
            if (Reference.Bit(512))
                return null;
        }
        return Reference.Bit(1) ? null : Reference.Current;
    }
    public bool UpdateItem(InventoryItemBase i)
    {
        Reference.Step(3, "UPDATE:" + Reference.Dump(i));
        if (Reference.Bit(262144))
        {
            i.InvType = 10;
            i.Flags ^= 0x80000000;
            i.AssetID = Reference.Id(9);
        }
        return !Reference.Bit(64);
    }
    public bool MoveItems(UUID a, List<InventoryItemBase> items)
    {
        Reference.Step(7, "MOVE:" + items.Count);
        if (Reference.Bit(128))
            items.Clear();
        if (Reference.Bit(256))
            items.Add(new(Reference.Id(8)));
        return !Reference.Bit(64);
    }
    public bool DeleteItems(UUID a, List<UUID> items)
    {
        Reference.Step(10, "DELETE:" + string.Join(",", items));
        return !Reference.Bit(64);
    }
    public bool DeleteFolders(UUID a, List<UUID> items)
    {
        Reference.Step(11, "FOLDERS:" + string.Join(",", items));
        return !Reference.Bit(64);
    }
}
class Transactions
{
    public void HandleItemUpdateFromTransaction(IClientAPI c, UUID transaction, InventoryItemBase i) =>
        Reference.Step(4, "TRANSACTION:" + Reference.Dump(i));
}
class Logger
{
    public void ErrorFormat(string s, params object[] a) => Reference.Step(6, "ERROR");
    public void DebugFormat(string s, params object[] a) => Reference.Step(8, "DEBUG:" + a[0]);
    public void Warn(string s) => Reference.Step(9, "WARN");
}
partial class Scene
{
    public Inventory InventoryService = new();
    public Transactions AgentTransactionsModule = Reference.Bit(32) ? new() : null;
    Logger m_log = new();
    public void TestRemoveItems(IClientAPI c, List<UUID> items) => RemoveInventoryItem(c, items);
    public void TestRemoveFolders(IClientAPI c, List<UUID> items) => RemoveInventoryFolder(c, items);
}
class Reference
{
    public static int Kind, Mode, Failure;
    public static InventoryItemBase Current;
    public static List<InventoryItemBase> List;
    static string Trace;
    public static bool Bit(int b) => (Mode & b) != 0;
    public static UUID Id(int n) => new("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static void Step(int n, string s)
    {
        Trace += "|" + s;
        if (Failure == n)
            throw new Exception();
    }
    static string Hex(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Dump(InventoryItemBase i) =>
        i == null
            ? "NULL"
            : $"{Hex(i.Name)}:{Hex(i.Description)}:{i.BasePermissions},{i.CurrentPermissions},{i.NextPermissions},{i.EveryOnePermissions},{i.GroupPermissions},{i.Flags},{i.CreationDate},{i.InvType},{i.SalePrice},{i.SaleType},{(i.GroupOwned?1:0)},{i.GroupID},{i.Owner},{i.Folder},{i.AssetID}";
    static string OldName(int t) => new[] {
        "Name",   "HG Native", "HG \u00adNative", "HG \0tail", "HG Native\u00ad", "ＨＧ old",
        "Plainé", "HG 😃"
    }[t & 7];
    static string OldDesc(int t) => new[] { "original @ http://grid", "old @ h\u00adttp://grid",
                                            "😀 @ httone @ htttwo",   "old @ ＨＴＴgrid",
                                            "old \u00ad@ httlink",    "description",
                                            "a\0@ httlink",           "old @ httend" }[t & 7];
    static string NewDesc(int t) => new[] { "edited", "", "edited @ embedded", " \t　", "a\0b", "新しい",
                                            "😃",     "@" }[t & 7];
    static string Probe(int kind, int mode, int text, int failure)
    {
        Kind = kind;
        Mode = mode;
        Failure = failure;
        Trace = "";
        Current = new() { ID = Id(2),
                          Owner = Id(Bit(2) ? 9 : 1),
                          CreatorId = Id(Bit(4096) ? 9 : 1).ToString(),
                          Name = OldName(text),
                          Description = OldDesc(text),
                          BasePermissions = Bit(8192) ? 0x96000u : 0x9e000u,
                          CurrentPermissions = Bit(16384) ? 0x8e000u : 0x9e000u,
                          NextPermissions = 0x82000,
                          EveryOnePermissions = Bit(1024) ? 0x12000u : 0x2000u,
                          GroupPermissions = 0x4000,
                          Flags = 0x1234fedc,
                          CreationDate = 222,
                          InvType = 10,
                          AssetType = Bit(8) ? 3 : 10,
                          SalePrice = 10,
                          SaleType = 1,
                          Folder = Id(3),
                          AssetID = Id(4) };
        var input = new InventoryItemBase { ID = Id(2),
                                            Owner = Id(1),
                                            Name = (text & 1) != 0 ? "HG Edited" : "Edited",
                                            Description = NewDesc(text),
                                            NextPermissions = Bit(4) ? 0u : 0x82000u,
                                            EveryOnePermissions = Bit(2048) ? 0x1e000u : 0x8000u,
                                            GroupPermissions = 0x2000,
                                            GroupID = Id(5),
                                            GroupOwned = true,
                                            CreationDate = Bit(256) ? 0 : -123,
                                            InvType = Bit(512) ? 18 : 10,
                                            Flags = 0xabcd1234,
                                            SalePrice = Bit(32768) ? 20 : 10,
                                            SaleType = Bit(32768) ? (byte)2 : (byte)1,
                                            Folder = Id(6) };
        if (Bit(65536))
            input = Current;
        if (Bit(131072))
            input = null;
        List = Bit(16) ? null : new() { Bit(4) ? null : new(Id(2), Id(1)), new(Id(7), Id(1)) };
        string result = "OK";
        try
        {
            var scene = new Scene();
            if (kind == 0)
                scene.UpdateInventoryItem(new Client(), Bit(16) ? Id(8) : UUID.Zero, Id(2), input);
            else if (kind == 1)
                scene.MoveInventoryItem(new Client(), List);
            else if (kind == 2)
                scene.TestRemoveItems(new Client(), new() { Id(2), Id(7) });
            else
                scene.TestRemoveFolders(new Client(), new() { Id(3), Id(6) });
        }
        catch
        {
            result = "THROW";
        }
        return result + ":" + Dump(Current) + ":" + Dump(input) + Trace;
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
