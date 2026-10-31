using System;using System.IO;using System.Threading;using System.Collections.Generic;using OpenSim.Framework;
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
namespace OpenSim.Framework{static class Util{public static double GetTimeStampMS()=>Probe.Now;}}
class Entries {
 public ExpiringKey<string> Values=new(300000);
 public bool ContainsKey(string s){Probe.Trace+="C";Probe.Fail(1);return Values.ContainsKey(s);}
 public void Add(string s,int ms){Probe.Trace+="A"+ms+",";Probe.Fail(3);Values.Add(s,ms);}
}
static class FakeFile{public static bool Real;public static void SetLastAccessTime(string s,DateTime time){if(Real){System.IO.File.SetLastAccessTime(s,time);return;}Probe.Trace+="S";Probe.Fail(2);}}
partial class FlotsamAssetCache {
 static Entries m_lastFileAccessTimeChange;
 public static void Reset(bool enabled){m_lastFileAccessTimeChange=enabled?new():null;}
 public static bool Check(string s)=>CheckUpdateFileLastAccessTime(s);
 public static void Touch(string s)=>UpdateFileLastAccessTime(s);
 public static void Note(string s,int ms)=>m_lastFileAccessTimeChange?.Add(s,ms);
 public static string Bits(){if(m_lastFileAccessTimeChange==null)return "---";string s="";for(int i=0;i<3;i++)s+=m_lastFileAccessTimeChange.Values.ContainsKey(i.ToString())?"1":"0";return s;}
}
static class Probe {
 public static double Now;public static string Trace="";static int point,kind;
 public static void Fail(int p){if(point!=p||kind==0)return;throw kind switch{1=>new FileNotFoundException(),2=>new DirectoryNotFoundException(),3=>new UnauthorizedAccessException(),4=>new IOException(),_=>new InvalidOperationException()};}
 static string Run(string line){var p=line.Split(' ',StringSplitOptions.RemoveEmptyEntries);Trace="";string result="OK";switch(p[0]){
 case "init":System.Threading.Timer.Reset();Now=double.Parse(p[2],System.Globalization.CultureInfo.InvariantCulture);point=kind=0;FlotsamAssetCache.Reset(p[1]=="1");break;
 case "failure":point=int.Parse(p[1]);kind=int.Parse(p[2]);break;
 case "touch":FlotsamAssetCache.Touch(p[1]);break;
 case "check":result=FlotsamAssetCache.Check(p[1])?"1":"0";break;
 case "note":FlotsamAssetCache.Note(p[1],int.Parse(p[2]));break;
 case "jump":Now+=double.Parse(p[1],System.Globalization.CultureInfo.InvariantCulture);break;
 case "advance":Now+=double.Parse(p[1],System.Globalization.CultureInfo.InvariantCulture);System.Threading.Timer.Pump();break;
 case "pump":System.Threading.Timer.Pump();break;
 }return result;}
 static void Main(string[] args){if(args.Length>0){FakeFile.Real=true;var path=System.Text.Encoding.UTF8.GetString(Convert.FromHexString(args[1]));FlotsamAssetCache.Reset(args[3]=="1");if(args[2]=="check")Console.WriteLine(FlotsamAssetCache.Check(path)?"1":"0");else{FlotsamAssetCache.Touch(path);Console.WriteLine("OK");}return;}string s;FlotsamAssetCache.Reset(true);while((s=Console.ReadLine())!=null){string result;try{result=Run(s);}catch{result="EX";}Console.WriteLine(result+"|"+Trace+"|"+FlotsamAssetCache.Bits());}}
}
