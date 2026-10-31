using System;using System.Text;using System.Collections.Generic;using System.Web;using OpenSim.Framework;using OpenSim.Data;using OpenSim.Services.Interfaces;
namespace OpenSim.Framework {public static class Util {public static int UnixTimeSinceEpoch()=>0;}}
public partial class AccountUrlReference {
    class Database {public string encoded;public bool Store(UserAccountData data){encoded=data.Data["ServiceURLs"];return true;}}
    class Callback {public Func<string> text;public override string ToString()=>text();}
    Database m_Database=new Database();
    static string Hex(string s)=>Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Snapshot(Dictionary<string,object> urls){if(urls==null)return "null";string s="";foreach(var p in urls)s+=Hex(p.Key)+":"+(p.Value is string str?"s"+Hex(str):p.Value==null?"null":"object")+";";return s;}
    public static void Main(){Culture.SetCurrentCulture();string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');string op=p[0],arg=p.Length>1?p[1]:".",status="OK",value="-";UserAccount account=new UserAccount();var owner=new AccountUrlReference();
        try{
            if(op=="codec"){string raw=arg=="."?"":Encoding.UTF8.GetString(Convert.FromHexString(arg));value=Hex(HttpUtility.UrlEncode(raw))+"/"+Hex(HttpUtility.UrlDecode(raw));}
            else if(op=="decode"){string raw=arg=="."?"":Encoding.UTF8.GetString(Convert.FromHexString(arg));account=owner.MakeUserAccount(new UserAccountData{Data=new Dictionary<string,string>{{"Created","0"},{"ServiceURLs",raw}}});owner.StoreUserAccount(account);value=Hex(owner.m_Database.encoded);}
            else if(op=="objects"){
                int mode=int.Parse(arg);if(mode!=0)account.ServiceURLs=new Dictionary<string,object>();var urls=account.ServiceURLs;
                if(mode==1)urls["null"]=null;
                if(mode==2){urls["z &=試験"]="https://host/a?x=1&y=two words";urls[""]="";}
                if(mode==3){urls["integer"]=7;urls["bool"]=true;urls["double"]=1e22;int count=0;urls["counter"]=new Callback{text=()=> (++count).ToString()};}
                if(mode==4){urls["before"]="first";urls["remove"]=new Callback{text=()=>{urls.Remove("before");return "middle";}};urls["after"]="last";}
                if(mode==5)urls["add"]=new Callback{text=()=>{urls["added"]="value";return "value";}};
                if(mode==6){urls["change"]=new Callback{text=()=>{account.ServiceURLs=new Dictionary<string,object>{{"new","value"}};return "value";}};urls["after"]="value";}
                owner.StoreUserAccount(account);value=Hex(owner.m_Database.encoded);
            }
        }catch{status="EX";}
        Console.WriteLine(status+"|"+value+"|"+Snapshot(account.ServiceURLs));
    }}
}
