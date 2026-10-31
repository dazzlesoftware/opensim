using System;using System.IO;using System.Threading;using System.Collections.Generic;using System.Linq;using System.Runtime.InteropServices;using System.Runtime.CompilerServices;using OpenSim.Framework;using OpenMetaverse;
delegate void AssetRetrieved(string id,object sender,AssetBase asset);
delegate void SimpleAssetRetrieved(AssetBase asset);
namespace System.Threading {
 public sealed class Timer:IDisposable {
  static readonly List<Timer> all=new();TimerCallback cb;object state;double due;bool active;
  public Timer(TimerCallback callback,object value,int ms,int period){cb=callback;state=value;Change(ms,period);all.Add(this);}
  public bool Change(int ms,int period){active=ms!=-1;due=Probe.Now+ms;return true;}
  public void Dispose(){active=false;cb=null;state=null;}
  public static void Reset(){foreach(var t in all)t.Dispose();all.Clear();}
  public static void Pump(){foreach(var t in all.ToArray())if(t.active&&t.due<=Probe.Now){t.active=false;var cb=t.cb;cb?.Invoke(t.state);}}
 }
}
namespace OpenSim.Framework{static class Util{public static double GetTimeStampMS()=>Probe.Now;public const string UUIDZeroString="00000000-0000-0000-0000-000000000000";}}
class Logger{public void Debug(string s){Probe.Add("L:"+s);Probe.Fail(10);}public void Warn(string s){Probe.Add("W:"+s);Probe.Fail(11);}}
partial class FlotsamAssetCache {
 bool m_FileCacheEnabled,m_MemoryCacheEnabled,m_negativeCacheEnabled;double m_MemoryExpiration;int m_negativeExpiration,m_LogLevel;ulong m_Requests,m_MemoryHits,m_weakRefHits;string m_CacheDirectory="cache";static bool m_updateFileTimeOnCacheHit;
 ExpiringCacheOS<string,AssetBase> m_MemoryCache;ExpiringKey<string> m_negativeCache=new(2000);Dictionary<string,WeakReference> weakAssetReferences=new();object weakAssetReferencesLock=new();Logger m_log=new();
 public FlotsamAssetCache(bool memory,bool negative,bool file,bool touch,double seconds,int negms,int log){m_MemoryCacheEnabled=memory;m_negativeCacheEnabled=negative;m_FileCacheEnabled=file;m_updateFileTimeOnCacheHit=touch;m_MemoryExpiration=seconds;m_negativeExpiration=negms;m_LogLevel=log;if(memory)m_MemoryCache=new((int)seconds*500);}
 void UpdateFileCache(string k,AssetBase a,bool replace){Probe.Add("F:"+Probe.Label(k)+":"+(replace?1:0));Probe.Fail(1);Probe.Disk[k]=Probe.Clone(a);if(Probe.Rename)a.ID=Probe.Key(6);}
 AssetBase GetFromFileCache(string k){Probe.Add("G:"+Probe.Label(k));Probe.Fail(3);return Probe.Disk.TryGetValue(k,out var a)?Probe.Clone(a):null;}
 bool CheckFromFileCache(string k){Probe.Add("H:"+Probe.Label(k));try{Probe.Fail(4);return Probe.Disk.ContainsKey(k);}catch{return false;}}
 string GetFileName(string k){Probe.Add("P:"+Probe.Label(k));Probe.Fail(2);return k;}
 static void UpdateFileLastAccessTime(string k){Probe.Add("T:"+Probe.Label(k));}
 public string Stats()=>m_Requests+","+m_MemoryHits+","+m_weakRefHits;
}
static class FakeFile{public static void Delete(string k){Probe.Add("X:"+Probe.Label(k));Probe.Fail(5);Probe.Disk.Remove(k);}}
static class FakeDirectory {
 public static bool Exists(string root){Probe.Add("E");Probe.Fail(6);return Probe.RootExists;}
 public static string[] GetDirectories(string root){Probe.Add("D");Probe.Fail(7);return new[]{System.IO.Path.Combine(root,"a"),System.IO.Path.Combine(root,"b")};}
 public static void Delete(string dir,bool recursive){var name=System.IO.Path.GetFileName(dir);Probe.Add("R:"+name);Probe.Fail(name=="a"?8:9);foreach(var k in Probe.Disk.Keys.ToArray())if(Probe.Group(k)==name)Probe.Disk.Remove(k);}
}
static class Probe {
 public static double Now;public static bool Rename,RootExists=true;public static Dictionary<string,AssetBase> Disk=new();static Dictionary<int,AssetBase> held=new();static FlotsamAssetCache cache;static int point,serial;static List<string> trace=new();
 public static string Key(int n)=>n==7?Util.UUIDZeroString:"key"+n;
 public static string Label(string k)=>UUID.TryParse(k,out var id)&&!id.IsZero()?"R":k;
 public static string Group(string k)=>k==Key(7)?"root":k==Key(0)||k==Key(2)||k==Key(4)||k==Key(6)?"a":"b";
 public static void Add(string s)=>trace.Add(s);
 public static void Fail(int p){if(point==p||(point==11&&p==5))throw new InvalidOperationException("fault");}
 public static AssetBase Clone(AssetBase a)=>new(a.ID,a.Name,a.Type,a.CreatorID){Data=a.Data==null?null:(byte[])a.Data.Clone()};
 static string Bytes(byte[] data)=>data==null?"-":Convert.ToHexString(data).ToLowerInvariant();
 static string Show(AssetBase a)=>a==null?"-":Label(a.ID)+":"+a.Name+":"+Bytes(a.Data);
 static AssetBase New(int k){var a=new AssetBase(Key(k),"asset"+(++serial),0,"creator"){Data=new byte[]{(byte)serial,0,255}};return a;}
 [MethodImpl(MethodImplOptions.NoInlining)]static string Run(string line){var p=line.Split(' ',StringSplitOptions.RemoveEmptyEntries);AssetBase a=null;int k=p.Length>1&&p[0]!="init"&&p[0]!="advance"&&p[0]!="jump"?int.Parse(p[1]):0;switch(p[0]){
 case "init":System.Threading.Timer.Reset();Now=double.Parse(p[8],System.Globalization.CultureInfo.InvariantCulture);serial=point=0;held.Clear();Disk.Clear();Rename=false;RootExists=true;cache=new(p[1]=="1",p[2]=="1",p[3]=="1",p[4]=="1",double.Parse(p[5],System.Globalization.CultureInfo.InvariantCulture),int.Parse(p[6]),int.Parse(p[7]));return "init";
 case "fault":point=k;return "fault";
 case "rename":Rename=k!=0;return "rename";
 case "root":RootExists=k!=0;return "root";
 case "seed":Disk[Key(k)]=New(k);return "seed";
 case "cache":case "replace":a=New(k);held[k]=a;cache.Cache(a,p[0]=="replace");return "cache";
 case "null":cache.Cache(null);return "null";
 case "drop":held.Remove(k);return "drop";
 case "mutate":if(held.TryGetValue(k,out a)){a.Name+="X";if(a.Data?.Length>0)a.Data[0]++;}return "mutate";
 case "negative":cache.CacheNegative(Key(k));return "negative";
 case "get":case "from":try{return ((p[0]=="get"?cache.Get(Key(k),out a):cache.GetFromMemory(Key(k),out a))?"1:":"0:")+Show(a);}catch{return "EX:"+Show(a);}
 case "asset":return Show(cache.Get(Key(k)));
 case "cached":return Show(cache.GetCached(Key(k)));
 case "check":return cache.Check(Key(k))?"1":"0";
 case "meta":return cache.GetMetadata(Key(k))?.Name??"-";
 case "meta-mutate":var m=cache.GetMetadata(Key(k));if(m!=null)m.Name+="M";return m?.Name??"-";
 case "data":return Bytes(cache.GetData(Key(k)));
 case "data-mutate":var d=cache.GetData(Key(k));if(d?.Length>0)d[0]++;return Bytes(d);
 case "async":case "async-null":AssetRetrieved callback=p[0]=="async-null"?null:(id,sender,asset)=>{Add("C:"+Label(id)+":"+sender+":"+Show(asset));Fail(12);};return cache.Get(Key(k),63,callback)?"1":"0";
 case "foreign":return Show(cache.Get(Key(k),"foreign",true));
 case "foreign-async":cache.Get(Key(k),"foreign",true,asset=>Add("BAD"));return "foreign";
 case "exists":return string.Concat(cache.AssetsExist(new[]{Key(k),Key(7),Key(0)}).Select(v=>v?"1":"0"));
 case "store":if(!held.TryGetValue(k,out a)){a=New(k);held[k]=a;}var stored=cache.Store(a);return "store:"+(stored==a.ID?"1":"0")+":"+(!a.FullID.IsZero()?"1":"0");
 case "store-null":cache.Store(null);return "unexpected";
 case "update":case "update-null":return cache.UpdateContent(Key(k),p[0]=="update"?new byte[]{9,8,7}:null)?"1":"0";
 case "delete":return cache.Delete(Key(k))?"1":"0";
 case "expire":cache.Expire(Key(k));return "expire";
 case "clear":cache.Clear();return "clear";
 case "advance":Now+=double.Parse(p[1],System.Globalization.CultureInfo.InvariantCulture);System.Threading.Timer.Pump();return "advance";
 case "jump":Now+=double.Parse(p[1],System.Globalization.CultureInfo.InvariantCulture);return "jump";
 }throw new Exception(line);}
 static void Main(){string line;while((line=Console.ReadLine())!=null){trace.Clear();string result;try{result=Run(line);}catch{result="EX";}GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();Console.WriteLine(result+"|"+string.Join(";",trace)+"|"+cache.Stats()+"|"+string.Join(",",Disk.Values.Select(Show).OrderBy(s=>s,StringComparer.Ordinal)));}}
}
