using System;
using System.Net.Http;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Text.Json;
class Program {
 static object Dump(Exception e) => new {type=e.GetType().FullName,e.Message,e.HResult,requestError=e is HttpRequestException h?(int?)h.HttpRequestError:null,nativeError=e is SocketException s?(int?)s.NativeErrorCode:null,socketError=e is SocketException t?(int?)t.SocketErrorCode:null,inner=e.InnerException is {} x?Dump(x):null};
 static void Main(){
  var flags=BindingFlags.NonPublic|BindingFlags.Static;
  var wrap=typeof(NetworkStream).GetMethod("WrapException",flags);
  var sr=typeof(NetworkStream).Assembly.GetType("System.SR");
  var connection=typeof(HttpClient).Assembly.GetType("System.Net.Http.HttpConnection");
  var instance=RuntimeHelpers.GetUninitializedObject(connection);
  var map=connection.GetMethod("MapSendException",BindingFlags.NonPublic|BindingFlags.Instance);
  foreach(var code in new[]{10013,10049,10050,10051,10053,10054,10058,10060,10061,10065})foreach(bool write in new[]{false,true})foreach(bool headers in new[]{false,true}){
   var socket=new SocketException(code);
   var format=(string)sr.GetProperty(write?"net_io_writefailure":"net_io_readfailure",flags).GetValue(null);
   var io=(Exception)wrap.Invoke(null,new object[]{format,socket});
   Exception result=io;
   if(headers){object[] args={io,CancellationToken.None,null};if(!(bool)map.Invoke(instance,args))throw new Exception("MapSendException did not map IOException");result=(Exception)args[2];}
   Console.WriteLine(JsonSerializer.Serialize(new {code,write,headers,error=Dump(result)}));
  }
 }
}
