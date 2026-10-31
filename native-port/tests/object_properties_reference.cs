using System;using OpenMetaverse;using OpenSim.Region.ClientStack.LindenUDP;
namespace OpenSim.Framework {public static class Util {public unsafe static int osUTF8Getbytes(string s,byte* b,int n,bool term)=>throw new NotSupportedException("unused encoder method");}}
class Group {public SceneObjectPart RootPart;}
class SceneObjectPart {
 public Group ParentGroup;public UUID UUID,CreatorID,OwnerID,GroupID,FromUserInventoryItemID,LastOwnerID;
 public int CreationDate,SalePrice;public uint BaseMask=0x9e000,OwnerMask=0x9e000,GroupMask,EveryoneMask,NextOwnerMask=0x82000,Category,InventorySerial;public byte ObjectSaleType;
 public osUTF8 osUTF8Name,osUTF8Description,osUTF8TouchName,osUTF8SitName;
}
partial class ObjectPropertiesReference {
 static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));
 static string Text(int mode){switch(mode&7){case 0:return "";case 1:return "Object <name> & desc";case 2:return new('a',255);case 3:return new('é',70);case 4:return new('中',70);case 5:return string.Concat(System.Linq.Enumerable.Repeat("😃",70));case 6:return "abc\0suffix";default:return "\r\n\t\"'é中😃";}}
 static osUTF8 Utf8(string s,int max)=>s.Length==0?null:new osUTF8(s,max);
 static string Probe(int seed,int mode){var o=new SceneObjectPart{UUID=Id(1),CreatorID=Id(2),OwnerID=Id(3),GroupID=(mode&8)!=0?Id(3):Id(4),LastOwnerID=(mode&16)!=0?UUID.Zero:Id(5),FromUserInventoryItemID=(mode&32)!=0?Id(6):UUID.Zero,CreationDate=seed};
 var root=new SceneObjectPart();o.ParentGroup=new(){RootPart=root};if((mode&64)!=0){root.BaseMask=0xffffffff;root.OwnerMask=0;root.GroupMask=0x12345678;root.EveryoneMask=0x87654321;root.NextOwnerMask=0x80000000;}
 if((mode&128)!=0){root.ObjectSaleType=255;root.SalePrice=seed;o.Category=unchecked((uint)seed);o.InventorySerial=unchecked((uint)seed);}
 o.osUTF8Name=Utf8(Text(mode),63);o.osUTF8Description=Utf8(Text(mode+1),127);o.osUTF8TouchName=Utf8(Text(mode+2),36);o.osUTF8SitName=Utf8(Text(mode+3),36);
 byte[] buf=new byte[4096];byte[] header={0xc0,0,0,0,0,0,0xff,9};Array.Copy(header,buf,8);var zc=new LLUDPZeroEncoder(buf);zc.Position=8;zc.AddByte(1);CreateObjectPropertiesBlock(o,zc);int length=zc.Finish();return Convert.ToHexString(buf.AsSpan(0,length)).ToLowerInvariant();}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1]));}}
}
