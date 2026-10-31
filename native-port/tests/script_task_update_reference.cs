using System;using System.Collections;using OpenMetaverse;
interface IClientAPI {UUID AgentId{get;}void SendAgentAlertMessage(string text,bool modal);}
class Client:IClientAPI {public UUID AgentId{get;set;}public void SendAgentAlertMessage(string text,bool modal)=>ScriptUpdateReference.Step(2,"AL:"+text+":"+(modal?1:0));}
class ScenePresence {public IClientAPI ControllingClient;}
class Logger {public void ErrorFormat(string text,params object[] values)=>ScriptUpdateReference.Step(4,"LOG");}
class Permissions {public bool CanEditScript(UUID i,UUID o,UUID a){ScriptUpdateReference.Step(1,"EDIT:"+i+":"+o+":"+a);return (ScriptUpdateReference.Mode&1)==0;}}
class TaskInventoryItem {
 public UUID ItemID;public string Name="Script <name>",Description="description & text";
 public bool ScriptRunning {set{ScriptUpdateReference.Step(6,"RUN:"+(value?1:0));ScriptUpdateReference.Running=value;}}
 public UUID AssetID {get{ScriptUpdateReference.Step(16,"GETASSET");return ScriptUpdateReference.Asset;}set{ScriptUpdateReference.Step(10,"ASSET:"+value);ScriptUpdateReference.Asset=value;}}
}
class AssetBase {
 public UUID FullID;public string Name,Description,Creator;public sbyte Type;public byte[] Data;
 public AssetBase(UUID ignored,string name,sbyte type,string creator){ScriptUpdateReference.Step(7,"NEW");FullID=ScriptUpdateReference.Id(3);Name=name;Type=type;Creator=creator;}
}
class Assets {public string Store(AssetBase a){ScriptUpdateReference.Step(8,"STORE:"+a.FullID+":"+a.Type+":"+a.Name+":"+a.Description+":"+a.Creator+":"+Convert.ToHexString(a.Data).ToLowerInvariant());if((ScriptUpdateReference.Mode&16)!=0)a.FullID=ScriptUpdateReference.Id(4);return "ignored";}}
class PartInventory {
 public void RemoveScriptInstance(UUID i,bool delete){ScriptUpdateReference.Step(9,"REMOVE:"+i+":"+(delete?1:0));}
 public ArrayList CreateScriptInstanceEr(UUID i,int start,bool post,string engine,int source){ScriptUpdateReference.Step(14,"COMPILE:"+i+":"+start+":"+(post?1:0)+":"+engine+":"+source);if((ScriptUpdateReference.Mode&128)!=0)ScriptUpdateReference.Asset=ScriptUpdateReference.Id(5);if((ScriptUpdateReference.Mode&64)!=0)return null;if((ScriptUpdateReference.Mode&32)!=0)return new(){"(1, 4): actual compiler error","another error"};return new();}
}
class SceneObjectGroup {
 public TaskInventoryItem GetInventoryItem(uint local,UUID key){ScriptUpdateReference.Step(5,"ITEM:"+local+":"+key);return (ScriptUpdateReference.Mode&4)==0?new(){ItemID=ScriptUpdateReference.Id((ScriptUpdateReference.Mode&8)!=0?7:2)}:null;}
 public void UpdateInventoryItem(TaskInventoryItem i)=>ScriptUpdateReference.Step(11,"INV:"+i.ItemID);
 public void InvalidateEffectivePerms()=>ScriptUpdateReference.Step(12,"PERMS");
 public void ResumeScripts()=>ScriptUpdateReference.Step(18,"RESUME");
}
class SceneObjectPart {public uint LocalId=123;public string Name="part";public UUID UUID=ScriptUpdateReference.Id(8);public SceneObjectGroup ParentGroup=new();public PartInventory Inventory=new();public void SendPropertiesToClient(IClientAPI c)=>ScriptUpdateReference.Step(13,"PROPS:"+c.AgentId);}
class Events {public void TriggerUpdateScript(UUID a,UUID i,UUID o,bool run,UUID asset)=>ScriptUpdateReference.Step(17,"EVENT:"+a+":"+i+":"+o+":"+(run?1:0)+":"+asset);}
partial class Scene {
 public Permissions Permissions=new();public Assets AssetService=new();public Events EventManager=new();public string DefaultScriptEngine="YEngine";Logger m_log=new();
 SceneObjectPart GetSceneObjectPart(UUID id){ScriptUpdateReference.Step(3,"PART:"+id);return (ScriptUpdateReference.Mode&2)==0?new():null;}
 bool TryGetScenePresence(UUID id,out ScenePresence presence){ScriptUpdateReference.Step(19,"CLIENT:"+id);presence=new(){ControllingClient=ScriptUpdateReference.Client()};return (ScriptUpdateReference.Mode&256)==0;}
}
class ScriptUpdateReference {
 public static int Mode,Failure;public static bool Running;public static UUID Asset;static string Trace;
 public static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));
 public static Client Client()=>new(){AgentId=Id((Mode&512)!=0?6:1)};
 public static void Step(int point,string text){Trace+="|"+text;if(Failure==point)throw new Exception();}
 static string Probe(int mode,int failure,int overload){Mode=mode;Failure=failure;Running=false;Asset=Id(9);Trace="";byte[] data=(mode&1024)!=0?null:(mode&2048)!=0?Array.Empty<byte>():new byte[]{0,1,255,42};string result;
 try{var scene=new Scene();var errors=overload!=0?scene.CapsUpdateTaskInventoryScriptAsset(Id(1),Id(2),Id(8),(mode&4096)!=0,data):scene.CapsUpdateTaskInventoryScriptAsset(Client(),Id(2),Id(8),(mode&4096)!=0,data);result=errors==null?"NULL":"OK:"+errors.Count;if(errors!=null)foreach(string e in errors)result+=":"+e;}catch{result="THROW";}
 return result+":"+(Running?1:0)+":"+Asset+Trace;}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1],a[2]));}}
}
