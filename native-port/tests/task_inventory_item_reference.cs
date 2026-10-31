using System;using System.Text;using OpenSim.Framework;using UUID=ControlledUuid;
namespace log4net {class Marker {}}
namespace OpenSim.Framework {static class Util {public static int UnixTimeSinceEpoch(){ModelReference.ClockCalls++;return ModelReference.Clock;}}}
public readonly struct ControlledUuid {
 readonly OpenMetaverse.UUID value;public ControlledUuid(string s){value=new(s);}ControlledUuid(OpenMetaverse.UUID id){value=id;}
 public static ControlledUuid Zero=>default;public static bool TryParse(string text,out ControlledUuid id){bool result=OpenMetaverse.UUID.TryParse(text,out var sdk);id=new(sdk);return result;}
 public static ControlledUuid Random(){ModelReference.RandomCalls++;if((ModelReference.Mode&128)!=0||((ModelReference.Mode&512)!=0&&ModelReference.RandomCalls==2))throw new Exception();return ModelReference.Id(8+ModelReference.RandomCalls);}
 public override string ToString()=>value.ToString();
}
class ModelReference {
 public static int Mode,Clock,ClockCalls,RandomCalls;
 public static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));
 static string Str(string s)=>s==null?"-":Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
 static string[] Inputs()=>new[]{null,"",Id(2).ToString(),"invalid",Id(2)+";https://profile",Id(2)+";https://profile;Display Name",Id(2)+";profile;Name;ignored;tail",";",";;",";;;extra","bad;profile;name",Id(2)+";;",Id(2)+";"," "+Id(2)+" ","{"+Id(2)+"}","112233445566778899aa000000000002",Id(2)+";é中;😃","bad\0uuid",Id(2)+"; ",Id(2)+";profile;",Id(2)+";profile;;ignored",Id(2)+"\n","00000000-0000-0000-0000-000000000000"};
 static string Dump(TaskInventoryItem i)=>i.AssetID+","+i.CreationDate+","+i.CreatorID+","+Str(i.CreatorData)+","+i.Flags+","+i.Type+","+i.InvType+","+i.ItemID+","+i.OldItemID+","+i.LoadedItemID+","+i.RezzerID+","+Str(i.Name)+","+Str(i.Description)+","+i.OwnerID+","+i.LastOwnerID+","+i.GroupID+","+i.ParentID+","+i.ParentPartID+","+i.BasePermissions+","+i.CurrentPermissions+","+i.EveryonePermissions+","+i.GroupPermissions+","+i.NextPermissions+","+i.PermsGranter+","+i.PermsMask+","+(i.OwnerChanged?1:0)+","+(i.ScriptRunning?1:0)+","+Str(i.CreatorIdentification);
 static string Probe(int c,int mode,int clock){Mode=mode;Clock=clock;ClockCalls=RandomCalls=0;var item=new TaskInventoryItem();string result=Dump(item);
 if((mode&1)!=0){item.AssetID=Id(1);item.Flags=0x8000ffff;item.Type=-42;item.InvType=57;item.ItemID=Id(2);item.OldItemID=Id(3);item.LoadedItemID=Id(4);item.RezzerID=Id(5);item.Name="name";item.Description="desc";item.OwnerID=Id(6);item.LastOwnerID=Id(7);item.GroupID=Id(8);item.ParentID=Id(9);item.ParentPartID=Id(10);item.BasePermissions=1;item.CurrentPermissions=0x80000000;item.EveryonePermissions=3;item.GroupPermissions=0xffffffff;item.NextPermissions=5;item.PermsGranter=Id(11);item.PermsMask=-2147483647;item.OwnerChanged=true;item.ScriptRunning=false;}
 item.CreatorID=Id(1);item.CreatorData=(mode&2)!=0?null:(mode&4)!=0?"":"previous;data";item.CreatorIdentification=Inputs()[c];if((mode&8)!=0)item.CreatorIdentification=Id(2).ToString();if((mode&16)!=0)item.CreatorData=null;result+="|SET:"+Dump(item);
 if((mode&32)!=0){var clone=(TaskInventoryItem)item.Clone();item.Name="changed";item.CreatorData="changed";item.CreatorID=Id(8);item.ItemID=Id(8);item.OwnerChanged=!item.OwnerChanged;result+="|CLONE:"+Dump(clone);}
 try{if((mode&64)!=0)item.ResetIDs(Id(5));if((mode&256)!=0)item.ResetIDs(Id(6));}catch{result+="|THROW";}
 return result+"|FINAL:"+Dump(item)+"|C:"+ClockCalls+"|R:"+RandomCalls;}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1],a[2]));}}
}

