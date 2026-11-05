// Copyright (c) Contributors, http://opensimulator.org/
// Actual bundled Nini.dll source graph behind the thin view compatibility API.
using System;using System.IO;using System.Linq;using System.Text;using System.Globalization;using Nini.Config;
class Program {
 static string Hex(string s)=>s.Length==0?"-":Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
 static string Unhex(string s)=>s=="-"?"":Encoding.UTF8.GetString(Convert.FromHexString(s));
 static string RawHex(string s){if(s.Length==0)return "-";return string.Concat(s.Select(c=>((int)c).ToString("x4")));}
 static string Raw(string s){if(s=="-")return "";var a=new char[s.Length/4];for(int i=0;i<a.Length;i++)a[i]=(char)Convert.ToUInt16(s.Substring(4*i,4),16);return new string(a);}
 static string Execute(string line){var sources=new IConfigSource[4];return string.Join("/",line.Split('|').Select(operation=>{try{var a=operation.Split(' ');int i=int.Parse(a[1]);var s=sources[i];string T(int n)=>Unhex(a[n]);IConfig C(int n)=>s.Configs[T(n)];switch(a[0]){
 case "NEW":sources[i]=new IniConfigSource();break;case "ARG":sources[i]=new ArgvConfigSource(Array.Empty<string>());break;case "TEXT":sources[i]=new IniConfigSource(new StringReader(T(2)));break;case "LOAD":sources[i]=new IniConfigSource(T(2));break;
 case "COPY":sources[i]=sources[int.Parse(a[2])];break;case "DROP":sources[i]=null;break;
 case "SET":var c=C(2)??s.AddConfig(T(2));c.Set(T(3),T(4));break;case "GET":return "G:"+Hex(C(2)?.GetString(T(3),T(4))??T(4));case "INT":return "I:"+(C(2)?.GetInt(T(3),int.Parse(a[4]))??int.Parse(a[4]));case "BOOL":return "B:"+((C(2)?.GetBoolean(T(3),a[4]=="1")??(a[4]=="1"))?1:0);
 case "HAS":return "B:"+(C(2)!=null?1:0);case "CON":return "B:"+(C(2)?.Contains(T(3))==true?1:0);case "ORDER":var section=C(2);var keys=section?.GetKeys()??Array.Empty<string>();return "O:"+keys.Length+string.Concat(keys.Select(k=>":"+Hex(k)+"="+Hex(section.Get(k)??"")));
 case "ALIAS":s.Alias.AddAlias(T(2),a[3]=="1");break;case "MERGE":s.Merge(sources[int.Parse(a[2])]);break;case "CASE":((IniConfigSource)s).CaseSensitive=a[2]=="1";break;case "REPLACE":s.ReplaceKeyValues();break;
 case "RAW":var rc=C(2)??s.AddConfig(T(2));rc.Set(T(3),Raw(a[4]));break;case "OWNER":return "B:"+(C(2)!=null&&ReferenceEquals(C(2).ConfigSource,sources[int.Parse(a[3])])?1:0);case "SAVE":((IniConfigSource)s).Save(T(2));break;
 default:throw new Exception("Unknown fixture command");}return "OK";}catch(Exception e){return "E:"+e.GetType().FullName+":"+unchecked((uint)e.HResult)+":"+(e is ArgumentException arg&&arg.ParamName!=null?RawHex(arg.ParamName):"~")+":"+RawHex(e.Message);}}));}
 static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;for(string line;(line=Console.ReadLine())!=null;)Console.WriteLine(Execute(line));}
}
