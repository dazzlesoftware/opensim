using System;using System.Collections.Generic;using System.Globalization;using System.Net;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;using GridRegion=OpenSim.Services.Interfaces.GridRegion;
namespace OpenSim.Framework {public class AgentCircuitData{public UUID AgentID;}}
static class Host {
 public static List<OSDMap> Calls;public static List<string> Logs;public static int Scenario;public static GridRegion Gate;public static AgentCircuitData Circuit;public static EntityTransferContext Previous;public static UserAgentServiceConnector Connector;
 public static void Log(string message){Logs.Add(message);if(Scenario==6)throw new InvalidOperationException("log");}
 public static bool Create(GridRegion source,GridRegion destination,AgentCircuitData circuit,uint flags,EntityTransferContext context,out string reason,Action<OSDMap> pack){
  reason="callee";var args=new OSDMap();args["seed"]=OSD.FromInteger(7);
  var record=new OSDMap{{"same_circuit",OSD.FromBoolean(object.ReferenceEquals(circuit,Circuit))},{"null_circuit",OSD.FromBoolean(circuit==null)},{"context",context.Pack()},{"fresh_context",OSD.FromBoolean(!object.ReferenceEquals(Previous,context))},{"flags",OSD.FromString(flags.ToString())},{"path",OSD.FromString(Connector.Path())},{"home_id",OSD.FromUUID(destination.RegionID)},{"home_x",OSD.FromInteger(destination.RegionLocX)},{"home_y",OSD.FromInteger(destination.RegionLocY)},{"home_name",OSD.FromString(destination.RegionName)},{"home_size_x",OSD.FromInteger(destination.RegionSizeX)},{"home_size_y",OSD.FromInteger(destination.RegionSizeY)},{"home_uri",OSD.FromString(destination.ServerURI)},{"args",args}};Calls.Add(record);Previous=context;
  if(Scenario==2)throw new InvalidOperationException("create");
  if(Scenario==3&&Gate!=null){Gate.ExternalHostName="Mutated";Gate.HttpPort=uint.MaxValue;Gate.ServerURI="https://MUTATED/Path";}
  if(Scenario==7&&Calls.Count==1){var next=new GridRegion{ServerURI="https://SECOND/Case",ExternalHostName="SecondGate",HttpPort=54321};string ignored;Connector.LoginAgentToGrid(source,circuit,next,destination,false,out ignored);}
  pack(args);reason=Scenario==1?"denied":"created";return Scenario!=1;
 }
 public static string Hex(string s)=>Convert.ToHexString(System.Text.Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
 public static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;string line;while((line=Console.ReadLine())!=null){var a=line.Split(' ');int profile=int.Parse(a[0]);Scenario=int.Parse(a[1]);Calls=new();Logs=new();Previous=null;string reason="seed",status;OSDMap after=null;bool alive=true;var circuit=(profile&8)!=0?null:new AgentCircuitData{AgentID=new UUID("00000000-0000-0000-0000-000000000007")};Circuit=circuit;
  var source=new GridRegion{RegionID=new UUID("00000000-0000-0000-0000-000000000002"),RegionLocX=-256,RegionLocY=1024,RegionName=(profile&256)!=0?null:"Source",RawServerURI=(profile&256)!=0?null:"https://SOURCE/raw"};
  var gate=new GridRegion{RegionID=new UUID("00000000-0000-0000-0000-000000000003"),RegionLocX=300,RegionLocY=400,HttpPort=9001,ExternalHostName=(profile&64)!=0?null:"GATE.host",RawServerURI=(profile&32)!=0?null:"https://GATE/Case"};
  var dest=new GridRegion{RegionID=new UUID("00000000-0000-0000-0000-000000000001"),RegionLocX=int.MinValue,RegionLocY=int.MaxValue,RegionSizeX=512,RegionSizeY=1024,RegionName="Destination",ServerURI="https://Destination/Case"};
  Gate=(profile&4)!=0?null:gate;WeakReference weak=new WeakReference(Gate);Connector=null;
  try{Connector=new UserAgentServiceConnector((profile&128)!=0?"relative":"HTTPS://HOME/Case?TOKEN=Secret");bool value;if(Scenario==4)value=Connector.LoginAgentToGrid((profile&1)!=0?null:source,circuit,Gate,(profile&2)!=0?null:dest,out reason);else value=Connector.LoginAgentToGrid((profile&1)!=0?null:source,circuit,Gate,(profile&2)!=0?null:dest,(profile&16)==0,out reason);status=value?"1":"0";
   if(Scenario==5&&(profile&2)==0){if(Gate!=null)Gate.HttpPort=12345;gate=null;Gate=null;GC.Collect();alive=(profile&4)!=0||weak.IsAlive;after=new OSDMap();Connector.PackOnly(after,(profile&1)!=0?null:source,circuit,dest,uint.MaxValue);}
  }catch(Exception e){status="E:"+e.GetType().FullName+":"+e.Message;}
  foreach(var call in Calls){var keys=new OSDArray();foreach(string key in ((OSDMap)call["args"]).Keys)keys.Add(OSD.FromString(key));call["keys"]=keys;}
  var output=new OSDMap{{"status",OSD.FromString(status)},{"reason",OSD.FromString(reason)},{"calls",new OSDArray()},{"logs",new OSDArray()},{"alive",OSD.FromBoolean(alive)},{"after",after??(OSD)new OSD()}};foreach(var c in Calls)((OSDArray)output["calls"]).Add(c);foreach(var log in Logs)((OSDArray)output["logs"]).Add(OSD.FromString(log));Console.WriteLine(Hex(OSDParser.SerializeJsonString(output,true)));
 }}
}
class Log {public void Debug(string s){Host.Log(s);}public void DebugFormat(string f,params object[] v){Host.Log(string.Format(f,v));}}
partial class SimulationServiceConnector {
 public bool CreateAgent(GridRegion source,GridRegion destination,AgentCircuitData circuit,uint flags,EntityTransferContext context,out string reason)=>Host.Create(source,destination,circuit,flags,context,out reason,args=>PackData(args,source,circuit,destination,flags));
}
