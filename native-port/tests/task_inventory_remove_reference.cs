using System;using OpenMetaverse;using OpenSim.Framework;
namespace log4net {class Marker{}}
namespace OpenSim.Framework {static class Util{public static int UnixTimeSinceEpoch()=>111;}}
interface IClientAPI {UUID AgentId{get;}void SendAgentAlertMessage(string message,bool modal);}
class Client:IClientAPI {public UUID AgentId=>RemoveReference.Id(1);public void SendAgentAlertMessage(string m,bool modal)=>RemoveReference.Step(7,"ALERT:"+(m??"NULL")+":"+(modal?1:0));}
class InventoryFolderBase {public UUID ID=RemoveReference.Id(4);}
class InventoryItemBase {}
class Inventory {public InventoryFolderBase GetFolderForType(UUID a,FolderType type){RemoveReference.Step(4,"TRASH:"+a+":"+(int)type);return (RemoveReference.Mode&16)!=0?null:new();}}
class Permissions {public bool CanEditObjectInventory(UUID p,UUID a){RemoveReference.Step(2,"EDIT:"+p+":"+a);return (RemoveReference.Mode&4)==0;}}
class Logger {public void WarnFormat(string fmt,params object[] values)=>RemoveReference.Step(6,"WARN:"+values[0]+":"+(values[1]??"NULL"));}
class Group {
 public TaskInventoryItem GetInventoryItem(uint local,UUID key){++RemoveReference.GetCount;RemoveReference.Step(RemoveReference.GetCount==1?3:13,"GET:"+RemoveReference.Id(3)+":"+local+":"+key);if((RemoveReference.Mode&8)!=0||!RemoveReference.Present)return null;if(RemoveReference.GetCount>1&&(RemoveReference.Mode&4096)!=0){var other=(TaskInventoryItem)RemoveReference.Item.Clone();other.Type=other.Type==10?7:10;return other;}return RemoveReference.Item;}
 public void AddActiveScriptCount(int count)=>RemoveReference.Step(9,"ACTIVE:"+RemoveReference.Id(3)+":"+count);
 public int RemoveInventoryItem(uint local,UUID key){RemoveReference.Step(10,"REMOVE:"+RemoveReference.Id(3)+":"+local+":"+key);RemoveReference.Present=false;RemoveReference.Serial++;return 10;}
 public void InvalidateEffectivePerms()=>RemoveReference.Step(11,"PERMS:"+RemoveReference.Id(3));
}
class SceneObjectPart {public UUID UUID=RemoveReference.Id(3);public Group ParentGroup=(RemoveReference.Mode&2)!=0?null:new();public void RemoveScriptEvents(UUID item)=>RemoveReference.Step(8,"EVENTS:"+UUID+":"+item);public void SendPropertiesToClient(IClientAPI c)=>RemoveReference.Step(12,"PROPS:"+UUID+":"+c.AgentId);}

partial class Scene {
 public Permissions Permissions=new();public Inventory InventoryService=new();Logger m_log=new();
 SceneObjectPart GetSceneObjectPart(uint local){RemoveReference.Step(1,"PART:"+local);return (RemoveReference.Mode&1)!=0?null:new();}
 InventoryItemBase MoveTaskInventoryItem(IClientAPI c,UUID folder,SceneObjectPart p,UUID key,out string message){RemoveReference.Step(5,"MOVE:"+c.AgentId+":"+folder+":"+p.UUID+":"+key);if((RemoveReference.Mode&64)!=0)RemoveReference.Present=false;if((RemoveReference.Mode&256)!=0)RemoveReference.Item.Type=RemoveReference.Item.Type==10?7:10;message=(RemoveReference.Mode&512)!=0?null:"failure";return (RemoveReference.Mode&32)!=0?null:new();}
}
class RemoveReference {
 public static int Mode,Failure,Serial,GetCount;public static bool Present;public static TaskInventoryItem Item;static string Trace;
 public static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));
 public static void Step(int point,string s){Trace+="|"+s;if(point==Failure)throw new Exception();}
 static string Probe(int mode,int failure){Mode=mode;Failure=failure;Serial=GetCount=0;Present=true;Trace="";Item=new(){ItemID=Id(2),Type=(mode&128)!=0?7:10};string status="OK";try{new Scene().RemoveTaskInventory(new Client(),(mode&1024)!=0?UUID.Zero:Id(2),(mode&2048)!=0?0u:123u);}catch{status="THROW";}return status+":"+(Present?1:0)+":"+Serial+Trace;}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1]));}}
}

