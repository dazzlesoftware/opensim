using System;using System.Collections.Generic;using System.Linq;using System.Text;using System.Reflection;using OpenMetaverse;using OpenSim.Framework;
interface IAssetCache{}interface IAssetService{}
class Service:IAssetService{public int Tag;public Service(int tag){Tag=tag;}}
class RegionInfo{public UUID RegionID;}
class Scene {
 public int Tag,Cache=-1;public bool Available=true;public Service Service;public RegionInfo Info;
 public RegionInfo RegionInfo{get{Probe.Add("ID:"+Tag);Probe.Fail(15);return Info;}}
 public void RegisterModuleInterface<T>(T value){var module=(FlotsamAssetCache)(object)value;Probe.Add("R:"+Tag+":"+module.Tag);Probe.Fail(1);Cache=module.Tag;}
 public void UnregisterModuleInterface<T>(T value){var module=(FlotsamAssetCache)(object)value;Probe.Add("U:"+Tag+":"+module.Tag);Probe.Fail(2);if(Cache==module.Tag)Cache=-1;}
 public T RequestModuleInterface<T>(){Probe.Add("S:"+Tag);Probe.Fail(3);return Available?(T)(object)Service:default;}
}
namespace System.Timers {
 public class ElapsedEventArgs:EventArgs{}
 public class Timer {
  public int Tag;public bool Active,Closed,Auto=true;public Action<object,ElapsedEventArgs> Handler;
  public Timer(double interval){Tag=++Probe.TimerSerial;Probe.Add("N:"+Tag+":"+BitConverter.DoubleToUInt64Bits(interval));Probe.Fail(4);var type=Type.GetType("System.Timers.Timer, System.ComponentModel.TypeConverter",true);try{using var actual=(IDisposable)Activator.CreateInstance(type,new object[]{interval});}catch(TargetInvocationException e){throw e.InnerException;}Probe.Timers.Add(this);}
  public bool AutoReset{get=>Auto;set{Probe.Add("A:"+Tag+":"+(value?1:0));Probe.Fail(5);Auto=value;}}
  public event Action<object,ElapsedEventArgs> Elapsed{add{Probe.Add("B:"+Tag);Probe.Fail(6);Handler+=value;}remove{Handler-=value;}}
  public void Start(){Probe.Add("T:"+Tag);Probe.Fail(7);Active=true;Closed=false;}
  public void Stop(){Probe.Add("P:"+Tag);Probe.Fail(8);Active=false;}
  public void Close(){Probe.Add("Z:"+Tag);Probe.Fail(9);Active=false;Closed=true;}
  public void Emit(){Probe.Add("E:"+Tag);if(Active&&!Closed){if(!Auto)Active=false;try{Handler?.Invoke(this,new());}catch{}}}
 }
}
namespace OpenSim.Framework {
 class ObjectJobEngine {
  public int Tag;public bool Disposed;
  public ObjectJobEngine(Action<object> callback,string name,int hold,int concurrency){Tag=++Probe.WorkerSerial;Probe.Add("W:"+Tag+":"+name+":"+hold+":"+concurrency);Probe.Fail(10);Probe.Workers.Add(this);}
  public void Dispose(){Probe.Add("Q:"+Tag);Probe.Fail(11);Disposed=true;}
 }
}
interface IAssetLoader{void ForEachDefaultXmlAsset(string args,Action<AssetBase> action);}
class Loader:IAssetLoader {
 public void ForEachDefaultXmlAsset(string args,Action<AssetBase> action){Probe.Add("F:"+args);Probe.Fail(13);if(Probe.LoaderMode==7){Probe.Retained=action;return;}if(Probe.LoaderMode==1)return;action(new AssetBase("a","default a",0,"creator"));if(Probe.LoaderMode==5)throw new Exception("middle");if(Probe.LoaderMode==4){action(null);return;}var a=new AssetBase(Probe.LoaderMode==3?"a":"b","default b",0,"creator");if(Probe.LoaderMode==6)a.Metadata=null;action(a);}
}
static class ServerUtils{public static T LoadPlugin<T>(string module,object[] args){Probe.Add("L:"+Convert.ToHexString(Encoding.UTF8.GetBytes(module)).ToLowerInvariant());Probe.Fail(12);return Probe.LoaderMode==0?default:(T)(object)new Loader();}}
class Logger{public void Info(string text){Probe.Add("INFO:"+text);Probe.Fail(17);}}
partial class FlotsamAssetCache:IAssetCache {
 Logger m_log=new();
 void Expire(string id){Probe.Add("EXPIRE:"+Tag+":"+id);Probe.Fail(18);}
 public void Defaults(bool remove){if(remove)HandleDeleteDefaultAssets();else HandleLoadDefaultAssets();}
 public int Tag,CacheCalls;bool m_Enabled,m_FileCacheEnabled,m_timerRunning,m_cleanupRunning;TimeSpan m_FileExpiration,m_FileExpirationCleanupTimer;IAssetService m_AssetService;string m_assetLoader,m_assetLoaderArgs="args";readonly List<Scene> m_Scenes=new();readonly object timerLock=new();System.Timers.Timer m_CacheCleanTimer;static ObjectJobEngine m_assetFileWriteWorker;static HashSet<string> m_defaultAssets=new();const string m_ModuleName="FlotsamAssetCache";
 public FlotsamAssetCache(int tag,bool enabled,bool file,int loader,long expiration,long cleanup){Tag=tag;m_Enabled=enabled;m_FileCacheEnabled=file;m_assetLoader=loader switch{0=>null,1=>"",2=>"  ",3=>"\u2003",4=>"loader",_=>"\u200b"};m_FileExpiration=TimeSpan.FromTicks(expiration);m_FileExpirationCleanupTimer=TimeSpan.FromTicks(cleanup);}
 void ProcessWrites(object o){}
 void CleanupExpiredFiles(object sender,System.Timers.ElapsedEventArgs e){Probe.Add("CLEAN:"+Tag);Probe.Fail(16);}
 void Cache(AssetBase a,bool replace){CacheCalls++;Probe.Add("CACHE:"+Tag+":"+(a==null?"-":a.ID)+":"+(replace?1:0));Probe.Fail(14);if(Probe.Mutate&&a!=null)a.ID="changed";}
 public void Cleanup(bool value){m_cleanupRunning=value;}
 public string Snapshot()=>string.Join(",",m_Scenes.Select(s=>s.Tag))+"/"+(m_AssetService==null?"-":((Service)m_AssetService).Tag.ToString())+"/"+(m_CacheCleanTimer?.Tag??0)+"/"+(m_timerRunning?1:0)+"/"+(m_cleanupRunning?1:0)+"/"+CacheCalls;
 public static void Reset(){m_assetFileWriteWorker=null;m_defaultAssets=new(){"old"};}
 public static string Shared()=>(m_assetFileWriteWorker?.Tag??0)+"/"+string.Join(",",m_defaultAssets.OrderBy(s=>s,StringComparer.Ordinal));
}
static class Probe {
 public static int TimerSerial,WorkerSerial,LoaderMode=2;public static bool Mutate;public static Action<AssetBase> Retained;public static List<System.Timers.Timer> Timers=new();public static List<ObjectJobEngine> Workers=new();static int point,nth,seen;static List<string> trace=new();static FlotsamAssetCache[] modules;static Scene[] scenes;
 public static void Add(string s)=>trace.Add(s);public static void Fail(int p){if(point==p&&++seen==nth)throw new Exception("fault");}
 static string Run(string line){var p=line.Split(' ',StringSplitOptions.RemoveEmptyEntries);int m=p.Length>1?int.Parse(p[1]):0;switch(p[0]){
 case "init":Retained=null;Timers.Clear();Workers.Clear();TimerSerial=WorkerSerial=point=seen=0;nth=1;LoaderMode=2;Mutate=false;FlotsamAssetCache.Reset();long expiration=long.Parse(p[7]),cleanup=long.Parse(p[8]);modules=new[]{new FlotsamAssetCache(0,p[1]=="1",p[2]=="1",int.Parse(p[3]),expiration,cleanup),new FlotsamAssetCache(1,p[4]=="1",p[5]=="1",int.Parse(p[6]),expiration,cleanup)};scenes=Enumerable.Range(0,3).Select(i=>new Scene{Tag=i,Service=new Service(i),Info=new RegionInfo{RegionID=new UUID(i==1?"00000000-0000-0000-0000-000000000002":"00000000-0000-0000-0000-000000000001")}}).ToArray();return "init";
 case "defaults":modules[m].Defaults(false);return "defaults";
 case "delete-defaults":modules[m].Defaults(true);return "delete-defaults";
 case "late":Retained?.Invoke(new AssetBase("late","late",0,""));return "late";
 case "fail":point=m;nth=int.Parse(p[2]);seen=0;return "fail";
 case "loader":LoaderMode=m;return "loader";
 case "mutate":Mutate=m!=0;return "mutate";
 case "service":scenes[m].Available=p[2]=="1";return "service";
 case "cleanup":modules[m].Cleanup(p[2]=="1");return "cleanup";
 case "post":modules[m].PostInitialise();return "post";
 case "meta":return modules[m].Name+":"+(modules[m].ReplaceableInterface==null?1:0);
 case "close":modules[m].Close();return "close";
 case "emit":if(m>=0&&m<Timers.Count)Timers[m].Emit();return "emit";
 case "add":case "remove":case "load":int s=int.Parse(p[2]);Scene scene=s<0?null:scenes[s];if(p[0]=="add")modules[m].AddRegion(scene);else if(p[0]=="remove")modules[m].RemoveRegion(scene);else modules[m].RegionLoaded(scene);return p[0];
 }throw new Exception(line);}
 [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]static WeakReference CreateRetained(){LoaderMode=7;FlotsamAssetCache.Reset();var m=new FlotsamAssetCache(0,true,false,4,0,1);var s=new Scene{Tag=0,Service=new Service(0),Info=new RegionInfo{RegionID=UUID.Zero}};m.AddRegion(s);m.RegionLoaded(s);m.RemoveRegion(s);m.Close();return new WeakReference(m);}
 [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]static bool Alive(WeakReference weak)=>weak.IsAlive;
 [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]static void InvokeRetained()=>Retained(new AssetBase("late","late",0,"creator"));
 static void Collect(){GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();}
 static void Main(string[] args){if(args.Length>0){var weak=CreateRetained();Collect();bool before=Alive(weak);InvokeRetained();bool added=FlotsamAssetCache.Shared()=="0/late";Retained=null;Collect();Console.WriteLine((before?1:0)+":"+(added?1:0)+":"+(Alive(weak)?1:0));return;}string line;while((line=Console.ReadLine())!=null){trace.Clear();string result;try{result=Run(line);}catch{result="EX";}Console.WriteLine(result+"|"+string.Join(";",trace)+"|"+string.Join(";",modules.Select(m=>m.Snapshot()))+"|"+FlotsamAssetCache.Shared()+"|"+string.Join(",",scenes.Select(s=>s.Cache))+"|"+string.Join(",",Timers.Select(t=>t.Tag+":"+(t.Active?1:0)+(t.Closed?1:0)+(t.Auto?1:0)+(t.Handler!=null?1:0)))+"|"+string.Join(",",Workers.Select(w=>w.Tag+":"+(w.Disposed?1:0))));}}
}
