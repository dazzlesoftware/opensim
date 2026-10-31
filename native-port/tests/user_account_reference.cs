using System;using System.Text;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;
namespace OpenSim.Framework {public static class Util {public static int UnixTimeSinceEpoch()=>123;}}
class AccountModelReference {
    static string Hex(string s)=>Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Str(string s)=>s==null?"null":"s"+Hex(s);
    static object Build(string kind,string value){switch(kind){case "string":return value=="."?"":Encoding.UTF8.GetString(Convert.FromHexString(value));case "int":return int.Parse(value);case "long":return long.Parse(value);case "bool":return value=="1";case "real":return BitConverter.UInt64BitsToDouble(Convert.ToUInt64(value,16));case "uuid":return new UUID("00000000-0000-0000-0000-000000000007");default:return null;}}
    static string Snapshot(UserAccount a){string s=a.PrincipalID+":"+a.ScopeID+":"+Str(a.FirstName)+":"+Str(a.LastName)+":"+Str(a.Email)+":"+Str(a.UserTitle)+":"+Str(a.UserCountry)+":"+a.UserLevel+":"+a.UserFlags+":"+a.Created+":"+(a.LocalToGrid?1:0)+":"+Hex(a.Name)+":";if(a.ServiceURLs==null)return s+"null";foreach(var p in a.ServiceURLs)s+=Hex(p.Key)+"="+(p.Value==null?"null":"s"+Hex(p.Value.ToString()))+";";return s;}
    static void Main(){Culture.SetCurrentCulture();var a=new UserAccount();string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');string op=p[0],status="OK",output="-";
        try{
            if(op=="default")a=new UserAccount();else if(op=="principal")a=new UserAccount(new UUID("00000000-0000-0000-0000-000000000007"));
            else if(op=="init"||op=="random"){var scope=new UUID("00000000-0000-0000-0000-000000000008");a=op=="init"?new UserAccount(scope,new UUID("00000000-0000-0000-0000-000000000007"),"First","Last",null):new UserAccount(scope,"First","Last",null);if(op=="random"){var bytes=a.PrincipalID.GetBytes();output=(bytes[6]>>4)==4&&(bytes[8]&192)==128?"1":"0";a.PrincipalID=new UUID("00000000-0000-0000-0000-000000000007");}}
            else if(op=="set"||op=="all"){int at=op=="set"?2:1;var value=Build(p[at],p.Length>at+1?p[at+1]:"");var values=new Dictionary<string,object>();if(op=="set")values[p[1]]=value;else foreach(var key in new[]{"FirstName","LastName","Email","PrincipalID","ScopeID","UserLevel","UserFlags","UserTitle","UserCountry","LocalToGrid","Created","ServiceURLs"})values[key]=value;a=new UserAccount(values);}
            else if(op=="export"){var values=a.ToKeyValuePairs();output="";foreach(var pair in values)output+=pair.Key+"="+(pair.Value==null?"null":"s"+Hex(pair.Value.ToString()))+";";}
            else if(op=="objects")a.ServiceURLs=new Dictionary<string,object>{{"null",null},{"integer",7},{"star;key","value*;"}};
        }catch{status="EX";}Console.WriteLine(status+"|"+output+"|"+Snapshot(a));
    }}
}
