using System;
using System.Net;
using System.Net.Http;
using System.Reflection;
class Reference {
 static int Main() {
  var type=typeof(HttpClient).Assembly.GetType("System.Net.Http.HttpNoProxy",true)!;
  var proxy=(IWebProxy)Activator.CreateInstance(type,true)!;
  if(proxy.GetProxy(null!)!=null || !proxy.IsBypassed(null!) || proxy.Credentials!=null)return 1;
  var credential=CredentialCache.DefaultNetworkCredentials;
  proxy.Credentials=credential;if(!ReferenceEquals(proxy.Credentials,credential))return 2;
  HttpClient.DefaultProxy=proxy;if(!ReferenceEquals(HttpClient.DefaultProxy,proxy))return 3;
  try {HttpClient.DefaultProxy=null!;return 4;}
  catch(ArgumentNullException e){if(e.ParamName!="value"||e.HResult!=unchecked((int)0x80004003))return 5;}
  if(!ReferenceEquals(HttpClient.DefaultProxy,proxy))return 6;
  foreach(var text in new[]{"http://example.com/", "https://example.com/", "socks5://localhost/", "relative"}){
   var uri=new Uri(text,UriKind.RelativeOrAbsolute);
   if(proxy.GetProxy(uri)!=null||!proxy.IsBypassed(uri))return 7;
  }
  Console.WriteLine("PASS HttpNoProxy and HttpClient.DefaultProxy value semantics");return 0;
 }
}
