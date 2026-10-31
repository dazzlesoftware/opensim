using System;using System.IO;using System.Text;using System.Xml;using System.Text.Json;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Services.Interfaces;using OpenSim.Data;
public class IOSHttpRequest{}public class IOSHttpResponse{}
public static class AuthEndpointUtil{public static Encoding UTF8NoBomEncoding=new UTF8Encoding(false);public static byte[] DocToBytes(XmlDocument doc){using var m=new MemoryStream();using(var w=new XmlTextWriter(m,UTF8NoBomEncoding){Formatting=Formatting.Indented})doc.WriteTo(w);return m.ToArray();}}
public class AuthLogger{public void ErrorFormat(string f,params object[] a){}public void DebugFormat(string f,params object[] a){}}
public partial class AuthInfoReference{
 public class Database{public AuthenticationData row,stored;public string trace="";public int outcome;public AuthenticationData Get(UUID id){trace+="get:"+id+";";return row;}public bool Store(AuthenticationData r){stored=r;trace+="store;";if(outcome==2)throw new Exception();return outcome==0;}}
 public Database m_Database=new();AuthLogger m_log=new();
}
public partial class AuthenticationHandler{
 IAuthenticationService m_AuthenticationService;bool m_AllowGetAuthInfo,m_AllowSetAuthInfo,m_AllowSetPassword;AuthLogger m_log=new();
 string[] SplitParams(string path)=>(path.StartsWith("/auth")?path.Substring(5):"").Split(new[]{'/','?','&'},StringSplitOptions.RemoveEmptyEntries);
 public static string H(string s)=>Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
 static string P(AuthInfo a){if(a==null)return "null";string s="";foreach(var p in a.ToKeyValuePairs())s+=H(p.Key)+"="+(p.Value==null?"N":p.Value is UUID?"U"+p.Value:"S"+H((string)p.Value))+",";return s;}
 static string R(AuthenticationData a){if(a==null)return "null";string s=a.PrincipalID+":";if(a.Data==null)return s+"null";foreach(var p in a.Data)s+=H(p.Key)+"="+(p.Value==null?"N":p.Value is string?"S"+H((string)p.Value):"I"+p.Value)+",";return s;}
 class Service:IAuthenticationService{
  public string trace="";bool Touch(UUID id){if(id.ToString().EndsWith("9"))throw new Exception();return !id.ToString().EndsWith("8");}
  public string Authenticate(UUID id,string password,int life){trace+="auth:"+id+":"+H(password)+":"+life+";";if(!Touch(id))return "";if(id.ToString().EndsWith("1"))return "\0\u0001\u000b\u001f\ufffe\uffff";return id.ToString().EndsWith("2")?null:"token<&>\r\n\tè©¦é¨“";}
  public string Authenticate(UUID id,string p,int life,out UUID real){real=UUID.Zero;return Authenticate(id,p,life);}
  public bool Verify(UUID id,string token,int life){trace+="verify:"+id+":"+H(token)+":"+life+";";return Touch(id);}
  public bool Release(UUID id,string token){trace+="release:"+id+":"+H(token)+";";return Touch(id);}
  public bool SetPassword(UUID id,string password){trace+="password:"+id+":"+H(password)+";";return Touch(id);}
  public AuthInfo GetAuthInfo(UUID id){trace+="info:"+id+";";if(!Touch(id))return null;var a=new AuthInfo{PrincipalID=id.ToString().EndsWith("4")?new UUID("00000000-0000-0000-0000-000000000008"):id.ToString().EndsWith("5")?new UUID("00000000-0000-0000-0000-000000000009"):id};if(id!=UUID.Zero){a.AccountType="Type<&>";a.PasswordHash="hash\r\n\t";a.PasswordSalt="salt";a.WebLoginKey="è©¦é¨“ ðŸ¦Š";}return a;}
  public bool SetAuthInfo(AuthInfo a){trace+="setinfo:"+P(a)+";";return Touch(a.PrincipalID);}
 }
 public static void Main(){OpenSim.Framework.Culture.SetCurrentCulture();string line;while((line=Console.ReadLine())!=null){var j=JsonDocument.Parse(line).RootElement;var kind=j.GetProperty("op").GetString();string status="OK",value="",trace="";
  if(kind=="endpoint"){var service=new Service();int flags=j.GetProperty("flags").GetInt32();var h=new AuthenticationHandler{m_AuthenticationService=service,m_AllowGetAuthInfo=(flags&1)!=0,m_AllowSetAuthInfo=(flags&2)!=0,m_AllowSetPassword=(flags&4)!=0};try{value=H(Encoding.UTF8.GetString(h.ProcessRequest(j.GetProperty("path").GetString(),new MemoryStream(Convert.FromHexString(j.GetProperty("body").GetString())),new(),new())));}catch{status="EX";}trace=service.trace;}
  else{var h=new AuthInfoReference();int mode=j.GetProperty("mode").GetInt32();if(kind=="get"){if(mode!=0){h.m_Database.row=new AuthenticationData{PrincipalID=new UUID("00000000-0000-0000-0000-000000000011")};if(mode!=1){var d=h.m_Database.row.Data=new();string[] keys={"accountType","passwordHash","passwordSalt","webLoginKey"};for(int i=0;i<4;i++){if(mode==i+3)continue;d[keys[i]]=mode==i+7?(object)17:mode==i+11?null:"value<&>è©¦é¨“\r\n\t";}}}try{value=P(h.GetAuthInfo(UUID.Zero));}catch{status="EX";}trace=h.m_Database.trace;}
   else{h.m_Database.outcome=j.GetProperty("outcome").GetInt32();AuthInfo info=null;if(mode>=0)info=new AuthInfo{PrincipalID=new UUID("00000000-0000-0000-0000-000000000011"),AccountType=(mode&1)!=0?null:"Type",PasswordHash=(mode&2)!=0?null:"hash",PasswordSalt=(mode&4)!=0?null:"salt",WebLoginKey=(mode&8)!=0?null:"<&>è©¦é¨“\r\n\t"};try{value=h.SetAuthInfo(info)?"true":"false";}catch{status="EX";}trace=h.m_Database.trace+R(h.m_Database.stored);}
  }Console.WriteLine(status+"|"+value+"|"+H(trace));
 } }
}
