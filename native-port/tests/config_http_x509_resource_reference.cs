using System;using System.Reflection;using System.Security.Cryptography.X509Certificates;
class Program {
 static byte[] Diagnose(Type type,string uri,long ticks){
  if(ticks<=0)return null;
  var download=(Func<string,System.Threading.CancellationToken,bool,System.Threading.Tasks.Task<byte[]>>)type.GetField("s_downloadBytes",BindingFlags.Static|BindingFlags.NonPublic).GetValue(null);if(download==null)return null;
  long ms=(long)TimeSpan.FromTicks(ticks).TotalMilliseconds;using var cts=ms>int.MaxValue?null:new System.Threading.CancellationTokenSource((int)ms);
  try{return download(uri,cts?.Token??default,false).GetAwaiter().GetResult();}catch(Exception e){Console.Error.WriteLine("FAULT "+uri+" "+e);return null;}
 }
 static void Main(string[] args){if(args[0]!="default")AppContext.SetData("System.Security.Cryptography.AiaDownloadLimit",long.Parse(args[0]));var type=typeof(X509Certificate2).Assembly.GetType("System.Net.Http.X509ResourceClient",true);var method=type.GetMethod("DownloadAsset",BindingFlags.Static|BindingFlags.NonPublic);string line;
  long limit=args[0]=="default"?104857600:long.Parse(args[0]);
  int index=0;while((line=Console.ReadLine())!=null){var fields=line.Split(' ');if(fields.Length>2&&limit>0&&limit<=int.MaxValue)System.Threading.Thread.Sleep(int.Parse(fields[2]));string uri=null;if(fields[1]!="_"){uri="";if(fields[1]!="-")for(int i=0;i<fields[1].Length;i+=4)uri+=(char)Convert.ToUInt16(fields[1].Substring(i,4),16);}var watch=System.Diagnostics.Stopwatch.StartNew();var result=Environment.GetEnvironmentVariable("OPENSIM_TEST_X509_FAULTS")=="1"?Diagnose(type,uri,long.Parse(fields[0])):(byte[])method.Invoke(null,new object[]{uri,TimeSpan.FromTicks(long.Parse(fields[0]))});Console.Error.WriteLine((index++)+" "+watch.ElapsedMilliseconds);Console.WriteLine(result==null?"_":result.Length==0?"-":Convert.ToHexString(result).ToLowerInvariant());}
 }
}
