using System;
using System.Net;
using System.Net.Http;
class Stop:Exception{}
class Proxy:IWebProxy {
 public int Calls;public ICredentials? Credentials{get;set;}
 public Uri GetProxy(Uri uri)=>throw new Stop();
 public bool IsBypassed(Uri uri){Calls++;return true;}
}
class Reference{
 static string Address="";
 static void Send(HttpClient client){using var stream=client.GetStreamAsync(Address).GetAwaiter().GetResult();using var reader=new System.IO.StreamReader(stream);if(reader.ReadToEnd()!="proxy-capture")throw new Exception("Wrong body");}
 static int Main(string[] args){
  Address="http://127.0.0.1:"+args[0]+"/";
  var a=new Proxy();var b=new Proxy();var c=new Proxy();HttpClient.DefaultProxy=a;
  using var next=new HttpClient();using var client=new HttpClient();Send(client);HttpClient.DefaultProxy=b;Send(client);
  Send(next);
  using var specific=new HttpClient(new HttpClientHandler{Proxy=c});Send(specific);
  if(a.Calls!=2||b.Calls!=1||c.Calls!=1)return 1;
  Console.WriteLine("PASS handler proxy capture: 2,1,1");return 0;
 }
}
