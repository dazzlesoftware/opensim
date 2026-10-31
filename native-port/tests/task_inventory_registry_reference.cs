using System;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;
namespace log4net {class Marker {}}
namespace OpenSim.Framework {static class Util {public static int UnixTimeSinceEpoch()=>111;}}
enum Changed {INVENTORY=1,ALLOWED_DROP=64}
class TaskInventoryDictionary:Dictionary<UUID,TaskInventoryItem> {public void LockItemsForRead(bool value){}public void LockItemsForWrite(bool value){}}
class Logger {public void ErrorFormat(string s,params object[] args)=>RegistryReference.Add("L");public void Error(string s)=>RegistryReference.Add("L");}
class Events {public void TriggerRemoveScript(uint local,UUID item)=>RegistryReference.Add("R"+local+":"+item);}
class RegionInfo {public string RegionName="region";}
class Scene {public Events EventManager=new();public RegionInfo RegionInfo=new();}
class Group {public Scene Scene=new();public bool HasGroupChanged{set{RegistryReference.Add("H");}}public void InvalidateDeepEffectivePerms()=>RegistryReference.Add("D");}
class Part {public UUID UUID=RegistryReference.Id(10),GroupID=RegistryReference.Id(11);public uint LocalId=123;public string Name="part",AbsolutePosition="position";public Group ParentGroup=new();public void RemFlag(PrimFlags flag)=>RegistryReference.Add("X");public void ScheduleFullUpdate()=>RegistryReference.Add("F");public void AggregateInnerPerms()=>RegistryReference.Add("A");public void TriggerScriptChangedEvent(Changed mask)=>RegistryReference.Add("C"+(int)mask);public void TriggerScriptChangedEvent(Changed mask,UUID id)=>RegistryReference.Add("C"+(int)mask+":"+id);}
partial class Registry {
 Part m_part=new();Logger m_log=new();TaskInventoryDictionary m_items=new();Dictionary<UUID,TaskInventoryItem> m_scripts;uint m_inventorySerial;bool HasInventoryChanged;
 public void Serial(uint value){m_inventorySerial=value;}public void ResetChanged(){HasInventoryChanged=false;}
 public string Snapshot(){string s=m_inventorySerial+":"+(HasInventoryChanged?1:0)+":"+(m_scripts!=null?1:0);foreach(var i in m_items.Values)s+="["+RegistryReference.Row(i)+"]";s+="S";if(m_scripts!=null)foreach(var i in m_scripts.Values)s+="["+RegistryReference.Row(i)+"]";return s;}
}
class RegistryReference {
 static string Trace;
 public static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));
 public static void Add(string s){Trace+="/"+s;}
 public static string Row(TaskInventoryItem i)=>i.ItemID+":"+(i.Name??"NULL")+":"+i.InvType+":"+i.AssetID+":"+i.OwnerID+":"+i.GroupID+":"+i.ParentID+":"+i.GroupPermissions;
 static TaskInventoryItem Make(int n,string name,int type)=>new(){ItemID=Id(n),Name=name,InvType=type,AssetID=Id(n+20),OwnerID=Id(9)};
 static string Probe(int mode){var r=new Registry();r.Serial((mode&128)!=0?0xffffffffu:0);string result="";
 void Step(Func<int> body){Trace="";string status;try{status=body().ToString();}catch{status="E";}result+="|"+status+Trace+"#"+r.Snapshot();}
 int AddItem(TaskInventoryItem i,bool drop=false){int old=Count(r);r.AddInventoryItem(i,drop);return Count(r)>old?1:0;}
 if((mode&256)!=0){for(int n=1;n<=256;++n)AddItem(Make(n,"Pool",10));Step(()=>AddItem(Make(300,"Pool",10)));Step(()=>AddItem(Make(301,"pool",10)));var live=r.GetInventoryItem(Id(1));live.Name=null;Step(()=>AddItem(Make(302,null,10)));live.ItemID=Id(999);Step(()=>r.UpdateInventoryItem(live,true,true)?1:0);Step(()=>r.RemoveInventoryItem(Id(1)));return result;}
 string name=(mode&1)!=0?null:(mode&2)!=0?"":"Script";var a=Make(1,name,(mode&4)!=0?7:10);var b=Make(2,name,10);var c=Make(3,"Different",10);
 Step(()=>AddItem(a,(mode&8)!=0));Step(()=>AddItem(b));Step(()=>AddItem(c));Step(()=>AddItem((mode&16)!=0?a:Make(1,name,7)));
 r.ResetChanged();Step(()=>{var i=r.GetInventoryItem(Id(2));if(i==null)return 0;i.AssetID=UUID.Zero;i.Name="Live";i.GroupPermissions=0;return r.UpdateInventoryItem(i,(mode&32)==0,(mode&64)==0)?1:0;});
 Step(()=>{var i=Make(3,"Replacement",(mode&4)!=0?10:7);i.AssetID=UUID.Zero;i.OwnerID=UUID.Zero;i.GroupID=Id(12);i.GroupPermissions=(mode&32)!=0?0u:1u;return r.UpdateInventoryItem(i,(mode&32)==0,(mode&64)==0)?1:0;});
 Step(()=>r.UpdateInventoryItem(Make(99,"Missing",10),true,true)?1:0);Step(()=>r.RemoveInventoryItem(Id(1)));Step(()=>r.RemoveInventoryItem(Id(2)));Step(()=>AddItem(Make(4,"Slot",10)));Step(()=>AddItem(Make(5,"Slot",7)));Step(()=>r.RemoveInventoryItem(Id(3)));Step(()=>r.RemoveInventoryItem(Id(5)));Step(()=>AddItem(Make(6,"Slot",10)));Step(()=>r.RemoveInventoryItem(Id(99)));return result;}
 static int Count(Registry r)=>r.Count;
 static void Main(){string line;while((line=Console.ReadLine())!=null)Console.WriteLine(Probe(int.Parse(line)));}
}
partial class Registry {public int Count=>m_items.Count;}
