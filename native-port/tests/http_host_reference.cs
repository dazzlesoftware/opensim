using System;using System.Net;using System.Text;using System.Globalization;using System.Collections.Generic;using OpenSim.Framework;
namespace OpenSim.Framework {public static class Util {public static IPAddress GetHostFromDNS(string host){HostReference.Dns.Add(host??"NULL");if(HostReference.Mode==4||HostReference.Mode==5)throw new Exception("dns");return host=="bad.invalid"?null:IPAddress.Parse("203.0.113.7");}}}
static class HostReference {
 public static int Mode;public static List<string> Dns=new();
 static string H(string s)=>Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
 static string D(string h){if(h=="N")return null;if(h==".")return "";var bytes=Convert.FromHexString(h);var chars=new char[bytes.Length/2];for(int i=0;i<chars.Length;i++)chars[i]=(char)(bytes[2*i]|bytes[2*i+1]<<8);return new string(chars);}
 public static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');Mode=int.Parse(p[0]);Dns.Clear();string result;try{
  if(Mode==6){Console.WriteLine(H("TYPE:"+(int)Uri.CheckHostName(D(p[1]))));continue;}
  var host=new OSHHTPHost(D(p[1]),Mode==1||Mode==4);if(Mode==2||Mode==5)host.ResolveDNS();host.ValidAndResolved(out var error);
  result=string.Join("|",(int)host.Flags,host.Port,host.Host,host.URI,host.IP?.ToString()??"NULL",host.URIwEndSlash,host.HostAndPort,host.IsValidHost?1:0,host.IsResolvedHost?1:0,error,string.Join(",",Dns));
  if(Mode==3){var other=new OSHHTPHost(D(p[2]));result+="|"+(host.Equals(other)?1:0)+"|"+host.CompareTo(other);}
 }catch{result="EX";}Console.WriteLine(H(result));}}
}
