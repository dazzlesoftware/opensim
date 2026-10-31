using System;using System.Collections;using OpenMetaverse;using OpenSim.Framework;
namespace log4net {class Marker {}}
namespace OpenSim.Framework {static class Util {public static int UnixTimeSinceEpoch()=>111;}}
class AssetBase {public byte[] Data;}
class AssetService {public AssetBase Get(string key){CreateReference.Step(6,"ASSET:"+key);if((CreateReference.Mode&32)!=0)return null;return new(){Data=(CreateReference.Mode&256)!=0?null:(CreateReference.Mode&2048)!=0?new byte[]{(byte)'a',0xe2,0x82,0}:new byte[]{(byte)'s',(byte)'r',(byte)'c',0}};}}
class Items {
 public void LockItemsForRead(bool v){}public void LockItemsForWrite(bool v){}
 public bool TryGetValue(UUID key,out TaskInventoryItem stored){CreateReference.Step(4,"FIND:"+key);if((CreateReference.Mode&16384)!=0){CreateReference.Input.ItemID=CreateReference.Id(10);CreateReference.Input.AssetID=CreateReference.Id(11);}stored=CreateReference.Stored;return (CreateReference.Mode&4)==0;}
}
class Permissions {public bool CanRunScript(TaskInventoryItem i,Part p){CreateReference.Step(1,"CAN:"+(i==null?"NULL":"I"));return (CreateReference.Mode&1)==0;}}
class RegionSettings {public bool DisableScripts {get{CreateReference.Step(3,"DISABLED");return (CreateReference.Mode&2)!=0;}}}
class RegionInfo {public RegionSettings RegionSettings=new();public string RegionName="Region";}
class Events {public void TriggerRezScript(uint local,UUID key,string text,int start,bool post,string engine,int source){CreateReference.Step(10,"REZ:"+local+":"+key+":"+CreateReference.Hex(text)+":"+start+":"+(post?1:0)+":"+engine+":"+source);CreateReference.CompilerError=(CreateReference.Mode&8192)!=0;}}
class Scene {public Permissions Permissions=new();public RegionInfo RegionInfo=new();public AssetService AssetService=new();public Events EventManager=new();public bool m_trustBinaries{get{CreateReference.Step(5,"TRUST");return (CreateReference.Mode&16)!=0;}}}
class Group {public Scene Scene=new();public object m_savedScriptState{get{CreateReference.Step(7,"SAVED");return (CreateReference.Mode&64)!=0?new object():null;}}public void AddActiveScriptCount(int count){CreateReference.Step(12,"ACTIVE:"+count);CreateReference.Active+=count;}}
class Part {public Group ParentGroup=new();public string Name="Part",AbsolutePosition="Position";public uint LocalId{get{CreateReference.Step(9,"LOCAL");return 123;}}public void AddFlag(PrimFlags flag){CreateReference.Step(2,"FLAG");CreateReference.Flagged=true;}public void ScheduleFullUpdate()=>CreateReference.Step(13,"FULL");}
class Logger {public void ErrorFormat(string s,params object[] args)=>CreateReference.Step(14,"LOG");}
partial class Registry {
 Part m_part=new();Items m_items=new();Logger m_log=new();
 void StoreScriptErrors(UUID key,ArrayList errors){string s="ERRORS:"+key+":";if(errors==null)s+="NULL";else foreach(string e in errors)s+=CreateReference.Hex(e)+",";CreateReference.Step(11,s);}
 UUID RestoreSavedScriptState(UUID loaded,UUID old,UUID key){CreateReference.Step(8,"RESTORE:"+loaded+":"+old+":"+key);return CreateReference.Id(9);}
}
class CreateReference {
 public static int Mode,Failure,Active;public static bool Flagged,CompilerError;public static TaskInventoryItem Input,Stored;static string Trace;
 public static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));public static void Step(int point,string value){Trace+="|"+value;if(point==Failure)throw new Exception();}public static string Hex(string s)=>Convert.ToHexString(System.Text.Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
 static string Row(TaskInventoryItem i)=>i==null?"NULL":i.ItemID+":"+i.OldItemID+":"+i.PermsMask+":"+i.PermsGranter+":"+(i.ScriptRunning?1:0);
 static string Probe(int mode,int state,int failure){Mode=mode;Failure=failure;Active=0;Flagged=CompilerError=false;Trace="";Input=new(){ItemID=Id(2),AssetID=Id(3),OldItemID=Id(4),LoadedItemID=Id(5),PermsMask=123,PermsGranter=Id(6),ScriptRunning=(mode&128)==0};Stored=(mode&8)!=0?(TaskInventoryItem)Input.Clone():Input;if((mode&8)!=0){Stored.OldItemID=Id(7);Stored.PermsMask=456;Stored.PermsGranter=Id(8);}if((mode&512)!=0)Input=null;if((mode&1024)!=0)Stored=null;string result;try{result=new Registry().CreateScriptInstance(Input,-42,true,"Native",state)?"TRUE":"FALSE";}catch{result="THROW";}return result+":"+Active+":"+(Flagged?1:0)+":"+(CompilerError?1:0)+":"+Row(Input)+":"+Row(Stored)+Trace;}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1],a[2]));}}
}
