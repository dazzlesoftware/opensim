using System;using System.IO;using System.Collections.Generic;using System.Linq;using System.Globalization;using System.Text;using OpenMetaverse;
struct ProbeDate {
 public System.DateTime Value;public ProbeDate(System.DateTime d){Value=d;}
 public static ProbeDate Now{get{Probe.Add("N");Probe.Hook(16);return new(new System.DateTime(Probe.NowTicks,Probe.Kind));}}
 public static ProbeDate operator -(ProbeDate d,TimeSpan t)=>new(d.Value-t);
 public static bool operator <(ProbeDate a,ProbeDate b)=>a.Value<b.Value;public static bool operator >(ProbeDate a,ProbeDate b)=>a.Value>b.Value;
 public override string ToString()=>Value.ToString(CultureInfo.InvariantCulture);
}
class WeakMap {
 public HashSet<string> Keys=new();
 public bool Remove(string id){Probe.Add("K:"+id);Probe.Hook(7);return Keys.Remove(id);}
 public static implicit operator WeakMap(Dictionary<string,WeakReference> value){Probe.Add("RESET");Probe.Hook(14);return new();}
}
class Logger{public void Info(string s){Probe.Add("I:"+s);Probe.Hook(11);}public void Warn(string s){Probe.Add("W:"+s);Probe.Hook(9);}}
class CleanupTimer{public void Start(){Probe.Add("T");Probe.Hook(15);Probe.Starts++;}}
static class FakeGC{public static long GetTotalMemory(bool force){int i=Probe.HeapCalls++;Probe.Add("H");Probe.Hook(12);return i%2==0?10000000:unchecked(10000000+Probe.HeapDelta);}}
static class FakeThread{public static void Sleep(int ms){Probe.Add("S:"+ms);Probe.Hook(10);}}
static class FakePath{public static string GetFileName(string path){Probe.Add("NAME:"+path);Probe.Hook(4);return System.IO.Path.GetFileName(path);}}
static class FakeDirectory {
 public static bool Exists(string path){Probe.Add("EXISTS:"+path);Probe.Hook(1);return Probe.Dirs.Contains(path);}
 public static string[] GetDirectories(string path){Probe.Add("D:"+path);Probe.Hook(2);if(Probe.Real)return System.IO.Directory.GetDirectories(path);if(!Probe.Dirs.Contains(path))throw new DirectoryNotFoundException("missing");return Probe.Children.TryGetValue(path,out var children)?children.Where(Probe.Dirs.Contains).ToArray():Array.Empty<string>();}
 public static string[] GetFiles(string path){Probe.Add("F:"+path);Probe.Hook(3);if(Probe.Real)return System.IO.Directory.GetFiles(path);if(!Probe.Dirs.Contains(path))throw new DirectoryNotFoundException("missing");return Probe.DirFiles.TryGetValue(path,out var files)?files.Where(Probe.Files.Contains).ToArray():Array.Empty<string>();}
 public static void Delete(string path){Probe.Add("R:"+path);Probe.Hook(8);if(Probe.Real){System.IO.Directory.Delete(path);return;}if(!Probe.Dirs.Contains(path))throw new DirectoryNotFoundException("missing");if((Probe.Children.TryGetValue(path,out var c)&&c.Any(Probe.Dirs.Contains))||(Probe.DirFiles.TryGetValue(path,out var f)&&f.Any(Probe.Files.Contains)))throw new IOException("not empty");Probe.Dirs.Remove(path);}
}
static class FakeFile {
 public static ProbeDate GetLastAccessTime(string path){Probe.Add("A:"+path);Probe.Hook(5);if(Probe.Real)return new(System.IO.File.GetLastAccessTime(path));string name=System.IO.Path.GetFileName(path);long offset=name.StartsWith("new")?1:name.StartsWith("edge")?0:-1;return new(new System.DateTime(Probe.PurgeTicks+offset,Probe.Kind));}
 public static void Delete(string path){Probe.Add("X:"+path);Probe.Hook(6);if(Probe.Real)System.IO.File.Delete(path);else Probe.Files.Remove(path);}
}
partial class FlotsamAssetCache {
 string m_CacheDirectory="cache";TimeSpan m_FileExpiration;bool m_timerRunning,m_cleanupRunning;object timerLock=new(),weakAssetReferencesLock=new();WeakMap weakAssetReferences=new();ulong m_weakRefHits=7;CleanupTimer m_CacheCleanTimer=new();static HashSet<string> m_defaultAssets;static int m_CacheWarnAt;Logger m_log=new();
 public FlotsamAssetCache(bool running,bool timer,long expiration,int warn){m_cleanupRunning=running;m_timerRunning=timer;m_FileExpiration=TimeSpan.FromTicks(expiration);m_CacheWarnAt=warn;m_defaultAssets=Probe.Defaults;weakAssetReferences.Keys=new(Probe.Files.Select(System.IO.Path.GetFileName).Append("unrelated"));}
 Dictionary<UUID,sbyte> GatherSceneAssets(){Probe.Add("G");Probe.Hook(13);return Probe.Protected;}
 public void Cancel()=>m_cleanupRunning=false;
 public void Root(string root){m_CacheDirectory=root;}
 public string Call(string op,int cooldown){if(op=="tree")return CleanExpiredFiles("cache/a",Probe.Protected,new ProbeDate(new System.DateTime(Probe.PurgeTicks,Probe.Kind)),cooldown).ToString();if(op=="all"){DoCleanExpiredFiles(new(new System.DateTime(Probe.PurgeTicks,Probe.Kind)));return "OK";}if(op=="retry"){string a;try{CleanupExpiredFiles(null,null);a="OK";}catch{a="EX";}CleanupExpiredFiles(null,null);return a+":OK";}CleanupExpiredFiles(null,null);return "OK";}
 public string State()=>(m_cleanupRunning?1:0)+":"+(m_timerRunning?1:0)+":"+m_weakRefHits+":"+Probe.Starts+":"+Probe.HeapCalls+"|"+string.Join(",",weakAssetReferences.Keys.OrderBy(s=>s,StringComparer.Ordinal));
}
static class Probe {
 public static bool Real;public static HashSet<string> Dirs,Files,Defaults;public static Dictionary<string,List<string>> Children,DirFiles;public static Dictionary<UUID,sbyte> Protected;public static long NowTicks,PurgeTicks,HeapDelta;public static DateTimeKind Kind;public static int Starts,HeapCalls;static int point,nth,errorKind,cancel,cancelNth,seen,cancelSeen;static List<string> trace=new();static FlotsamAssetCache cache;
 public static void Add(string s)=>trace.Add(s);
 public static void Hook(int p){if(cancel==p&&++cancelSeen==cancelNth)cache.Cancel();if(point==p&&++seen==nth)throw errorKind switch{1=>new DirectoryNotFoundException("fault"),2=>new UnauthorizedAccessException("fault"),3=>new FileNotFoundException("fault"),4=>new InvalidOperationException("fault"),_=>new IOException("fault")};}
 static void Dir(string name,params string[] children){Dirs.Add(name);Children[name]=children.ToList();foreach(var child in children)Dirs.Add(child);}
 static void Items(string name,params string[] files){DirFiles[name]=files.ToList();foreach(var file in files)Files.Add(file);}
 static void Build(int profile){Dirs=new();Files=new();Defaults=new(){"default"};Children=new();DirFiles=new();Protected=new(){{new UUID("11111111-1111-1111-1111-111111111111"),0},{UUID.Zero,0}};Dir("cache","cache/a","cache/b");Items("cache","cache/root-marker");if(profile==0){Dir("cache/a","cache/a/nested");Items("cache/a/nested","cache/a/nested/old","cache/a/nested/edge","cache/a/nested/new","cache/a/nested/default","cache/a/nested/11111111-1111-1111-1111-111111111111");Items("cache/a","cache/a/old2");}else if(profile==1)Items("cache/a",Enumerable.Range(0,30).Select(i=>"cache/a/old"+i).ToArray());else if(profile==2){Dir("cache/a","cache/a/n1");Dir("cache/a/n1","cache/a/n1/n2");Dir("cache/a/n1/n2","cache/a/n1/n2/n3");}else if(profile==3){var names=Enumerable.Range(0,40).Select(i=>"p"+i).ToArray();foreach(var n in names)Defaults.Add(n);Items("cache/a",names.Select(n=>"cache/a/"+n).ToArray());}else if(profile==4)Items("cache/a","cache/a/");else if(profile==5){Dirs.Remove("cache/a");Dirs.Remove("cache/b");}else if(profile==6){Dirs.Clear();Files.Clear();}else Items("cache/a","cache/a/00000000-0000-0000-0000-000000000000","cache/a/11111111111111111111111111111111","cache/a/not-a-uuid","cache/a/old");}
 static void Main(string[] args){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;if(args.Length>0){var root=Encoding.UTF8.GetString(Convert.FromHexString(args[1]));if(args[0]=="--time"){Console.WriteLine(System.IO.File.GetLastAccessTime(root).Ticks);return;}if(args[0]=="--remove-tree"){try{System.IO.Directory.Delete(root,true);Console.WriteLine("OK");}catch{Console.WriteLine("EX");}return;}Real=true;Build(0);Kind=DateTimeKind.Local;HeapDelta=-1048577;PurgeTicks=long.Parse(args[2]);cache=new(true,false,0,3);cache.Root(root);string outcome;try{outcome=cache.Call("all",0);}catch{outcome="EX";}Console.WriteLine(outcome+"|"+string.Join(";",trace)+"|"+cache.State());return;}string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ',StringSplitOptions.RemoveEmptyEntries);trace.Clear();Build(int.Parse(p[1]));Starts=HeapCalls=seen=cancelSeen=0;point=int.Parse(p[6]);nth=int.Parse(p[7]);errorKind=int.Parse(p[8]);cancel=int.Parse(p[9]);cancelNth=int.Parse(p[10]);NowTicks=long.Parse(p[12]);PurgeTicks=long.Parse(p[13]);Kind=(DateTimeKind)int.Parse(p[14]);HeapDelta=long.Parse(p[15]);cache=new(p[2]=="1",p[3]=="1",long.Parse(p[11]),int.Parse(p[4]));string result;try{result=cache.Call(p[0],int.Parse(p[5]));}catch{result="EX";}Console.WriteLine(result+"|"+string.Join(";",trace)+"|"+cache.State()+"|"+string.Join(",",Dirs.OrderBy(s=>s,StringComparer.Ordinal))+"|"+string.Join(",",Files.OrderBy(s=>s,StringComparer.Ordinal)));}}
}
