using System;using System.IO;using System.IO.Compression;using System.Net;using System.Net.Http;using System.Net.Http.Headers;using System.Text;using System.Threading;using System.Collections.Generic;using System.Globalization;using OpenMetaverse.StructuredData;
static class Host {
 public static int Level;public static List<string> Logs=new();
 public static string H(string s){if(s==null)return "-";if(s.Length==0)return ".";var b=new StringBuilder(s.Length*4);foreach(char c in s)b.Append(((int)c&255).ToString("x2")).Append(((int)c>>8).ToString("x2"));return b.ToString();}
 static string D(string h){if(h=="-")return null;if(h==".")return "";var bytes=Convert.FromHexString(h);var chars=new char[bytes.Length/2];for(int i=0;i<chars.Length;i++)chars[i]=(char)(bytes[2*i]|bytes[2*i+1]<<8);return new string(chars);}
 public static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;
#if GLOBAL_WEB_HTTP_FACTORY
WebUtil.SetupHTTPClients(false,false,null,200);
#endif
string line;while((line=Console.ReadLine())!=null){var a=line.Split(' ');int flags=int.Parse(a[0]),timeout=int.Parse(a[2]);Level=int.Parse(a[1]);Logs.Clear();string status;try{var data=D(a[5]);var result=WebUtil.ServiceOSDRequest(D(a[4]),data==null?null:(OSDMap)OSDParser.DeserializeJson(data),D(a[3]),timeout,(flags&1)!=0,(flags&2)!=0,(flags&4)!=0);status="R:"+H(OSDParser.SerializeJsonString(result,true));}catch(Exception e){status="E:"+e.GetType().FullName+":"+H(e.Message);}Logs.RemoveAll(s=>s.StartsWith("[LOGHTTP]: HTTP OUT ")&&s.Contains(" took "));Console.WriteLine(status+"|"+H(string.Join("\n",Logs)));}}
}
class Log {public void Debug(string s){Host.Logs.Add(s);}public void Info(string s){Host.Logs.Add(s);}}
static partial class WebUtil {
 static Log m_log=new();public static int RequestNumber;public static int DebugLevel=>Host.Level;public const string OSHeaderRequestID="opensim-request-id";public const int LongCallTime=3000,MaxRequestDiagLength=200;
#if GLOBAL_WEB_HTTP_FACTORY
 public static SocketsHttpHandler SharedSocketsHttpHandler,SharedSocketsHttpHandlerNoRedir;
#else
 static SocketsHttpHandler handler=new(){UseCookies=false,UseProxy=false,AllowAutoRedirect=true,MaxAutomaticRedirections=10,AutomaticDecompression=DecompressionMethods.None,ConnectTimeout=TimeSpan.FromSeconds(120),MaxConnectionsPerServer=200,PooledConnectionIdleTimeout=TimeSpan.FromSeconds(31),PooledConnectionLifetime=TimeSpan.FromMinutes(3)};
 public static HttpClient GetNewGlobalHttpClient(int timeout){var c=new HttpClient(handler,false){Timeout=TimeSpan.FromMilliseconds(timeout>0?timeout:30000),MaxResponseContentBufferSize=250*1024*1024};c.DefaultRequestHeaders.ExpectContinue=false;return c;}
#endif
}
static partial class Util {public static int EnvironmentTickCount()=>0;public static int EnvironmentTickCountSubtract(int n)=>0;public static byte[] UTF8Getbytes(string s)=>Encoding.UTF8.GetBytes(s);}
