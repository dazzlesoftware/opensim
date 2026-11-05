using System;using System.Collections.Generic;using System.Globalization;using System.Linq;using System.Text;
using OriginalSource=Nini.Config.IniConfigSource;using OriginalSection=Nini.Config.IConfig;
class Source{public Sections Configs;public Source(Sections sections){Configs=sections;}}
class Sections{public Dictionary<string,Section> Values=new();public Section this[string name]{get{Probe.Log("S:"+name);Probe.Hit(1);return Values.TryGetValue(name,out var value)?value:null;}}}
class Section{
 string name;OriginalSection original;public Section(string name,OriginalSection original){this.name=name;this.original=original;}
 public string GetString(string key,string fallback){Probe.Log("G:"+name+":"+key+":"+Probe.Text(fallback));Probe.Hit(2);return original.GetString(key,fallback);}
 public bool GetBoolean(string key,bool fallback){Probe.Log("B:"+name+":"+key+":"+(fallback?1:0));Probe.Hit(3);return original.GetBoolean(key,fallback);}
 public int GetInt(string key,int fallback){Probe.Log("N:"+name+":"+key+":"+fallback);Probe.Hit(5);return original.GetInt(key,fallback);}
 public long GetLong(string key,long fallback){Probe.Log("L:"+name+":"+key+":"+fallback);Probe.Hit(6);return original.GetLong(key,fallback);}
 public double GetDouble(string key,double fallback){Probe.Log("R:"+name+":"+key+":"+BitConverter.DoubleToUInt64Bits(fallback));Probe.Hit(4);return original.GetDouble(key,fallback);}
}
static class FakePath{public static string GetFullPath(string value){Probe.Log("P:"+Probe.Text(value));Probe.Hit(7);if(string.IsNullOrEmpty(value))throw new Exception("path");return "FULL:"+value;}}
struct FakeTimeSpan{
 public long Ticks;public static FakeTimeSpan FromHours(double value){Probe.Log("T:"+BitConverter.DoubleToUInt64Bits(value));Probe.Hit(8);return new(){Ticks=System.TimeSpan.FromHours(value).Ticks};}
 public double TotalHours{get{Probe.Log("H:"+Ticks);Probe.Hit(14);return System.TimeSpan.FromTicks(Ticks).TotalHours;}}
}
namespace OpenSim.Framework{
 class AssetBase{}
 class ExpiringKey<T>{public int Tag;public ExpiringKey(int interval){Tag=++Probe.Serial;Probe.Log("K:"+Tag+":"+interval);Probe.Hit(9);}}
 class ExpiringCacheOS<K,V>{public int Tag;public ExpiringCacheOS(int interval){Tag=++Probe.Serial;Probe.Log("M:"+Tag+":"+interval);Probe.Hit(10);}}
}
class Logger{public void Info(string value){Probe.Log("I:"+value);Probe.Hit(11);}public void Debug(string value){Probe.Log("D:"+value);Probe.Hit(12);}}
class Commands{public void AddCommand(string category,bool shared,string command,string usage,string description,Action<string,string[]> handler){Probe.Log("C:"+category+":"+(shared?1:0)+":"+command+":"+usage+":"+description);Probe.Hit(13);}}
class ConsoleStub{public Commands Commands=new();}static class MainConsole{public static ConsoleStub Instance=new();}
partial class FlotsamAssetCache{
 bool m_Enabled=false,m_FileCacheEnabled=true,m_MemoryCacheEnabled=false,m_negativeCacheEnabled=true;string m_CacheDirectory="assetcache",m_assetLoader,m_assetLoaderArgs;double m_MemoryExpiration=.016;int m_negativeExpiration=120,m_LogLevel=0;ulong m_HitRateDisplay=100;FakeTimeSpan m_FileExpiration=new(){Ticks=48L*3600*10000000},m_FileExpirationCleanupTimer=new(){Ticks=3600L*10000000};const double m_DefaultFileExpiration=48;
 static int m_CacheDirectoryTiers=1,m_CacheDirectoryTierLen=3,m_CacheWarnAt=30000;static bool m_updateFileTimeOnCacheHit;static OpenSim.Framework.ExpiringKey<string> m_lastFileAccessTimeChange;OpenSim.Framework.ExpiringKey<string> m_negativeCache;OpenSim.Framework.ExpiringCacheOS<string,OpenSim.Framework.AssetBase> m_MemoryCache;Logger m_log=new();public string Name=>"FlotsamAssetCache";void HandleConsoleCommand(string module,string[] args){}
 public static void Reset(){m_CacheDirectoryTiers=1;m_CacheDirectoryTierLen=3;m_CacheWarnAt=30000;m_updateFileTimeOnCacheHit=false;m_lastFileAccessTimeChange=null;}
 public string Snapshot()=>(m_Enabled?1:0).ToString()+(m_FileCacheEnabled?1:0)+(m_MemoryCacheEnabled?1:0)+(m_negativeCacheEnabled?1:0)+","+Probe.Text(m_CacheDirectory)+","+BitConverter.DoubleToUInt64Bits(m_MemoryExpiration)+","+m_negativeExpiration+","+m_LogLevel+","+m_HitRateDisplay+","+m_FileExpiration.Ticks+","+m_FileExpirationCleanupTimer.Ticks+","+Probe.Text(m_assetLoader)+","+Probe.Text(m_assetLoaderArgs)+","+(m_negativeCache?.Tag??0)+","+(m_MemoryCache?.Tag??0);
 public static string Shared()=>m_CacheDirectoryTiers+","+m_CacheDirectoryTierLen+","+m_CacheWarnAt+","+(m_updateFileTimeOnCacheHit?1:0)+","+(m_lastFileAccessTimeChange?.Tag??0)+","+Probe.Serial;
}
static class Probe{
 public static int Serial;static int point,nth=1,seen;static List<string> trace=new();static FlotsamAssetCache[] modules;
 public static void Log(string value)=>trace.Add(value);public static void Hit(int value){if(value==point&&++seen==nth)throw new Exception("fault");}public static string Text(string value)=>value==null?"~":Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
 static Source Config(int profile){if(profile<0)return null;var real=new OriginalSource();var sections=new Sections();Section Add(string name){var actual=real.AddConfig(name);var value=new Section(name,actual);sections.Values[name]=value;return value;}void Set(string section,string key,string value)=>real.Configs[section].Set(key,value);var source=new Source(sections);if(profile==0)return source;Add("Modules");if(profile==1)return source;Set("Modules","AssetCaching",profile==2?"wrong":"FlotsamAssetCache");if(profile<4)return source;Add("AssetCache");if(profile==4)return source;
 var fields=new Dictionary<string,string>{{"FileCacheEnabled","true"},{"MemoryCacheEnabled","true"},{"MemoryCacheTimeout","0.25"},{"NegativeCacheEnabled","false"},{"NegativeCacheTimeout","-2147484"},{"UpdateFileTimeOnCacheHit","true"},{"LogLevel","2"},{"HitRateDisplay","-1"},{"FileCacheTimeout","1.25"},{"FileCleanupTimer","0.001"},{"CacheDirectoryTiers","9"},{"CacheDirectoryTierLength","0"},{"CacheWarnAt","0"}};Add("AssetService");Set("AssetService","DefaultAssetLoader","loader");Set("AssetService","AssetLoaderArgs","args");
 switch(profile){case 6:fields["FileCacheEnabled"]="false";fields["MemoryCacheTimeout"]="NaN";fields["NegativeCacheEnabled"]="true";fields["NegativeCacheTimeout"]="2147483647";fields["LogLevel"]="-2147483648";fields["HitRateDisplay"]="9223372036854775807";fields["FileCacheTimeout"]="-1";fields["FileCleanupTimer"]="0";fields["CacheDirectoryTiers"]="-9";fields["CacheDirectoryTierLength"]="99";fields["CacheWarnAt"]="-1";Set("AssetService","AssetLoaderArgs"," \u2003 ");break;case 7:fields["FileCacheTimeout"]="NaN";break;case 8:fields["FileCacheTimeout"]="Infinity";break;case 9:fields["FileCacheTimeout"]="1e20";break;case 10:fields["FileCacheEnabled"]="bogus";break;case 11:fields["NegativeCacheTimeout"]="2147483648";break;case 12:fields["MemoryCacheTimeout"]="x";break;case 13:fields["CacheDirectory"]="";break;case 14:fields["MemoryCacheTimeout"]="Infinity";Set("AssetService","DefaultAssetLoader","\u2003");break;case 15:fields["MemoryCacheTimeout"]="-0";fields["CacheDirectory"]="é/漢";Set("AssetService","AssetLoaderArgs","\u200b");break;}
 foreach(var field in fields)Set("AssetCache",field.Key,field.Value);return source;}
 static void Reset(){Serial=point=seen=0;nth=1;FlotsamAssetCache.Reset();modules=new[]{new FlotsamAssetCache(),new FlotsamAssetCache()};}
 static string Run(string line){trace.Clear();var parts=line.Split(' ');string result;try{if(parts[0]=="reset"){Reset();result="reset";}else if(parts[0]=="fail"){point=int.Parse(parts[1]);nth=int.Parse(parts[2]);seen=0;result="fail";}else{modules[int.Parse(parts[1])].Initialise(Config(int.Parse(parts[2])));result="OK";}}catch{result="EX";}return result+"|"+Text(string.Join(";",trace))+"|"+string.Join(";",modules.Select(m=>m.Snapshot()))+"|"+FlotsamAssetCache.Shared();}
 public static void Main(){Console.OutputEncoding=new UTF8Encoding(false);CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;Reset();string line;while((line=Console.ReadLine())!=null)Console.WriteLine(Run(line));}
}
