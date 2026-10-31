using System;using System.Collections;using System.Collections.Generic;using System.IO;using System.Net;using System.Text;using System.Xml;using System.Reflection;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework.Capabilities;using OSDMap=OpenMetaverse.StructuredData.OSDMap;using Timer=ControlledTimer;
class ControlledTimer {public ControlledTimer(Action<object> cb,object state,int due,int period){InventoryAssetReference.Add("T"+due);}public void Dispose(){InventoryAssetReference.Add("T0");}}
static class Timeout {public const int Infinite=-1;}
class Logger {public void Error(string s){InventoryAssetReference.Add("L");}public void Debug(string s){InventoryAssetReference.Add("L");}public void InfoFormat(string s){}}
class IOSHttpRequest {public string HttpMethod="POST";public IPEndPoint RemoteIPEndPoint=new(IPAddress.Loopback,123);public Stream InputStream;public object Headers;}
class IOSHttpResponse {public int StatusCode=200;public byte[] RawBuffer;public void AddHeader(string k,string v){}}
interface IHttpServer {void RemoveSimpleStreamHandler(string p);void AddSimpleStreamHandler(SimpleBinaryHandler h);}
class Server:IHttpServer {public void RemoveSimpleStreamHandler(string p){InventoryAssetReference.Add("R");}public void AddSimpleStreamHandler(SimpleBinaryHandler h){InventoryAssetReference.Add("A");}}
class SceneObjectPart {}
class Permissions {public bool CanEditObjectInventory(UUID o,UUID a){InventoryAssetReference.Add("I");return (InventoryAssetReference.Mode&2)==0;}public bool CanEditScript(UUID i,UUID o,UUID a){InventoryAssetReference.Add("S");return (InventoryAssetReference.Mode&4)==0;}}
class Scene {public Permissions Permissions=new();public SceneObjectPart GetSceneObjectPart(UUID id){InventoryAssetReference.Add("O");if((InventoryAssetReference.Mode&8)!=0)throw new Exception();return (InventoryAssetReference.Mode&1)==0?new():null;}}
class HostCaps {public bool SSLCaps=false;public string HostName="host";public int Port=9000;public IHttpServer HttpListener=new Server();public UUID AgentID=InventoryAssetReference.Id(1);}
static class Util {public static byte[] UTF8NBGetbytes(string s)=>Encoding.UTF8.GetBytes(s);public static void SaveAssetToFile(string f,byte[] b){InventoryAssetReference.Add("D");throw new Exception();}}
class LLSDSerializeException:Exception {public LLSDSerializeException(string s):base(s){}}
partial class LLSD {}
partial class SimpleBinaryHandler {
 public int RequestsReceived,RequestsHandled;public string m_httMethod;public int m_maxDatasize=-1;public Auth m_Auth=null;public Action<IOSHttpRequest,IOSHttpResponse,byte[]> m_processRequest;
 public SimpleBinaryHandler(string m,string p,Action<IOSHttpRequest,IOSHttpResponse,byte[]> cb){m_httMethod=m;m_processRequest=cb;}
 public int MaxDataSize {set{m_maxDatasize=value;}}
 protected void ProcessRequest(IOSHttpRequest r,IOSHttpResponse s,byte[] b){}
}
class Auth {public bool Authenticate(object h,Action<string,string> a,out HttpStatusCode code){code=HttpStatusCode.OK;return true;}}
public delegate UUID UpdateItem(UUID i,UUID o,byte[] data);
partial class BunchOfCaps {
 Scene m_Scene=new();UUID m_AgentID=InventoryAssetReference.Id(1);HostCaps m_HostCapsObj=new();bool m_dumpAssetsToFile=false;string m_regionName="region";static Logger m_log=new();
 public delegate UUID ItemUpdatedCallback(UUID a,UUID i,UUID o,byte[] data);public ItemUpdatedCallback ItemUpdatedCall;
 string GetNewCapPath(){InventoryAssetReference.Add("P");return "/upload";}
 public void TaskScriptUpdated(UUID i,UUID o,bool r,byte[] data,ref ArrayList errors){}
 public delegate void UpdateTaskScript(UUID i,UUID o,bool r,byte[] data,ref ArrayList errors);
}
class InventoryAssetReference {
 public static int Mode;static string Trace;public static void Add(string s){Trace+="|"+s;}
 public static UUID Id(int n)=>new(n==1?"11223344-5566-7788-99aa-000000000001":"11223344-5566-7788-99aa-000000000002");
 static string Probe(int kind,int mode,int phase){Mode=mode;Trace="";var req=new IOSHttpRequest();var resp=new IOSHttpResponse();
 if(kind==0||kind==2){var map=new OSDMap();map["item_id"]=OSD.FromString(phase==6?"invalid":Id(1).ToString());if(phase!=8)map["task_id"]=OSD.FromString(phase==7?"invalid":Id(2).ToString());try{var bunch=new BunchOfCaps();if(kind==2)bunch.UpdateAnimSetItemAsset(req,resp,map);else bunch.UpdateNotecardItemAsset(req,resp,map);}catch{resp.StatusCode=500;}}
 else{var up=new BunchOfCaps.ItemUpdater(Id(1),Id(2),unchecked((byte)mode),"/upload",new Server(),(mode&128)!=0);up.m_remoteAdress=IPAddress.Loopback;
 if((mode&16)==0)up.OnUpLoad+=(i,o,b)=>{Add("U:"+b.Length);if((mode&32)!=0)throw new Exception();return (mode&64)!=0?UUID.Zero:Id(2);};
 byte[] body=Encoding.UTF8.GetBytes("default{}");if(phase==1)req.RemoteIPEndPoint=new(IPAddress.Parse("127.0.0.2"),456);if(phase==2)req.HttpMethod="GET";if(phase==3)body=Array.Empty<byte>();if(phase==4)body=new byte[10000001];if(phase==6)body=new byte[10000000];req.InputStream=new MemoryStream(body);
 if(phase==5){up.Timedout(null);resp.StatusCode=0;}else{if(phase==7)up.Timedout(null);var h=new SimpleBinaryHandler("POST","/upload",up.process);h.MaxDataSize=10000000;h.Handle(req,resp);}}

 return resp.StatusCode+"|"+Convert.ToHexString(resp.RawBuffer??Array.Empty<byte>()).ToLowerInvariant()+Trace;}
 static void Main(){string s;while((s=Console.ReadLine())!=null){var a=Array.ConvertAll(s.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1],a[2]));}}
}
