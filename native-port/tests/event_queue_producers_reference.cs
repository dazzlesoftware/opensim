using System;
using System.Collections.Generic;
using System.Text;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Region.ClientStack.Linden;
using OpenSim.Framework;
namespace OpenSim.Framework
{
    public class PlacesReplyData
    {
        public UUID OwnerID, SnapshotID;
        public string Name, Desc, SimName;
        public int ActualArea, BillableArea, Price;
        public byte Flags;
        public uint GlobalX, GlobalY, GlobalZ, Dwell;
    }
    public class InventoryItemBase
    {
        public UUID ID, AssetID, Folder, Owner, GroupID, CreatorIdAsUuid;
        public string Name, Description;
        public int AssetType, InvType, CreationDate, SalePrice;
        public uint Flags, CurrentPermissions, NextPermissions, BasePermissions, EveryOnePermissions,
            GroupPermissions;
        public byte SaleType;
        public bool GroupOwned;
    }
}
public static class Producers
{
    public static void Produce(EventQueueGetModule m, UUID a, char op, int p)
    {
        UUID id = (p & 1) != 0 ? UUID.Zero : new UUID("01234567-89ab-cdef-0123-456789abcdef");
        string text = (p % 6) switch {
            0 => null,
            1 => "",
            2 => "<&\"'>",
            3 => "世界😀",
            4 => new string('é', 300),
            _ => "a\0b"
        };
        System.Net.IPEndPoint endpoint = new(
            System.Net.IPAddress.Parse((p & 1) != 0 ? "fe80::1%12" : "192.0.2.37"), (p & 2) != 0 ? 65535 : 0);
        if ((p & 16) != 0)
            endpoint = null;
        int n = (p & 2) != 0 ? -1 : 0;
        uint u = unchecked((uint)n);
        ulong handle = (p & 2) != 0 ? ulong.MaxValue : 0;
        Vector3 v = new((float)p / 7, -0.0f, (p & 4) != 0 ? 1e-20f : -123.25f);
        byte[] bucket = (p & 4) != 0 ? null : new byte[] { 0, 128, 255, 60, 38 };
        if (p >= 32)
        {
            uint[] bits = { 0x7f800000, 0xff800000, 0x7fc00000, 0x80000000,
                            1,          0x7f7fffff, 0x3eaaaaab, 0x00800000 };
            v.X = BitConverter.Int32BitsToSingle(unchecked((int)bits[p % 8]));
        }
        switch (op)
        {
        case 'e':
            m.EnableSimulator(handle, endpoint, a, n, 256);
            break;
        case 'f':
            m.TeleportFinishEvent(handle, (byte)u, endpoint, u, u, text, a, n, 256);
            break;
        case 'c':
            m.CrossRegion(handle, v, v, endpoint, text, a, id, n, 256);
            break;
        case 'a':
            m.EstablishAgentCommunication(a, endpoint, text, handle, n, 256);
            break;
        case 'i':
            m.ChatterboxInvitation(id, text, id, text, a, text, (byte)u, u, (p & 1) != 0, n, v, u, id,
                                   (p & 2) != 0, bucket);
            break;
        case 'u':
            var updates = new List<OpenSim.Region.Framework.Interfaces.GroupChatListAgentUpdateData>();
            for (int i = 0; i < p % 3; ++i)
                updates.Add(((p / 4) % 4) switch {
                    0 => new(id),
                    1 => new(id, (p & 8) != 0),
                    2 => new(id, (p & 1) != 0, (p & 2) != 0, (p & 4) != 0, (p & 8) != 0),
                    _ => new()
                });
            m.ChatterBoxSessionAgentListUpdates(id, a, p == 31 ? null : updates);
            break;
        case 'r':
            m.ChatterBoxSessionStartReply(id, text, n, (p & 1) != 0, (p & 2) != 0, id, (p & 4) != 0, text, a);
            break;
        case 'q':
            m.ChatterBoxForceClose(a, id, text);
            break;
        case 'g':
            var groups = new List<GroupMembershipData>();
            for (int i = 0; i < p % 3; ++i)
                groups.Add((p & 16) != 0
                               ? null
                               : new() { GroupID = id, GroupPicture = id, GroupName = text,
                                         GroupPowers = handle, Contribution = n, AcceptNotices = (p & 1) != 0,
                                         ListInProfile = (p & 2) != 0 });
            m.GroupMembershipData(a, p == 31 ? null : groups.ToArray());
            break;
        case 'p':
            var places = new List<PlacesReplyData>();
            for (int i = 0; i < p % 3; ++i)
                places.Add((p & 16) != 0
                               ? null
                               : new() { OwnerID = id, SnapshotID = id, Name = text, Desc = text,
                                         SimName = text, GlobalX = u, GlobalY = u, GlobalZ = u, Dwell = u,
                                         ActualArea = n, BillableArea = n, Price = n, Flags = (byte)u });
            m.PlacesQueryReply(a, id, id, p == 31 ? null : places.ToArray());
            break;
        case 'j':
            m.ScriptRunningEvent(id, id, (p & 1) != 0, a);
            break;
        case 'h':
            m.partPhysicsProperties(u, (byte)u, v.X, v.Y, v.Z, n, a);
            break;
        case 'w':
            m.WindlightRefreshEvent(p - 16, a);
            break;
        case 'l':
            m.SendLargeGenericMessage(a, (p & 1) != 0 ? id : null, (p & 2) != 0 ? id : null, text, id,
                                      p == 31 ? null : new List<string> { text, "next" });
            break;
        case 't':
            m.SendLargeGenericMessage(a, (p & 1) != 0 ? id : null, (p & 2) != 0 ? id : null, text, id,
                                      p == 31 ? null
                                              : new List<byte[]> { bucket, Encoding.UTF8.GetBytes("next") });
            break;
        case 'v':
            var item = new InventoryItemBase { ID = id,
                                               AssetID = id,
                                               Folder = id,
                                               Owner = id,
                                               GroupID = id,
                                               CreatorIdAsUuid = id,
                                               Name = text ?? "",
                                               Description = text ?? "",
                                               AssetType = n,
                                               InvType = n,
                                               CreationDate = n,
                                               Flags = u,
                                               CurrentPermissions = u,
                                               NextPermissions = u,
                                               BasePermissions = u,
                                               EveryOnePermissions = u,
                                               GroupPermissions = u,
                                               SalePrice = n,
                                               SaleType = (byte)p,
                                               GroupOwned = (p & 1) != 0 };
            m.SendBulkUpdateInventoryItem(p == 31 ? null : item, a, (p & 1) != 0 ? id : null);
            break;
        case 'b':
            Console.Write(Convert.ToHexString(m.BuildEvent(text, p == 31 ? null : new OSDString(text ?? "")))
                              .ToLowerInvariant());
            break;
        case 'k':
            Console.Write(Convert.ToHexString(Encoding.UTF8.GetBytes(EventQueueGetModule.KeepAliveEvent()))
                              .ToLowerInvariant());
            break;
        case 's':
            Console.Write(
                Convert.ToHexString(m.EndEventToBytes(m.StartEvent(text, p - 16))).ToLowerInvariant());
            break;
        }
    }
}
