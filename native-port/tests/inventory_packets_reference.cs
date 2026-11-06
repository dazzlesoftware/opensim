using System;
using System.Collections.Generic;
using OpenMetaverse;
using OpenMetaverse.Packets;
enum ThrottleOutPacketType
{
    Asset
}
class PacketPool
{
    public static PacketPool Instance = new();
    public Packet GetPacket(PacketType type) => Packet.BuildPacket(type);
}
class Util
{
    public static byte[] StringToBytes256(ReadOnlySpan<char> value) => Utils.StringToBytes(value, 255);
}
class InventoryFolderBase
{
    public UUID ID, ParentID;
    public string Name;
    public short Type;
}
class InventoryItemBase
{
    public UUID ID, AssetID, CreatorIdAsUuid, Folder, Owner, GroupID;
    public uint BasePermissions, CurrentPermissions, NextPermissions, EveryOnePermissions, GroupPermissions,
        Flags;
    public string Name, Description;
    public int InvType, AssetType, CreationDate, SalePrice;
    public byte SaleType;
    public bool GroupOwned;
}
partial class InventoryPacketsReference
{
    UUID m_agentId = Id(1);
    List<string> Sent = new();
    static UUID Id(int n) => new UUID("11223344-5566-7788-99aa-" + n.ToString().PadLeft(12, '0'));
    static InventoryItemBase Sample(int n, int seed, int mode)
    {
        var i = new InventoryItemBase { ID = Id(100 + n),
                                        Folder = Id(2),
                                        AssetID = Id(300 + n),
                                        Owner = Id(3),
                                        GroupID = Id(4),
                                        CreatorIdAsUuid = Id(5),
                                        BasePermissions = uint.MaxValue,
                                        CurrentPermissions = (uint)(int.MaxValue - seed),
                                        NextPermissions = unchecked((uint)-seed),
                                        EveryOnePermissions = (uint)(seed * 19),
                                        GroupPermissions = unchecked((uint)-19),
                                        Flags = unchecked((uint)(-1 - seed)),
                                        InvType = seed % 2 != 0 ? 257 : -129,
                                        AssetType = seed % 2 != 0 ? -129 : 257,
                                        GroupOwned = seed % 2 != 0,
                                        CreationDate = -1000 - seed,
                                        SalePrice = -seed * 11,
                                        SaleType = unchecked((byte)(seed % 2 != 0 ? 257 : -1)),
                                        Name = "name" + n,
                                        Description = "description" };
        if ((mode & 1) != 0)
            i.Name = new string('x', 300);
        if ((mode & 2) != 0)
        {
            i.Name = new string('x', 252) + "雪🙂";
            i.Description = "é雪🙂";
        }
        if ((mode & 4) != 0)
        {
            i.Name = "a\0b";
            i.Description = "\0trailing";
        }
        if ((mode & 8) != 0)
            i.Description = "";
        return i;
    }
    void OutPacket(Packet packet, ThrottleOutPacketType throttle)
    {
        _ = packet.Length;
        Emit(packet, true);
    }
    void OutPacket(Packet packet, ThrottleOutPacketType throttle, bool split)
    {
        Emit(packet, split);
    }
    void Emit(Packet packet, bool split)
    {
        var raw = packet.ToBytes();
        var parts = split ? packet.ToBytesMultiple() : new[] { raw };
        var encoded = new List<string>();
        foreach (var bytes in parts)
        {
            var destination = new byte[bytes.Length * 2 + 32];
            int n = Helpers.ZeroEncode(bytes, bytes.Length, destination);
            encoded.Add(Convert.ToHexString(destination, 0, n).ToLowerInvariant());
        }
        Sent.Add((split ? "1:" : "0:") + Convert.ToHexString(raw).ToLowerInvariant() + ":" +
                 string.Join(',', encoded));
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int kind = int.Parse(p[0]), seed = int.Parse(p[1]), ni = int.Parse(p[2]), nf = int.Parse(p[3]),
                mode = int.Parse(p[4]), flags = int.Parse(p[5]);
            var items = new List<InventoryItemBase>();
            var folders = new List<InventoryFolderBase>();
            for (int n = 0; n < ni; n++)
                items.Add((mode & 16) != 0 && n == ni - 1 ? null : Sample(n, seed, mode));
            for (int n = 0; n < nf; n++)
                folders.Add((mode & 32) != 0 && n == nf - 1
                                ? null
                                : new() { ID = Id(600 + n), ParentID = Id(2), Type = (short)(n + 129),
                                          Name = (mode & 1) != 0 ? new string('f', 300) : "folder雪" + n });
            var host = new InventoryPacketsReference();
            string status = "OK";
            try
            {
                if (kind == 0)
                    host.SendInventoryItemDetails(items.ToArray());
                else
                    host.SendInventoryFolderDetails(Id(3), Id(2), items, folders, -seed, ni + nf,
                                                    (flags & 1) != 0, (flags & 2) != 0);
            }
            catch (NullReferenceException)
            {
                status = "NULL";
            }
            catch (Exception)
            {
                status = "ERROR";
            }
            Console.WriteLine(status + (host.Sent.Count > 0 ? "|" + string.Join('|', host.Sent) : ""));
        }
    }
}
