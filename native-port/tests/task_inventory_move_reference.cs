using System;using System.Text;using OpenMetaverse;using OpenSim.Framework;using UUID=ControlledUuid;
namespace log4net {class Marker{}}
namespace OpenSim.Framework {static class Util{public static int UnixTimeSinceEpoch()=>999;}}
public readonly struct ControlledUuid {
 readonly OpenMetaverse.UUID value;public ControlledUuid(string s){value=new(s);}ControlledUuid(OpenMetaverse.UUID v){value=v;}public static ControlledUuid Zero=>default;
 public static ControlledUuid Random(){MoveReference.Step(3,"NEW");return MoveReference.Id(5);}public static bool TryParse(string s,out ControlledUuid result){bool ok=OpenMetaverse.UUID.TryParse(s,out var id);result=new(id);return ok;}
 public bool IsZero()=>value==OpenMetaverse.UUID.Zero;public bool NotEqual(ControlledUuid other)=>value!=other.value;public static bool operator==(ControlledUuid a,ControlledUuid b)=>a.value==b.value;public static bool operator!=(ControlledUuid a,ControlledUuid b)=>a.value!=b.value;public override bool Equals(object o)=>o is ControlledUuid other&&this==other;public override int GetHashCode()=>value.GetHashCode();public override string ToString()=>value.ToString();
}
partial class InventoryNodeBase {public osUTF8 UTF8Name;}
partial class InventoryItemBase:InventoryNodeBase {public osUTF8 UTF8Description;public UUID ID,Owner,AssetID,Folder,GroupID;public string CreatorId,CreatorData;public int AssetType,InvType,CreationDate,SalePrice;public byte SaleType;public bool GroupOwned;public uint BasePermissions,CurrentPermissions,NextPermissions,EveryOnePermissions,GroupPermissions,Flags;public InventoryItemBase(){MoveReference.Step(2,"TIME");CreationDate=111;}}
interface IClientAPI {UUID AgentId{get;}string Name{get;}void SendAgentAlertMessage(string message,bool modal);}
class Client:IClientAPI {public UUID AgentId=>MoveReference.Id(1);public string Name=>"Viewer";public void SendAgentAlertMessage(string s,bool modal)=>MoveReference.Step(12,"ALERT:"+(s??"NULL")+":"+(modal?1:0));}
class PartInventory {public TaskInventoryItem GetInventoryItem(UUID key){MoveReference.Step(1,"GET:"+MoveReference.Id(6)+":"+key);return (MoveReference.Mode&4)!=0||!MoveReference.Present?null:MoveReference.Task;}public int RemoveInventoryItem(UUID key){MoveReference.Step(9,"REMOVE:"+MoveReference.Id(6)+":"+key);MoveReference.Present=false;return 10;}}
class Group {public void AddActiveScriptCount(int n)=>MoveReference.Step(8,"ACTIVE:"+MoveReference.Id(6)+":"+n);}
class SceneObjectPart {public UUID UUID=MoveReference.Id(6),OwnerID=(MoveReference.Mode&2)!=0?MoveReference.Id(9):MoveReference.Id(1);public string Name="Part";public PartInventory Inventory=new();public Group ParentGroup=new();public void RemoveScriptEvents(UUID key)=>MoveReference.Step(7,"EVENTS:"+UUID+":"+key);}
class ScenePresence {public IClientAPI ControllingClient=new Client();}
class Permissions {public bool PropagatePermissions(){MoveReference.Step(4,"PROPAGATE");return (MoveReference.Mode&8)!=0;}public bool BypassPermissions(){MoveReference.Step(6,"BYPASS");return (MoveReference.Mode&16)!=0;}public bool CanCopyObjectInventory(UUID key,UUID p,UUID a){MoveReference.Step(11,"COPY:"+key+":"+p+":"+a);return (MoveReference.Mode&16384)==0;}}
class Logger {public void DebugFormat(string s,params object[] args)=>MoveReference.Step(15,"DEBUG");public void ErrorFormat(string s,params object[] args)=>MoveReference.Step(16,"ERROR");public void WarnFormat(string s,params object[] args)=>MoveReference.Step(17,"WARN");}
partial class Scene {
 public Permissions Permissions=new();Logger m_log=new();SceneObjectPart GetSceneObjectPart(uint local){MoveReference.Step(10,"PART:"+local);return (MoveReference.Mode&8192)!=0?null:new();}
 void AddInventoryItem(IClientAPI c,InventoryItemBase i){MoveReference.Step(5,"ADD:"+c.AgentId+":"+MoveReference.Dump(i));MoveReference.Added(i);}
 bool AddInventoryItem(InventoryItemBase i){MoveReference.Step(14,"OFFLINE:"+MoveReference.Dump(i));MoveReference.Added(i);return false;}
 bool TryGetScenePresence(UUID a,out ScenePresence presence){MoveReference.Step(13,"CLIENT:"+a);presence=new();return (MoveReference.Mode&4096)==0;}
 public string Probe(int kind,UUID key){var part=new SceneObjectPart();string message;InventoryItemBase i;if(kind==0)i=CreateAgentInventoryItemFromTask(MoveReference.Id(1),part,key,out message);else if(kind==1){RemoveNonCopyTaskItemFromPrim(part,key);return "VOID";}else if(kind==2)i=MoveTaskInventoryItem(new Client(),MoveReference.Id(7),part,key,out message);else if(kind==3){ClientMoveTaskInventoryItem(new Client(),MoveReference.Id(7),123,key);return "VOID";}else i=MoveTaskInventoryItem((MoveReference.Mode&1048576)!=0?MoveReference.Id(9):MoveReference.Id(1),MoveReference.Id(7),part,key,out message);return (i==null?"NULL":MoveReference.Dump(i))+":"+(message??"NULL");}
}
class MoveReference {
 public static int Mode,Failure,Stored;public static bool Present;public static TaskInventoryItem Task;static string Trace;
 public static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));
 public static void Step(int point,string s){Trace+="|"+s;if(point==Failure)throw new Exception();}
 public static void Added(InventoryItemBase i){Stored++;if((Mode&65536)!=0)Present=false;if((Mode&131072)!=0)Task.CurrentPermissions&=~0x8000u;i.Folder=Id(8);}
 static string Text(string s)=>s==null?"NULL":Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
 public static string Dump(InventoryItemBase i)=>i.ID+":"+i.Owner+":"+i.AssetID+":"+i.CreatorId+":"+Text(i.CreatorData)+":"+Text(i.Name)+":"+Text(i.Description)+":"+i.AssetType+":"+i.InvType+":"+i.BasePermissions+":"+i.CurrentPermissions+":"+i.NextPermissions+":"+i.EveryOnePermissions+":"+i.GroupPermissions+":"+i.Flags+":"+i.CreationDate+":"+i.GroupID+":"+(i.GroupOwned?1:0)+":"+i.SalePrice+":"+i.SaleType+":"+i.Folder;
 static string Probe(int kind,int mode,int perm,int failure){Mode=mode;Failure=failure;Stored=0;Present=true;Trace="";Task=new(){ItemID=Id(2),AssetID=Id(3),CreatorID=Id(4),OwnerID=(mode&1)!=0?Id(9):Id(1),Type=(mode&256)!=0?10:7,InvType=(mode&128)!=0?6:10,BasePermissions=0xf00fffff,NextPermissions=0x9e000,EveryonePermissions=0x8a000,GroupPermissions=0x84000,CurrentPermissions=0x80000u|((uint)(perm&240)<<9)|(uint)(perm&15),Flags=0xffffffff,CreatorData=(mode&512)!=0?null:"https://profile;Creator",Name=(mode&2048)!=0?null:(mode&1024)!=0?"A\0é":"Script",Description=(mode&2048)!=0?null:(mode&1024)!=0?"D\0中":(mode&524288)!=0?" \t\n　":"Description"};if((mode&32)!=0)Task.CurrentPermissions&=~0x8000u;if((mode&64)!=0)Task.CurrentPermissions&=~0x2000u;string result;try{result=new Scene().Probe(kind,(mode&32768)!=0?UUID.Zero:Id(2));}catch{result="THROW";}return result+":"+(Present?1:0)+":"+Stored+Trace;}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1],a[2],a[3]));}}
}
