using System;using System.IO;using System.Linq;using System.Text;using System.Collections;using System.Collections.Generic;using System.Globalization;using System.Net;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using OpenSim.Services.Interfaces;using Response=OpenSim.Services.LLLoginService.LLLoginResponse;
namespace OpenSim.Framework {
 public class InventoryFolderImpl:InventoryFolderBase{}
}
namespace OpenSim.Services.Interfaces {
 public class GridRegion{public int RegionLocX,RegionLocY,RegionSizeX=256,RegionSizeY=256;public string uri;public IPEndPoint endpoint;
 public string ServerURI{get{ResponseFixture.Events+="uri;";if(ResponseFixture.Profile==20)throw new Exception();if(ResponseFixture.Profile==37)RegionSizeX=999;return ResponseFixture.Profile==21?null:uri;}}
 public IPEndPoint ExternalEndPoint{get{ResponseFixture.Events+="endpoint;";if(ResponseFixture.Profile==19)throw new Exception();if(ResponseFixture.Profile==36)RegionLocX=-512;return ResponseFixture.Profile==18?null:endpoint;}}}
 public class GridUserInfo{public Vector3 HomePosition,HomeLookAt;}
 public interface ILibraryService{InventoryFolderImpl LibraryRootFolder{get;}Dictionary<UUID,InventoryFolderImpl> GetAllFolders();}
}
public static partial class ResponseFixture {
 public static int random,clocks;public static double SecondsValue=1700000000;
 public static UUID NextId()=>new UUID("00000000-0000-0000-0000-"+(++random).ToString("x12"));public static double Seconds{get{++clocks;return SecondsValue;}}
}
public partial class ResponseProgram {
 static Response current;static List<Hashtable> held=new();static string H(string s)=>Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();static string Text(string s)=>s=="-"?null:s=="."?"":Encoding.UTF8.GetString(Convert.FromHexString(s));
 static string View(object v){if(v==null)return "u";if(v is OSD o){switch(o.Type){case OSDType.Unknown:return "u";case OSDType.UUID:return "q"+o.AsUUID();case OSDType.Array:return "["+string.Join(",",((OSDArray)o).Select(x=>View(x)))+"]";case OSDType.Map:var values=new SortedDictionary<string,object>(StringComparer.Ordinal);foreach(KeyValuePair<string,OSD> p in (OSDMap)o)values[p.Key]=p.Value;return View(values);case OSDType.Integer:return "i"+o.AsInteger();case OSDType.String:return "s"+H(o.AsString());case OSDType.Boolean:return o.AsBoolean()?"b1":"b0";default:throw new Exception();}}
 if(v is string s)return "s"+H(s);if(v is int n)return "i"+n;if(v is bool b)return b?"b1":"b0";if(v is IDictionary d){var sorted=new SortedDictionary<string,object>(StringComparer.Ordinal);foreach(DictionaryEntry e in d)sorted[(string)e.Key]=e.Value;return "{"+string.Join(",",sorted.Select(p=>H(p.Key)+"="+View(p.Value)))+"}";}if(v is IEnumerable list){var values=new List<string>();foreach(var x in list)values.Add(View(x));return "["+string.Join(",",values)+"]";}throw new Exception("unknown view");}
 static ArrayList List(int mode){if(mode==0)return null;var result=new ArrayList();if(mode==1)return result;if(mode==2)result.Add(new Hashtable{{"name",""},{"null",null},{"i",1},{"bool",false}});if(mode==3)result.Add("wrong");if(mode==4)result.Add(new Hashtable{{"map",new Hashtable()},{"array",new ArrayList()}});return result;}
 static void Main(){Culture.SetCurrentCulture();string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');string output="OK";if(p[0]=="construct"){Console.WriteLine(Construct(int.Parse(p[1]),int.Parse(p[2])));continue;}try{switch(p[0]){
 case "new":ResponseFixture.random=ResponseFixture.clocks=0;current=new Response();held.Clear();break;
 case "xml":var h=current.ToHashtable();held.Add(h);output=View(h);break;case "llsd":output=View(current.ToOSDMap());break;case "held":output=View(held[int.Parse(p[1])]);break;
 case "set":typeof(Response).GetProperty(p[1]).SetValue(current,Text(p[2]));break;
 case "num":var prop=typeof(Response).GetProperty(p[1]);prop.SetValue(current,Convert.ChangeType(p[2],prop.PropertyType,CultureInfo.InvariantCulture));break;
 case "list":typeof(Response).GetProperty(p[1]).SetValue(current,List(int.Parse(p[2])));break;
 case "ui":current.AddToUIConfig(Text(p[1]),Text(p[2]));break;case "category":current.AddClassifiedCategory(int.Parse(p[1]),Text(p[2]));break;
 case "clock":ResponseFixture.SecondsValue=BitConverter.UInt64BitsToDouble(Convert.ToUInt64(p[1],16));break;
 case "buddy":if(current.BuddList==null)current.BuddList=new();var buddy=new Response.BuddyList.BuddyInfo(Text(p[1])){BuddyRightsHave=int.Parse(p[2]),BuddyRightsGiven=int.Parse(p[3])};current.BuddList.AddNewBuddy(buddy);current.BuddList.AddNewBuddy(buddy);break;
 case "buddy-null":if(current.BuddList==null)current.BuddList=new();current.BuddList.AddNewBuddy(null);break;case "buddy-reset":current.BuddList=null;break;
 case "mut-ui":((Hashtable)((ArrayList)held[int.Parse(p[1])]["ui-config"])[0])[Text(p[2])]=Text(p[3]);break;
 case "mut-inventory":((ArrayList)held[int.Parse(p[1])]["inventory-skeleton"]).Add(new Hashtable{{"added","live"}});break;
 case "mut-global":((Hashtable)((ArrayList)held[int.Parse(p[1])]["global-textures"])[0])["sun_texture_id"]=Text(p[2]);break;
 }}catch{output="EX";}Console.WriteLine(output+"|"+ResponseFixture.random+":"+ResponseFixture.clocks);}}
}
