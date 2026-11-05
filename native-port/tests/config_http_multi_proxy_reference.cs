using System;using System.Net.Http;using System.Reflection;using System.Collections.Generic;using System.Linq;
class Reference{
 static string Hex(string s)=>Convert.ToHexString(System.Text.Encoding.BigEndianUnicode.GetBytes(s)).ToLowerInvariant();
 static int CacheChecks(){
  var type=typeof(HttpClient).Assembly.GetType("System.Net.Http.FailedProxyCache",true)!;var cache=Activator.CreateInstance(type,true)!;
  var entries=(IDictionary<Uri,long>)type.GetField("_failedProxies",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(cache)!;
  object? Call(string name,params object?[] args){try{return type.GetMethod(name)!.Invoke(cache,args);}catch(TargetInvocationException e){System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(e.InnerException!).Throw();throw;}}
  var uri=new Uri("http://proxy/");if((long)Call("GetProxyRenewTicks",uri)!=0)return 1;
  long before=Environment.TickCount64;Call("SetProxyFailed",uri);long value=(long)Call("GetProxyRenewTicks",uri)!;if(value<before+1800000||value>Environment.TickCount64+1800000)return 2;
  if((bool)Call("TryRenewProxy",uri,value-1)!||(long)Call("GetProxyRenewTicks",uri)!=value)return 3;
  if(!(bool)Call("TryRenewProxy",uri,value)!||(bool)Call("TryRenewProxy",uri,value)!)return 4;
  entries[uri]=1;if((long)Call("GetProxyRenewTicks",uri)!=0||entries.Count!=0)return 5;
  for(int i=0;i<8;i++)entries[new Uri("http://p"+i+"/")]=1;
  var flush=type.GetField("_nextFlushTicks",BindingFlags.Instance|BindingFlags.NonPublic)!;flush.SetValue(cache,0L);
  if((long)Call("GetProxyRenewTicks",uri)!=0||entries.Count!=8)return 6;
  before=Environment.TickCount64;Call("SetProxyFailed",uri);long next=(long)flush.GetValue(cache)!;if(entries.Count!=1||next<before+300000||next>Environment.TickCount64+300000)return 7;
  foreach(string name in new[]{"GetProxyRenewTicks","SetProxyFailed","TryRenewProxy"}){bool rejected=false;try{if(name=="TryRenewProxy")Call(name,null,0L);else Call(name,new object?[]{null});}catch(ArgumentNullException e){rejected=e.ParamName==(name=="TryRenewProxy"?"item":"key")&&e.HResult==unchecked((int)0x80004003);}if(!rejected)return 8;}
  Call("SetProxyFailed",new Uri("http://PROXY:80/#fragment"));if(entries.Count!=1)return 9;
  value=(long)Call("GetProxyRenewTicks",uri)!;entries[uri]=value+1;if((bool)Call("TryRenewProxy",uri,value)!||(long)Call("GetProxyRenewTicks",uri)!=value+1)return 10;
  Console.WriteLine("PASS FailedProxyCache lifetime, conditional renewal, cleanup threshold, URI equality and null keys");return 0;
 }
 static void Main(string[] arguments){if(arguments.Length!=0){Environment.ExitCode=CacheChecks();return;}
var assembly=typeof(HttpClient).Assembly;var type=assembly.GetType("System.Net.Http.MultiProxy",true)!;var cacheType=assembly.GetType("System.Net.Http.FailedProxyCache",true)!;var read=type.GetMethod("ReadNext",BindingFlags.Instance|BindingFlags.Public)!;var index=type.GetField("_currentIndex",BindingFlags.Instance|BindingFlags.NonPublic)!;var parse=type.GetMethod("Parse",BindingFlags.Public|BindingFlags.Static)!;string? row;
 while((row=Console.ReadLine())!=null){var pieces=row.Split(' ');string? text=pieces[3]=="null"?null:pieces[3]=="-"?"":System.Text.Encoding.BigEndianUnicode.GetString(Convert.FromHexString(pieces[3]));bool secure=pieces[1]=="1";int mode=int.Parse(pieces[2]);long start=Environment.TickCount64;var cache=Activator.CreateInstance(cacheType,true)!;
 cacheType.GetField("_nextFlushTicks",BindingFlags.NonPublic|BindingFlags.Instance)!.SetValue(cache,long.MaxValue);var entries=(IDictionary<Uri,long>)cacheType.GetField("_failedProxies",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(cache)!;
 var seed= parse.Invoke(null,new object?[]{cache,text,secure})!;var uris=(Uri[])type.GetField("_uris",BindingFlags.NonPublic|BindingFlags.Instance)!.GetValue(seed)!;
 for(int i=0;i<uris.Length;i++){if(mode==1&&i!=0)continue;if(mode!=0)entries[uris[i]]=mode==4?1:mode==5?long.MaxValue:long.MaxValue-1000+(mode==3?0:i);}
 var method=type.GetMethod(pieces[0]=="1"?"CreateLazy":"Parse",BindingFlags.Public|BindingFlags.Static)!;var plan=method.Invoke(null,new object?[]{cache,text,secure})!;
 for(int i=0;i<1024;++i){var args=new object?[]{null,false};var found=(bool)read.Invoke(plan,args)!;if(i!=0)Console.Write("|");Console.Write((found?"1":"0")+":"+((bool)args[1]!?"1":"0")+":"+index.GetValue(plan)+":"+(args[0] is Uri uri?Hex(uri.AbsoluteUri):"-"));if(!found)break;}
 Console.Write("#");foreach(var e in entries.OrderBy(e=>e.Key.AbsoluteUri,StringComparer.Ordinal)){Console.Write(Hex(e.Key.AbsoluteUri)+":");if(e.Value>=long.MaxValue-1000)Console.Write("S"+(long.MaxValue-e.Value));else if(e.Value==1)Console.Write("E");else if(e.Value>=start+1800000&&e.Value<=Environment.TickCount64+1800000)Console.Write("F");else Console.Write("BAD"+e.Value);Console.Write(",");}Console.WriteLine();
 }
 }
}
