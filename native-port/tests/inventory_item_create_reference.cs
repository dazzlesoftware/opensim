using System;using OpenMetaverse;using OpenMetaverse.Packets;
enum ThrottleOutPacketType {Asset}
class PacketPool {public static PacketPool Instance=new();public Packet GetPacket(PacketType t)=>Packet.BuildPacket(t);}
class Util {public static byte[] StringToBytes256(ReadOnlySpan<char> value)=>Utils.StringToBytes(value,255);}
partial class InventoryNodeBase {public osUTF8 UTF8Name;}
partial class InventoryItemBase:InventoryNodeBase {public osUTF8 UTF8Description;public string m_creatorId;public UUID m_creatorIdAsUuid;public UUID ID,AssetID,Folder,Owner,GroupID;public uint BasePermissions,CurrentPermissions,NextPermissions,GroupPermissions,EveryOnePermissions,Flags;public int AssetType,InvType,CreationDate,SalePrice;public byte SaleType;public bool GroupOwned;}
partial class ItemCreateReference {
 UUID m_agentId=Id(1);string Sent;static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));
 void OutPacket(Packet p,ThrottleOutPacketType t){var raw=p.ToBytes();var wire=new byte[raw.Length*2+32];int n=Helpers.ZeroEncode(raw,raw.Length,wire);Sent=Convert.ToHexString(raw).ToLowerInvariant()+"|"+Convert.ToHexString(wire,0,n).ToLowerInvariant();}
 static string Text(int m){switch(m&7){case 0:return "";case 1:return "item <name> & description";case 2:return new('a',300);case 3:return new('é',150);case 4:return new('中',100);case 5:return string.Concat(System.Linq.Enumerable.Repeat("😃",100));case 6:return "abc\0suffix";default:return " \t\n　";}}
 static string Probe(int seed,int m){var i=new InventoryItemBase{ID=Id(2),AssetID=Id(3),Folder=Id(4),Owner=Id(5),GroupID=Id(6),CreatorId=(m&32)!=0?"invalid":Id(7).ToString(),Name=Text(m),Description=Text(m+1),AssetType=seed,InvType=-seed,BasePermissions=0xffffffff,CurrentPermissions=unchecked(0x80000000u+(uint)seed),NextPermissions=0x9e000,GroupPermissions=0x84000,EveryOnePermissions=0x8a000,Flags=0xffffffff,GroupOwned=(m&8)!=0,SalePrice=seed,SaleType=unchecked((byte)seed),CreationDate=seed};if((m&64)!=0)i.CreatorId+=";profile;name";var host=new ItemCreateReference();host.SendInventoryItemCreateUpdate(i,(m&128)!=0?Id(8):UUID.Zero,unchecked((uint)seed));return host.Sent;}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1]));}}
}
