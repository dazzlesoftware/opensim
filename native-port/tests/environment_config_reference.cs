// Actual Nini.dll core plus unchanged extracted Env class bodies and Util method.
using System;using System.IO;using System.Text;using System.Linq;using System.Collections.Generic;using System.Globalization;using System.Reflection;using System.Runtime.InteropServices;using Nini.Config;
sealed class ProbeSource:ConfigSourceBase{}
namespace Shim {static class Environment {public static Func<string,string> Get;public static Action<string,string> Set;public static string GetEnvironmentVariable(string k)=>Get(k);public static void SetEnvironmentVariable(string k,string v)=>Set(k,v);}}
sealed class Probe {
 [DllImport("libc",EntryPoint="getenv")]static extern IntPtr GetRaw(byte[] key);
 [DllImport("libc",EntryPoint="setenv")]static extern int SetRaw(byte[] key,byte[] value,int overwrite);
 [DllImport("libc",EntryPoint="unsetenv")]static extern int UnsetRaw(byte[] key);
 static byte[] Bytes(string s)=>Encoding.UTF8.GetBytes(s+"\0");

 static string Enc(string s)=>s==null?"-":s.Length==0?".":string.Concat(s.Select(c=>((int)c&255).ToString("x2")+((int)c>>8).ToString("x2")));
 static string Dec(string s)=>s=="-"?null:s=="."?"":new string(Enumerable.Range(0,s.Length/4).Select(i=>(char)(Convert.ToInt32(s.Substring(i*4,2),16)|(Convert.ToInt32(s.Substring(i*4+2,2),16)<<8))).ToArray());
 readonly List<IConfigSource> masters=new();IConfigSource current;readonly Dictionary<string,string> dictionary=new(StringComparer.Ordinal);readonly List<string> trace=new();bool real;int point,nth,hit,mutation;
 void Event(int p,string s){trace.Add(s);if(point==p&&++hit==nth)throw new Exception("fault");}
 void Bind(IConfigSource s){int id=masters.Count;masters.Add(s);s.Saved+=(o,a)=>Event(5,"S:"+id);s.Configs.ConfigRemoved+=(o,a)=>Event(6,"R:"+id+":"+Enc(a.Config.Name));s.Configs.ConfigAdded+=(o,a)=>{var c=a.Config;Event(4,"A:"+id+":"+Enc(c.Name)+":"+c.GetType().Name);((ConfigBase)c).KeySet+=(o,k)=>Event(3,"K:"+id+":"+Enc(c.Name)+":"+Enc(k.KeyName)+":"+Enc(k.KeyValue));};current=s;}
 ScopedEnv.Env.EnvMap Map=>ScopedEnv.Env.EnvMap.Instance;
 void Merge(){if(real)RealUtil.MergeEnvironmentToConfig(current);else ControlledUtil.MergeEnvironmentToConfig(current);}
 void Mutate(){int m=mutation;mutation=0;if(m==1){var c=current.Configs["Environment"];if(c!=null)current.Configs.Remove(c);}else if(m==2){var c=current.Configs.Add("Other");c.Set("x","changed");}else if(m==3)current.Configs["Environment"]?.Set("added","during-get");else if(m==4)Map.EnvList.Add("Injected",new ScopedEnv.Env.EnvItem("Injected","injected-value",""));else if(m==5)Merge();}
 string Get(string k){Event(1,"G:"+Enc(k));string v=null;if(k!=null)dictionary.TryGetValue(k,out v);Mutate();return v;}
 void Set(string k,string v){Event(2,"E:"+Enc(k)+":"+Enc(v));if(k==null)throw new ArgumentNullException("key");if(v==null)dictionary.Remove(k);else dictionary[k]=v;}
 string Snapshot(){var b=new StringBuilder();for(int i=0;i<masters.Count;i++){b.Append(i).Append('{');foreach(IConfig c in masters[i].Configs){b.Append(Enc(c.Name)).Append(':').Append(c.GetType().Name).Append(':').Append(c.ConfigSource.GetType().Name).Append('[');foreach(string k in c.GetKeys())b.Append(Enc(k)).Append('=').Append(Enc(c.Get(k))).Append(';');b.Append(']');}b.Append('}');}b.Append("/M:");if(real){var m=(Nini.Env.EnvMap)typeof(Nini.Env.EnvMap).GetField("instance",BindingFlags.Static|BindingFlags.NonPublic).GetValue(null);if(m!=null)for(int i=0;i<m.EnvList.Count;i++){var x=m.GetItem(i);b.Append(Enc(x.Name)).Append('=').Append(Enc(x.Value)).Append(';');}}else{var m=(ScopedEnv.Env.EnvMap)typeof(ScopedEnv.Env.EnvMap).GetField("instance",BindingFlags.Static|BindingFlags.NonPublic).GetValue(null);if(m!=null)for(int i=0;i<m.EnvList.Count;i++){var x=m.GetItem(i);b.Append(Enc(x.Name)).Append('=').Append(Enc(x.Value)).Append(';');}}return b.ToString();}
 public string Run(string row){var p=row.Split(' ',5);real=p[0]=="R";point=int.Parse(p[1]);nth=int.Parse(p[2]);mutation=int.Parse(p[3]);typeof(ScopedEnv.Env.EnvMap).GetField("instance",BindingFlags.Static|BindingFlags.NonPublic).SetValue(null,null);Shim.Environment.Get=Get;Shim.Environment.Set=Set;Bind(new ProbeSource());var results=new List<string>();foreach(string token in p[4].Split(',')){var a=token.Split(':');string op=a[0],result=op;try{switch(op){
 case "new":Bind(new ProbeSource());break;case "sel":current=masters[int.Parse(a[1])];break;case "null":current=null;break;case "sec":current.Configs.Add(Dec(a[1]));break;
 case "key":current.Configs[Dec(a[1])].Set(Dec(a[2]),Dec(a[3]));break;case "add":((ConfigBase)current.Configs[Dec(a[1])]).Add(Dec(a[2]),Dec(a[3]));break;
 case "env":if(real)System.Environment.SetEnvironmentVariable(Dec(a[1]),Dec(a[2]));else if(Dec(a[1])!=null){if(Dec(a[2])==null)dictionary.Remove(Dec(a[1]));else dictionary[Dec(a[1])]=Dec(a[2]);}break;
 case "get":result="V:"+Enc(real?System.Environment.GetEnvironmentVariable(Dec(a[1])):Get(Dec(a[1])));break;case "emptyid":{var first=System.Environment.GetEnvironmentVariable(Dec(a[1]));var second=System.Environment.GetEnvironmentVariable(Dec(a[2]));string fallback=null;var oldGet=Shim.Environment.Get;var oldSet=Shim.Environment.Set;try{Shim.Environment.Get=k=>null;Shim.Environment.Set=(k,v)=>fallback=v;var auxiliary=new ProbeSource();((ConfigBase)auxiliary.Configs.Add("Environment")).Add("CHECK_MISSING","ignored");ControlledUtil.MergeEnvironmentToConfig(auxiliary);}finally{Shim.Environment.Get=oldGet;Shim.Environment.Set=oldSet;}result="ID:"+(first!=null&&first.Length==0&&ReferenceEquals(first,second)&&ReferenceEquals(first,fallback)&&ReferenceEquals(first,string.Empty)&&ReferenceEquals(first,System.Environment.GetEnvironmentVariable(Dec(a[1]))));break;}case "rawbytes":SetRaw(Convert.FromHexString(a[1]+"00"),Convert.FromHexString(a[2]+"00"),1);break;case "rawget":result="V:"+Enc(Marshal.PtrToStringUTF8(GetRaw(Bytes(Dec(a[1])))));break;case "rawset":if(Dec(a[2])==null)UnsetRaw(Bytes(Dec(a[1])));else SetRaw(Bytes(Dec(a[1])),Bytes(Dec(a[2])),1);break;case "merge":Merge();break;case "auto":current.AutoSave=a[1]=="1";break;
 case "ecase":((ScopedEnv.Config.EnvConfigSource)current.Configs["Environment"].ConfigSource).CaseSensitive=a[1]=="1";break;
 case "cache":Map.EnvList.Add(Dec(a[1]),new ScopedEnv.Env.EnvItem(Dec(a[2]),Dec(a[3]),""));break;case "cv":Map.GetItem(int.Parse(a[1])).Value=Dec(a[2]);break;case "rm":current.Configs.Remove(current.Configs[Dec(a[1])]);break;case "mut":mutation=int.Parse(a[1]);break;default:throw new Exception("operation");}}
 catch(Exception e){result="EX:"+e.GetType().FullName+":"+unchecked((uint)e.HResult).ToString(CultureInfo.InvariantCulture)+":"+Enc(e.Message)+":"+Enc(e is ArgumentException ae?ae.ParamName:null);}results.Add(result+"/"+Snapshot());}return string.Join("|",results)+"/IO:"+string.Join(";",trace);}
 static void Main(string[] args){if(args.Length>0&&args[0]=="--identity"){var source=new ProbeSource();var config=source.Configs.Add("Environment");((ConfigBase)config).Add("A","ignored");((ConfigBase)config).Add("B","ignored");var defaults=new List<string>();Shim.Environment.Get=k=>null;Shim.Environment.Set=(k,v)=>defaults.Add(v);ControlledUtil.MergeEnvironmentToConfig(source);try{ControlledUtil.MergeEnvironmentToConfig(source);}catch(ArgumentException){}var env=new ScopedEnv.Config.EnvConfigSource();env.LoadEnv();var name=env.Configs[0].Name;env.LoadEnv();Console.WriteLine(defaults.Count+":"+(defaults.Count==3&&ReferenceEquals(defaults[0],string.Empty)&&ReferenceEquals(defaults[0],defaults[1])&&ReferenceEquals(defaults[0],defaults[2])&&ReferenceEquals(name,env.Configs[0].Name)));return;}if(args.Length>0&&args[0]=="--runtime"){Console.WriteLine(System.Environment.Version+"|"+typeof(System.Environment).Assembly.Location+"|"+RuntimeInformation.OSDescription);return;}CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;string row;while((row=Console.ReadLine())!=null)Console.WriteLine(new Probe().Run(row));}
}

namespace ScopedEnv.Env {
using System;using Nini.Config;using Nini.Util;using ScopedEnv.Env;using Environment=Shim.Environment;
// BEGIN EXTRACTED EnvAdapter
public class EnvAdapter
{
	public string Get(string key, string defaultValue)
	{
		string environmentVariable = Environment.GetEnvironmentVariable(key);
		if (environmentVariable == null)
		{
			Set(key, defaultValue);
			return defaultValue;
		}
		return environmentVariable;
	}

	public void Set(string key, string val)
	{
		Environment.SetEnvironmentVariable(key, val);
	}
}
// END EXTRACTED EnvAdapter
}

namespace ScopedEnv.Env {
using System;using Nini.Config;using Nini.Util;using ScopedEnv.Env;
// BEGIN EXTRACTED EnvMap
public sealed class EnvMap
{
	private static volatile EnvMap instance;

	private static object syncRoot = new object();

	private OrderedList envList;

	private bool synchronized = false;

	private EnvAdapter env = null;

	public bool Synchronized
	{
		get
		{
			return synchronized;
		}
		set
		{
			synchronized = value;
		}
	}

	public string this[string key]
	{
		get
		{
			if (envList.Contains(key))
			{
				lock (syncRoot)
				{
					return envList[key].ToString();
				}
			}
			return null;
		}
		set
		{
			lock (syncRoot)
			{
				envList[key] = value;
			}
		}
	}

	public OrderedList EnvList
	{
		get
		{
			lock (syncRoot)
			{
				return envList;
			}
		}
	}

	public static EnvMap Instance
	{
		get
		{
			if (instance == null)
			{
				lock (syncRoot)
				{
					if (instance == null)
					{
						instance = new EnvMap();
						instance.envList = new OrderedList();
					}
				}
			}
			return instance;
		}
	}

	private EnvMap()
	{
		env = new EnvAdapter();
	}

	public EnvItem GetItem(int index)
	{
		return (EnvItem)EnvList[index];
	}

	public void Add(string key, string defaultValue)
	{
		EnvItem value = new EnvItem(key, env.Get(key, defaultValue), "");
		envList.Add(key, value);
	}

	public void Save()
	{
	}
}
// END EXTRACTED EnvMap
}

namespace ScopedEnv.Env {
using System;using Nini.Config;using Nini.Util;using ScopedEnv.Env;
// BEGIN EXTRACTED EnvItem
public class EnvItem
{
	private string envName = "";

	private string envValue = "";

	private string envComment = null;

	public string Value
	{
		get
		{
			return envValue;
		}
		set
		{
			envValue = value;
		}
	}

	public string Name => envName;

	protected internal EnvItem(string name, string val, string comment)
	{
		envName = name;
		envValue = val;
		envComment = comment;
	}
}
// END EXTRACTED EnvItem
}

namespace ScopedEnv.Config {
using System;using Nini.Config;using Nini.Util;using ScopedEnv.Env;
// BEGIN EXTRACTED EnvConfig
public class EnvConfig : ConfigBase
{
	private EnvConfigSource parent = null;

	public EnvConfig(string name, IConfigSource source)
		: base(name, source)
	{
		parent = (EnvConfigSource)source;
	}

	public override void Set(string key, object value)
	{
		if (!parent.CaseSensitive)
		{
			key = CaseInsensitiveKeyName(key);
		}
		base.Set(key, value);
	}

	private string CaseInsensitiveKeyName(string key)
	{
		string text = null;
		string text2 = key.ToLower();
		foreach (string key2 in keys.Keys)
		{
			if (key2.ToLower() == text2)
			{
				text = key2;
				break;
			}
		}
		return (text == null) ? key : text;
	}
}
// END EXTRACTED EnvConfig
}

namespace ScopedEnv.Config {
using System;using Nini.Config;using Nini.Util;using ScopedEnv.Env;
// BEGIN EXTRACTED EnvConfigSource
public class EnvConfigSource : ConfigSourceBase
{
	private EnvMap envmap = EnvMap.Instance;

	private bool caseSensitive = true;

	public bool CaseSensitive
	{
		get
		{
			return caseSensitive;
		}
		set
		{
			caseSensitive = value;
		}
	}

	public void AddEnv(string key, string defaultValue)
	{
		envmap.Add(key, defaultValue);
	}

	public override void Save()
	{
		envmap.Save();
		base.Save();
	}

	public void LoadEnv()
	{
		Configs.Clear();
		Load();
	}

	private void Load()
	{
		EnvConfig envConfig = new EnvConfig("Environment", this);
		Configs.Add(envConfig);
		EnvItem envItem = null;
		for (int i = 0; i < envmap.EnvList.Count; i++)
		{
			envItem = envmap.GetItem(i);
			if (envItem.Name != null)
			{
				envConfig.Add(envItem.Name, envItem.Value);
			}
		}
	}
}
// END EXTRACTED EnvConfigSource
}
