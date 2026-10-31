using System;using System.Collections.Generic;using OpenMetaverse;
interface IClientAPI {UUID AgentId{get;}}
class Client:IClientAPI {public UUID AgentId=>ScriptControlReference.Id(1);}
class SceneObjectPart {public uint LocalId=123;}
class Permissions {
 public bool CanEditScript(UUID i,UUID o,UUID a){ScriptControlReference.Step(1,"EDIT:"+i+":"+o+":"+a);return (ScriptControlReference.Mode&1)==0;}
 public bool CanResetScript(UUID o,UUID i,UUID a){ScriptControlReference.Step(3,"RESETCHECK:"+o+":"+i+":"+a);return (ScriptControlReference.Mode&4)==0;}
}
class Events {
 public void TriggerStartScript(uint local,UUID i){ScriptControlReference.Step(4,"START:"+local+":"+i);ScriptControlReference.Engine.OnStartScript(local,i);}
 public void TriggerStopScript(uint local,UUID i){ScriptControlReference.Step(5,"STOP:"+local+":"+i);ScriptControlReference.Engine.OnStopScript(local,i);}
 public void TriggerScriptReset(uint local,UUID i){ScriptControlReference.Step(6,"RESET:"+local+":"+i);ScriptControlReference.Engine.OnScriptReset(local,i);}
}
interface IUrlModule {void ScriptRemoved(UUID i);}
class Urls:IUrlModule {public void ScriptRemoved(UUID i)=>ScriptControlReference.Step(12,"URL_REMOVE:"+i);}
partial class Scene {
 public Permissions Permissions=new();public Events EventManager=new();
 SceneObjectPart GetSceneObjectPart(UUID i){ScriptControlReference.Step(2,"PART:"+i);return (ScriptControlReference.Mode&2)!=0?null:new();}
 public T RequestModuleInterface<T>()where T:class{ScriptControlReference.Step(11,"URL_MODULE");return (ScriptControlReference.Mode&32)!=0?null:new Urls() as T;}
}
class Item {public bool ScriptRunning{set{ScriptControlReference.Step(9,"ITEM:"+(value?1:0));ScriptControlReference.ItemRunning=value;}}}
class ControlInstance {public Item m_Item=(ScriptControlReference.Mode&16)!=0?null:new();public bool Running{set{ScriptControlReference.Step(8,"RUN:"+(value?1:0));ScriptControlReference.Running=value;}}public void Reset()=>ScriptControlReference.Step(13,"INSTANCE_RESET");}
partial class Engine {
 Scene m_Scene=new();bool TryGetInstance(UUID i,out ControlInstance value){ScriptControlReference.Step(7,"FIND:"+i);value=new();return (ScriptControlReference.Mode&8)==0;}
 void TraceCalls(string fmt,string local,string item)=>ScriptControlReference.Step(10,"TRACE:"+local+":"+item);
}
enum ScriptEventCode {None,state_entry,touch}
class EventParams {public string EventName;}
class ExecQueues {
 public void QueueToYield(ExecutionInstance i)=>ExecutionInstance.Step(1,"YIELD");
 public void QueueToStart(ExecutionInstance i)=>ExecutionInstance.Step(2,"START");
 public void RemoveFromSleep(ExecutionInstance i)=>ExecutionInstance.Step(3,"REMOVE_SLEEP");
}
partial class ExecutionInstance {
 public bool m_Running=true;public XMRInstState m_IState;public int m_SuspendCount;public ScriptEventCode eventCode;public LinkedList<EventParams> m_EventQueue=new();object m_QueueLock=new();ExecQueues m_Engine=new();
 Dictionary<string,ScriptEventCode> m_eventCodeMap=new(){{"state_entry",ScriptEventCode.state_entry},{"touch",ScriptEventCode.touch}};public bool[] m_HaveEventHandlers={false,true,true};
 public void EmptyEventQueues(){Step(4,"EMPTY");m_EventQueue.Clear();}
 public static int Failure;public static string Trace;
 public static void Step(int point,string text){Trace+="|"+text;if(point==Failure)throw new Exception();}
}
class ScriptControlReference {
 public static int Mode,Failure;public static bool Running,ItemRunning;static string Trace;public static Engine Engine;
 public static UUID Id(int n)=>new(n==1?"11223344-5566-7788-99aa-000000000001":"11223344-5566-7788-99aa-000000000002");
 public static void Step(int point,string text){Trace+="|"+text;if(point==Failure)throw new Exception();}
 static string Probe(int command,int mode,int fail){Mode=mode;Failure=fail;Running=ItemRunning=false;Trace="";Engine=new();var scene=new Scene();string result="OK";try{switch(command){case 0:scene.SetScriptRunning(new Client(),Id(2),Id(1),false);break;case 1:scene.SetScriptRunning(new Client(),Id(2),Id(1),true);break;case 2:scene.ProcessScriptReset(new Client(),Id(2),Id(1));break;case 3:Engine.OnStartScript(999,Id(1));break;case 4:Engine.OnStopScript(999,Id(1));break;case 5:Engine.OnScriptReset(999,Id(1));break;case 6:Engine.ResetScript(Id(1));break;}}catch{result="THROW";}return result+":"+(Running?1:0)+":"+(ItemRunning?1:0)+Trace;}
 static string Execution(int s,int r,int a,int u,int n,int f){var i=new ExecutionInstance{m_IState=(XMRInstState)s,eventCode=a!=0?ScriptEventCode.touch:ScriptEventCode.None,m_SuspendCount=u};for(int j=0;j<n;++j)i.m_EventQueue.AddLast(new EventParams());ExecutionInstance.Failure=f;ExecutionInstance.Trace="";string result="OK";try{i.Running=r!=0;}catch{result="THROW";}return result+":"+(int)i.m_IState+":"+(i.m_Running?1:0)+":"+i.m_EventQueue.Count+ExecutionInstance.Trace;}
 static string Gate(int s,int r,int k,int h,int e,int n){var i=new ExecutionInstance{m_IState=(XMRInstState)s,m_Running=r!=0};i.m_HaveEventHandlers[1]=i.m_HaveEventHandlers[2]=h!=0;for(int j=0;j<n;++j)i.m_EventQueue.AddLast(new EventParams());var result=i.PostGate(new(){EventName=k==0?"unknown":e!=0?"state_entry":"touch"});return !result.HasValue?"1":!result.Value?"0":i.m_EventQueue.Count>n?"3":"2";}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(a[0]==0?Probe(a[1],a[2],a[3]):a[0]==1?Execution(a[1],a[2],a[3],a[4],a[5],a[6]):Gate(a[1],a[2],a[3],a[4],a[5],a[6]));}}
}
