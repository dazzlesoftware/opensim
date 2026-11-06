using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading;
using OpenMetaverse;
using OpenMetaverse.Packets;
class Trace
{
    public static List<string> Values = new();
    public static void Add(string s) => Values.Add(s);
}
class ControlledThread
{
    public static void Sleep(int ms) => Trace.Add("S" + ms);
}
class Util
{
    public static byte[] StringToBytes256(ReadOnlySpan<char> value) => Utils.StringToBytes(value, 255);
    public static void FireAndForget(Action<object> action)
    {
    }
}
enum ThrottleOutPacketType
{
    Asset
}
class PacketPool
{
    public static PacketPool Instance = new();
    public Packet GetPacket(PacketType type) => Packet.BuildPacket(type);
}
class InventoryFolderBase
{
    public UUID ID, ParentID, Owner;
    public string Name;
    public short Type;
    public ushort Version;
    public InventoryFolderBase()
    {
    }
    public InventoryFolderBase(UUID id)
    {
        ID = id;
    }
}
class InventoryItemBase
{
    public UUID ID, AssetID, CreatorIdAsUuid, Folder, Owner, GroupID;
    public uint BasePermissions, CurrentPermissions, NextPermissions, EveryOnePermissions, GroupPermissions,
        Flags;
    public string Name = "", Description = "";
    public int InvType, AssetType, CreationDate, SalePrice;
    public byte SaleType;
    public bool GroupOwned;
}
class InventoryCollection
{
    public UUID OwnerID, FolderID;
    public int Version;
    public List<InventoryItemBase> Items = new();
    public List<InventoryFolderBase> Folders = new();
}
class InventoryServiceStub
{
    public int Mode;
    public InventoryItemBase GetItem(UUID owner, UUID key)
    {
        Trace.Add("I" + owner + "," + key);
        if ((Mode & 8) != 0)
            throw new Exception();
        int k = int.Parse(key.ToString().Substring(24));
        if (k == 999 || ((Mode & 64) != 0 && k >= 300 && k < 500 && k % 2 != 0))
            return null;
        return LegacyReference.Item((Mode & 16) != 0 && k == 500 ? 777 : k, 500,
                                    k == 501   ? 24
                                    : k == 502 ? 25
                                               : 6);
    }
    public InventoryCollection GetFolderContent(UUID agent, UUID folder)
    {
        Trace.Add("C" + agent + "," + folder);
        if ((Mode & 256) != 0)
            throw new Exception();
        return LegacyReference.Contents(folder, Mode);
    }
    public InventoryFolderBase GetFolder(UUID agent, UUID folder)
    {
        Trace.Add("F" + agent + "," + folder);
        if ((Mode & 512) != 0)
            throw new Exception();
        return (Mode & 4) != 0 ? null : new() { ID = folder, Owner = agent, Version = 9 };
    }
}
class LibraryStub
{
    public LibraryRoot LibraryRootFolder;
}
class LibraryRoot
{
    public int Calls, Mode;
    public LibraryNode FindFolder(UUID key)
    {
        Trace.Add("L" + key);
        Calls++;
        if ((Mode & 1024) != 0 || ((Mode & 2048) != 0 && Calls > 1))
            throw new Exception();
        if (int.Parse(key.ToString().Substring(24)) != 2 ||
            !((Mode & 1) != 0 || ((Mode & 2) != 0 && Calls > 1)))
            return null;
        return new() { Value = LegacyReference.Contents(key, Mode) };
    }
}
class LibraryNode
{
    public InventoryCollection Value;
    public UUID Owner => Value.OwnerID;
    public int Version => Value.Version;
    public List<InventoryItemBase> RequestListOfItems() => Value.Items;
    public List<InventoryFolderBase> RequestListOfFolders() => Value.Folders;
}
class DescendentsRequestData
{
    public Client RemoteClient;
    public UUID FolderID;
    public bool FetchFolders, FetchItems;
}
class FetchHolder
{
    public Client Client;
    public UUID[] Items, Owners;
}
class LogStub
{
    public void ErrorFormat(string text, params object[] args) => Trace.Add("E");
}
partial class Client
{
    public UUID m_agentId = LegacyReference.Id(42);
    public UUID AgentId => m_agentId;
    public bool Active = true;
    public bool IsActive
    {
        get {
            Trace.Add(Active ? "A1" : "A0");
            return Active;
        }
    }
    void OutPacket(Packet packet, ThrottleOutPacketType throttle)
    {
        _ = packet.Length;
        OutPacket(packet, throttle, true);
    }
    void OutPacket(Packet packet, ThrottleOutPacketType throttle, bool split)
    {
        Trace.Add((split ? "P1:" : "P0:") + Convert.ToHexString(packet.ToBytes()).ToLowerInvariant());
    }
}
partial class LegacyReference
{
    public InventoryServiceStub InventoryService;
    public LibraryStub LibraryService;
    LogStub m_log = new();
    object m_descendentsRequestLock = new();
    bool m_descendentsRequestProcessing;
    ConcurrentQueue<DescendentsRequestData> m_descendentsRequestQueue = new();
    public static UUID Id(int n) => new UUID("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    public static InventoryItemBase Item(int key, int asset = 500, int type = 6, int owner = 200) => new() {
        ID = Id(key),     Owner = Id(owner), Folder = Id(2),        AssetID = Id(asset),
        AssetType = type, InvType = 6,       Flags = uint.MaxValue, Name = "item" + key
    };
    public static InventoryCollection Contents(UUID folder, int mode)
    {
        var c = new InventoryCollection { OwnerID = Id(250), FolderID = folder, Version = 7 };
        if ((mode & 128) != 0)
            return c;
        c.Folders.Add(new() { ID = Id(900), ParentID = folder, Name = "child", Type = -1 });
        int[] targets = { 500, 500, 999, 999, 501, 502 };
        for (int n = 0; n < 6; n++)
            c.Items.Add(Item(201 + n, targets[n], 24, 200 + n));
        return c;
    }
    static void Main()
    {
        string command;
        while ((command = Console.ReadLine()) != null)
        {
            Trace.Values = new();
            var p = command.Split(' ');
            int kind = int.Parse(p[0]), count = int.Parse(p[1]), folder = int.Parse(p[2]),
                mode = int.Parse(p[3]), flags = int.Parse(p[4]);
            var host = new LegacyReference {
                InventoryService = new() { Mode = mode },
                LibraryService = (mode & 3) != 0 ? new() { LibraryRootFolder = new() { Mode = mode } } : null
            };
            var client = new Client { Active = (mode & 32) == 0 };
            try
            {
                if (kind == 0)
                {
                    var items = new UUID[count];
                    var owners = new UUID[count];
                    for (int n = 0; n < count; n++)
                    {
                        items[n] = Id(300 + n);
                        owners[n] = Id(100 + n);
                    }
                    var sender = new ItemSender { m_scene = host };
                    sender.m_fetchHolder.Enqueue(new() { Client = client, Items = items, Owners = owners });
                    sender.ProcessQueue();
                }
                else
                {
                    host.HandleFetchInventoryDescendents(client, folder == 0 ? UUID.Zero : Id(folder),
                                                         Id(999), (flags & 1) != 0, (flags & 2) != 0, 99);
                    if (host.m_descendentsRequestQueue.Count > 0)
                    {
                        Trace.Add("Q");
                        if (kind == 2)
                            host.SendInventoryAsync();
                    }
                }
            }
            catch (Exception)
            {
                Trace.Add("E");
            }
            Console.WriteLine(string.Join('|', Trace.Values));
        }
    }
}
partial class ItemSender
{
    public LegacyReference m_scene;
    public ConcurrentQueue<FetchHolder> m_fetchHolder = new();
    object m_threadLock = new();
    bool m_running;
}
