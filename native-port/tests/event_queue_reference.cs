using System;using System.Collections.Generic;using System.Reflection;using System.Text;using OpenMetaverse;using OpenSim.Region.ClientStack.Linden;using OpenSim.Framework.Servers.HttpServer;
namespace OpenSim.Framework{public static class Util{public static byte[] UTF8NBGetbytes(string s)=>new UTF8Encoding(false).GetBytes(s);}}
namespace Mono.Addins{public class ExtensionAttribute:Attribute{public string Path{get;set;}public string NodeName{get;set;}public string Id{get;set;}}}
namespace OpenSim.Region.Framework.Interfaces{public interface IEventQueue{}public interface INonSharedRegionModule{}}
namespace OpenSim.Framework.Servers.HttpServer{public class OSHttpRequest{}public class OSHttpResponse{}}
namespace OpenSim.Framework{public class MainConsole{public static MainConsole Instance=new();public Commands Commands=new();public void Output(string s){}}public class Commands{public void AddCommand(string a,bool b,string c,string d,string e,Action<string,string[]> f){}}}
namespace OpenSim.Region.Framework.Scenes{
public class ScenePresence{public string Name=>"Viewer";}
public class RegionInfo{public string RegionName=>"Region";}
public class Events{public event Action<UUID,Scene> OnClientClosed;public event Action<UUID,OpenSim.Framework.Capabilities.Caps> OnRegisterCaps;}
public class Scene{public string Name=>"Scene";public RegionInfo RegionInfo=new();public Events EventManager=new();public void RegisterModuleInterface<T>(T m){}public void UnregisterModuleInterface<T>(T m){}public ScenePresence GetScenePresence(UUID id)=>new();}}
namespace OpenSim.Framework.Capabilities{public class Caps{public PollServiceEventArgs Handler;public void RegisterPollHandler(string n,PollServiceEventArgs p){Handler=p;}}}
public class QueueLog{public void Debug(object s){}public void DebugFormat(string s,params object[] a){}public void Warn(object s){}public void WarnFormat(string s,params object[] a){}public void Error(object s){}}
public static class QueueHost{public static int Draws;public static int Next(int max)=>40+Draws++;public static UUID RandomUUID()=>new UUID("00000000-0000-0000-0000-000000000001");}
public static class QueueReference{
static void Main(){string line;while((line=Console.ReadLine())!=null){int profile=0;if(line.StartsWith('@')){int end=line.IndexOf(':');profile=int.Parse(line[1..end]);line=line[(end+1)..];}QueueHost.Draws=0;var m=new EventQueueGetModule();m.AddRegion(new());var c=new OpenSim.Framework.Capabilities.Caps();UUID a=new("00000000-0000-0000-0000-000000000002"),r=UUID.Zero;foreach(char op in line){try{switch(op){
case 'R':m.OnRegisterCaps(a,c);Console.Write(c.Handler.Url);break;
case 'N':m.OnRegisterCaps(a,null);break;
case 'A':Console.Write(m.Enqueue(Encoding.UTF8.GetBytes("A"),a)?1:0);break;
case 'B':Console.Write(m.Enqueue(Encoding.UTF8.GetBytes("B"),a)?1:0);break;
case 'Z':Console.Write(m.Enqueue(Array.Empty<byte>(),a)?1:0);break;
case 'M':Console.Write(m.Enqueue((byte[])null,a)?1:0);break;
case 'U':Console.Write(m.Enqueue((osUTF8)null,a)?1:0);break;
case 'L':Console.Write(m.Enqueue(OpenMetaverse.StructuredData.OSD.FromLong(-12345678901234),a)?1:0);break;
case 'O':Console.Write(m.Enqueue(OpenMetaverse.StructuredData.OSD.FromInteger(0),a)?1:0);break;
case 'G':var v=m.GetEvents(r,a);Console.Write(v["int_response_code"]);if(v.ContainsKey("bin_response_data"))Console.Write(":"+Convert.ToHexString((byte[])v["bin_response_data"]).ToLowerInvariant());break;
case 'H':Console.Write(m.HasEvents(r,a)?1:0);break;
case 'T':Console.Write(m.NoEvents(r,a)["int_response_code"]);break;
case 'C':typeof(EventQueueGetModule).GetMethod("ClientClosed",BindingFlags.NonPublic|BindingFlags.Instance).Invoke(m,new object[]{a,null});break;
case 'D':m.Drop(r,a);break;
case '0':case 'X':case 'Y':((Dictionary<UUID,int>)typeof(EventQueueGetModule).GetField("m_ids",BindingFlags.NonPublic|BindingFlags.Instance).GetValue(m))[a]=op=='0'?0:op=='X'?int.MaxValue:int.MinValue;break;
default:Producers.Produce(m,a,op,profile);break;
}Console.Write(';');}catch{Console.Write("EX;");}}Console.WriteLine("|"+QueueHost.Draws);}}
}
