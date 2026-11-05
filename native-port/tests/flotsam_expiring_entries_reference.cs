using System;using System.Collections.Generic;using System.Globalization;using System.Linq;using System.Reflection;using System.Runtime.CompilerServices;using System.Threading;using OpenSim.Framework;
#if !REAL_TIMERS
namespace System.Threading {
 public sealed class Timer:IDisposable {
  static readonly List<Timer> all=new();TimerCallback callback;object state;public double Due;public bool Active;
  public Timer(TimerCallback callback,object state,int due,int period){this.callback=callback;this.state=state;Change(due,period);all.Add(this);}
  public bool Change(int due,int period){Active=due!=-1;Due=Probe.Now+due;return true;}
  public void Dispose(){Active=false;callback=null;state=null;}
  public static void Reset(){foreach(var timer in all)timer.Dispose();all.Clear();}
  public static void Pump(){foreach(var timer in all.ToArray())if(timer.Active&&timer.Due<=Probe.Now){timer.Active=false;var callback=timer.callback;callback?.Invoke(timer.state);}}
 }
}
#endif
namespace OpenSim.Framework {static class Util {
 public static double GetTimeStampMS(){
#if REAL_TIMERS
  return System.Diagnostics.Stopwatch.GetTimestamp()*(1000.0/System.Diagnostics.Stopwatch.Frequency);
#else
  double value=Probe.Now;Probe.Now+=Probe.Step;return value;
#endif
 }
}}
#if !REAL_TIMERS
static class Probe {
 public static double Now,Step;static ExpiringKey<string> keys;static ExpiringCacheOS<string,AssetBase> assets;static Dictionary<int,AssetBase> held=new();static Dictionary<int,WeakReference> weak=new();
 static string Bits(double n)=>BitConverter.DoubleToUInt64Bits(n).ToString();
 static AssetBase Make(int n){var asset=new AssetBase("asset"+n,"asset"+n,0,"creator");asset.Data=new byte[]{unchecked((byte)n)};return asset;}
 static string Show(AssetBase asset)=>asset?.Name??"-";
 static object Field(object value,string name)=>value.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(value);
 static string Due(object value){var timer=(System.Threading.Timer)Field(value,"m_purgeTimer");return timer!=null&&timer.Active?Bits(timer.Due):"-";}
 [MethodImpl(MethodImplOptions.NoInlining)]static string Run(string line){var p=line.Split(' ');string op=p[0],result;AssetBase got=held.TryGetValue(999,out var sentinel)?sentinel:null;
  try{
   switch(op){
   case "init":System.Threading.Timer.Reset();held.Clear();weak.Clear();Now=BitConverter.UInt64BitsToDouble(ulong.Parse(p[3],NumberStyles.HexNumber));Step=BitConverter.UInt64BitsToDouble(ulong.Parse(p[4],NumberStyles.HexNumber));keys=new(int.Parse(p[1]));assets=new(int.Parse(p[2]));Step=0;held[999]=Make(999);result=op;break;
   case "jump":case "advance":Now+=double.Parse(p[1],CultureInfo.InvariantCulture);if(op=="advance")System.Threading.Timer.Pump();result=op;break;
   case "pump":System.Threading.Timer.Pump();result=op;break;
   case "newk":keys=new(int.Parse(p[1]));result=op;break;
   case "newa":assets=new(int.Parse(p[1]));result=op;break;
   case "keyclear":keys.Clear();result=op;break;
   case "assetclear":assets.Clear();result=op;break;
   case "keydispose":keys.Dispose();result=op;break;
   case "assetdispose":assets.Dispose();result=op;break;
   default:int id=int.Parse(p[1]);string name="key"+id;
    switch(op){
     case "keyadd":keys.Add(name,int.Parse(p[2]));result=op;break;
     case "assetadd":int tag=int.Parse(p[3]);AssetBase asset=null;if(tag>=0){if(!held.ContainsKey(tag))held[tag]=Make(tag);asset=held[tag];weak[tag]=new(asset);}assets.Add(name,asset,int.Parse(p[2]));result=op;break;
     case "keyget":result=keys.ContainsKey(name)?"1":"0";break;
     case "assetget":result=(assets.TryGetValue(name,out got)?"1:":"0:")+Show(got);break;
     case "keyremove":result=keys.Remove(name)?"1":"0";break;
     case "assetremove":result=assets.Remove(name)?"1":"0";break;
     case "drop":held.Remove(id);result=op;break;
     case "alive":result=weak.TryGetValue(id,out var item)&&item.IsAlive?"1":"0";break;
     case "mutate":if(held.TryGetValue(id,out var heldAsset))heldAsset.Name+="X";result=op;break;
     default:throw new Exception(op);
    }break;
   }
  }catch(NullReferenceException e){result="EX:NullReferenceException:"+e.Message+":OUT="+Show(got);}
  return result+"|"+keys.Count+","+assets.Count+"|"+Due(keys)+","+Due(assets)+"|"+Bits((double)Field(keys,"m_startTS"))+","+Field(keys,"m_expire")+","+Bits((double)Field(assets,"m_startTS"))+","+Field(assets,"m_expire")+"|"+Bits(Now);
 }
 public static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;Console.OutputEncoding=new System.Text.UTF8Encoding(false);string line;while((line=Console.ReadLine())!=null){var result=Run(line);GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();Console.WriteLine(result);}}
}
#else
static class Retained {
 [MethodImpl(MethodImplOptions.NoInlining)]static WeakReference Create(){var entries=new ExpiringCacheOS<string,AssetBase>(500);var asset=new AssetBase("retained","retained",0,"");var weak=new WeakReference(asset);entries.Add("retained",asset,750);return weak;}
 [MethodImpl(MethodImplOptions.NoInlining)]static bool Alive(WeakReference value)=>value.IsAlive;
 static void Collect(){GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();}
 static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;var weak=Create();Collect();bool early=Alive(weak);var clock=System.Diagnostics.Stopwatch.StartNew();while(Alive(weak)&&clock.Elapsed.TotalSeconds<6){Thread.Sleep(5);Collect();}bool released=!Alive(weak);var entries=new ExpiringCacheOS<string,AssetBase>(500);var asset=new AssetBase("disposed","disposed",0,"");var value=new WeakReference(asset);entries.Add("disposed",asset,0);asset=null;entries.Dispose();Collect();bool kept=entries.Count==1&&Alive(value);AssetBase output=new("sentinel","sentinel",0,"");bool error=false;try{entries.TryGetValue("disposed",out output);}catch(NullReferenceException){error=output.Name=="sentinel";}GC.KeepAlive(entries);Console.WriteLine((early?1:0)+":"+(released?1:0)+":"+(kept?1:0)+":"+(error?1:0));}
}
#endif
