using System;
using System.Text;
using System.Collections.Generic;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
namespace OpenSim.Framework {public static class CapsUtil {public static Func<string> Generate;public static string GetRandomCapsObjectPath()=>Generate();}public static class Util {public static int UnixTimeSinceEpoch()=>0;}}
namespace OpenSim.Services.Interfaces {public class GridRegion {}}
public partial class FactoryReference {
    public class Config {public List<KeyValuePair<string,string>> values=new();public int reads,keys;public int seed;public string[] GetKeys(){keys++;if(seed%19==0)throw new Exception("keys");var result=new List<string>();foreach(var p in values)result.Add(p.Key);return result.ToArray();}public string GetString(string key,string fallback){reads++;if(seed%17==0)throw new Exception("read");foreach(var p in values)if(p.Key==key)return p.Value;return fallback;}}
    public class Accounts {public int count,seed;public bool StoreUserAccount(UserAccount account){count++;if(seed%23==0)throw new Exception("store");return seed%2==0;}}
    Config m_LoginServerConfig;Accounts m_UserAccountService;string m_GatekeeperURL;static readonly log4net.ILog m_log=log4net.LogManager.GetLogger(typeof(FactoryReference));
    static string Hex(string s)=>Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Urls(Dictionary<string,object> urls){if(urls==null)return "null";string text="";foreach(var p in urls)text+=Hex(p.Key)+"="+(p.Value is string s?"s"+Hex(s):p.Value is int n?"i"+n:"null")+";";return text;}
    public static void Main(string[] args){Culture.SetCurrentCulture();if(args.Length>0&&args[0]=="suffixes"){
        int start=-1,last=-1;for(int n=0;n<=0x10ffff;n++){bool blocked=(n<0xd800||n>0xdfff)&&!("SRV_"+char.ConvertFromUtf32(n)+"Foo").StartsWith("SRV_");if(blocked){if(start<0)start=n;last=n;}else if(start>=0){Console.WriteLine(start.ToString("x")+" "+last.ToString("x"));start=-1;}}if(start>=0)Console.WriteLine(start.ToString("x")+" "+last.ToString("x"));return;
    }string line;while((line=Console.ReadLine())!=null){int seed=int.Parse(line);var account=new UserAccount{PrincipalID=new UUID("00000000-0000-0000-0000-000000000001"),FirstName="First",LastName="Last"};int mode=seed%6;
        if(mode!=0){account.ServiceURLs=new Dictionary<string,object>();if(mode>=2){account.ServiceURLs["z"]="https://old/z";account.ServiceURLs["Foo"]=mode==3?(object)7:mode==4?null:mode==5?"https://new/":"https://old/";account.ServiceURLs["Empty"]="";}}
        var config=new Config{seed=seed};void Add(string k,string v)=>config.values.Add(new KeyValuePair<string,string>(k,v));switch(seed/6%12){case 1:Add("Other","value");break;case 2:Add("SRV_Foo","https://new");Add("SRV_New","https://new/");break;case 3:Add("SRV_Foo","https://new/");break;case 4:Add("SRV_","");Add("srv_Foo","ignored");break;case 5:Add("S\u00adRV_Foo","unicode");break;case 6:Add("SRV_試験","unicode");break;case 7:Add("SRV_GatekeeperURI","configured");break;case 8:Add("SRV_\u0301Foo","combining");break;case 9:Add("SRV\0_Foo","zero");break;case 10:Add("SRV\u034f_Foo","joining");break;case 11:Add("SRV_😀","supplementary");break;}
        var accounts=new Accounts{seed=seed};var f=new FactoryReference{m_LoginServerConfig=config,m_UserAccountService=accounts,m_GatekeeperURL=seed/72%3==0?null:seed/72%3==1?"":"https://gatekeeper"};int caps=0;CapsUtil.Generate=()=>{caps++;if(seed%29==0)throw new Exception("caps");return "00000000-0000-0000-0000-000000001111";};
        var avatar=new AvatarAppearance{Serial=42};avatar.Texture.GetFace(0).TextureID=new UUID("00000000-0000-0000-0000-000000000002");avatar.Texture.GetFace(44).TextureID=new UUID("00000000-0000-0000-0000-000000000003");
        for(int i=0;i<2;i++){string result="EX";try{var c=f.MakeAgent(null,account,(seed/216%2)!=0?avatar:null,new UUID("00000000-0000-0000-0000-000000000004"),new UUID("00000000-0000-0000-0000-000000000005"),uint.MaxValue,new Vector3(128,64,25.5f),"127.0.0.1","1.0","Viewer","mac","id0");result=Convert.ToBase64String(Encoding.UTF8.GetBytes(OSDParser.SerializeLLSDXmlString(c.PackAgentCircuitData(new EntityTransferContext()))));}catch{}Console.Write(result+"|"+accounts.count+","+caps+","+config.keys+","+config.reads+"|"+Urls(account.ServiceURLs));if(i==0)Console.Write('|');}
        Console.WriteLine();
    }}
}
