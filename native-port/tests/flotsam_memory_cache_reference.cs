using System;using System.IO;using System.Threading;using System.Collections.Generic;using System.Runtime.InteropServices;using System.Runtime.CompilerServices;using OpenSim.Framework;
namespace System.Threading {
 public sealed class Timer:IDisposable {
  static readonly List<Timer> all=new();TimerCallback cb;object state;long due;bool active;
  public Timer(TimerCallback callback,object value,int ms,int period){cb=callback;state=value;Change(ms,period);all.Add(this);}
  public bool Change(int ms,int period){active=ms!=-1;due=Probe.Now+ms;return true;}
  public void Dispose(){active=false;cb=null;state=null;}
  public static void Reset(){foreach(var t in all)t.Dispose();all.Clear();}
  public static void Pump(){foreach(var t in all.ToArray())if(t.active&&t.due<=Probe.Now){t.active=false;var cb=t.cb;cb?.Invoke(t.state);}}
 }
}
namespace OpenSim.Framework{static class Util{public static double GetTimeStampMS()=>Probe.Now;public const string UUIDZeroString="00000000-0000-0000-0000-000000000000";}}
class Logger{public void Debug(string s){}public void Warn(string s){}}
partial class FlotsamAssetCache {
 bool m_FileCacheEnabled=false,m_MemoryCacheEnabled,m_negativeCacheEnabled;double m_MemoryExpiration;int m_negativeExpiration,m_LogLevel=0;ulong m_Requests,m_MemoryHits,m_weakRefHits,m_DiskHits,m_RequestsForInprogress;string m_CacheDirectory="";static bool m_updateFileTimeOnCacheHit=false;Logger m_log=new();
 ExpiringCacheOS<string,AssetBase> m_MemoryCache;ExpiringKey<string> m_negativeCache=new(2000);Dictionary<string,WeakReference> weakAssetReferences=new();object weakAssetReferencesLock=new();
 public FlotsamAssetCache(bool memory,bool negative,double seconds,int negativeMs){m_MemoryCacheEnabled=memory;m_negativeCacheEnabled=negative;m_MemoryExpiration=seconds;m_negativeExpiration=negativeMs;if(memory)m_MemoryCache=new((int)seconds*500);}
 void UpdateFileCache(string k,AssetBase a,bool r){throw new Exception("Unselected file cache");}AssetBase GetFromFileCache(string k){throw new Exception("Unselected file cache");}bool CheckFromFileCache(string k){throw new Exception("Unselected file cache");}string GetFileName(string k)=>k;static void UpdateFileLastAccessTime(string p){}
 public static string Report(string line){var p=line.Split(' ');var c=new FlotsamAssetCache(p[7]=="1",false,0,0);c.m_Requests=ulong.Parse(p[0]);c.m_MemoryHits=ulong.Parse(p[1]);c.m_weakRefHits=ulong.Parse(p[2]);c.m_DiskHits=ulong.Parse(p[3]);c.m_RequestsForInprogress=ulong.Parse(p[4]);int count=int.Parse(p[5]),alive=int.Parse(p[6]);var keep=new List<object>();for(int i=0;i<count;++i){object value=i<alive?new object():null;if(value!=null)keep.Add(value);c.weakAssetReferences.Add(i.ToString(),new WeakReference(value));}var result=string.Join(";",c.GenerateCacheHitReport());GC.KeepAlive(keep);return result;}
 public void ClearMemory(){if(m_MemoryCacheEnabled)m_MemoryCache.Clear();}
 public void ClearNegatives(){if(m_negativeCacheEnabled)m_negativeCache.Clear();}
 public string ReportCurrent()=>string.Join(";",GenerateCacheHitReport());
 public string Stats()=>m_Requests+","+m_MemoryHits+","+m_weakRefHits;
}
static class Probe{
 public static long Now;static FlotsamAssetCache cache;static Dictionary<int,AssetBase> held=new();static int serial;static string Key(int k)=>k==7?Util.UUIDZeroString:"key"+k;
 static string Show(AssetBase a)=>a==null?"-":a.Name+":"+Convert.ToHexString(a.Data).ToLowerInvariant();
 [MethodImpl(MethodImplOptions.NoInlining)]static string Run(string s){var p=s.Split(' ');int k=p.Length>1&&p[0]!="init"&&p[0]!="advance"?int.Parse(p[1]):0;AssetBase a;switch(p[0]){
 case "init":System.Threading.Timer.Reset();Now=0;serial=0;held.Clear();cache=new(p[1]=="1",p[2]=="1",double.Parse(p[3],System.Globalization.CultureInfo.InvariantCulture),int.Parse(p[4]));return "init";
 case "cache":a=new AssetBase(Key(k),"asset"+(++serial),0,"creator");a.Data=new byte[]{(byte)serial,0,255};held[k]=a;cache.Cache(a);return "cache";
 case "null":cache.Cache(null);return "null";
 case "drop":held.Remove(k);return "drop";
 case "mutate":if(held.TryGetValue(k,out a)){a.Name+="X";a.Data[0]++;}return "mutate";
 case "negative":cache.CacheNegative(Key(k));return "negative";
 case "get":return (cache.Get(Key(k),out a)?"1":"0")+":"+Show(a);
 case "from":return (cache.GetFromMemory(Key(k),out a)?"1":"0")+":"+Show(a);
 case "cached":return Show(cache.GetCached(Key(k)));
 case "check":return cache.Check(Key(k))?"1":"0";
 case "expire":cache.Expire(Key(k));return "expire";
 case "memoryclear":cache.ClearMemory();return "memoryclear";
 case "negativeclear":cache.ClearNegatives();return "negativeclear";
 case "report":return cache.ReportCurrent();
 case "clear":cache.Clear();return "clear";
 case "advance":Now+=long.Parse(p[1]);System.Threading.Timer.Pump();return "advance";
 }throw new Exception(s);}
 static void Main(string[] args){Console.OutputEncoding=new System.Text.UTF8Encoding(false);System.Globalization.CultureInfo.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;System.Globalization.CultureInfo.CurrentUICulture=System.Globalization.CultureInfo.InvariantCulture;if(args.Length>0){string line;while((line=Console.ReadLine())!=null)Console.WriteLine(FlotsamAssetCache.Report(line));return;}string s;while((s=Console.ReadLine())!=null){var result=Run(s);GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();Console.WriteLine(result+"|"+cache.Stats());}}
}
