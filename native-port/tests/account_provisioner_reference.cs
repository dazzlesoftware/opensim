using System;using System.Text;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Services.Interfaces;using OpenSim.Framework;
namespace OpenSim.Framework{public static class Util{public static int UnixTimeSinceEpoch()=>123;}}
namespace OpenSim.Services.Interfaces{public class GridRegion{public UUID RegionID;}}
public class Logger{public void Error(string s){}public void Warn(string s){}public void Debug(string s){}public void Info(string s){}}
public class PasswordService{public Func<UUID,string,bool> call;public bool SetPassword(UUID id,string password)=>call(id,password);}
public class GridService{public Func<UUID,List<GridRegion>> call;public List<GridRegion> GetDefaultRegions(UUID scope)=>call(scope);}
public class HomeService{public Func<string,UUID,Vector3,Vector3,bool> call;public bool SetHome(string id,UUID region,Vector3 position,Vector3 look)=>call(id,region,position,look);}
public class InventoryService{public Func<UUID,bool> call;public bool CreateUserInventory(UUID id)=>call(id);}
public partial class ProvisionerReference{
    int fault;string trace="";bool m_CreateDefaultAvatarEntries;Logger m_log=new Logger();PasswordService m_AuthenticationService;GridService m_GridService;HomeService m_GridUserService;InventoryService m_InventoryService;
    static UUID seven=new UUID("00000000-0000-0000-0000-000000000007"),eight=new UUID("00000000-0000-0000-0000-000000000008"),five=new UUID("00000000-0000-0000-0000-000000000005");
    static string Snapshot(UserAccount a){string s=a.PrincipalID+":"+a.ScopeID+":"+(a.FirstName??"_")+":"+(a.LastName??"_")+":"+(a.Email??"_")+":"+a.Created+":";foreach(var p in a.ServiceURLs)s+=p.Key+"="+p.Value+",";return s;}
    UserAccount GetUserAccount(UUID scope,string first,string last){trace+="lookup:"+scope+":"+first+":"+last+";";if(fault==1)throw new Exception();return fault==16?new UserAccount():null;}
    bool StoreUserAccount(UserAccount a){trace+="store:"+Snapshot(a)+";";if(fault==3)throw new Exception();if(fault==17){a.PrincipalID=eight;a.FirstName="Changed";}return fault!=2;}
    void CreateDefaultAppearanceEntries(UUID id){trace+="default:"+id+";";if(fault==14)throw new Exception();}
    void EstablishAppearance(UUID id,string model){trace+="model:"+id+":"+model+";";if(fault==15)throw new Exception();}
    public static void Main(){Culture.SetCurrentCulture();string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');int mask=int.Parse(p[0]),fault=int.Parse(p[1]),variant=int.Parse(p[2]);var owner=new ProvisionerReference{fault=fault,m_CreateDefaultAvatarEntries=(mask&16)!=0};
        if((mask&1)!=0)owner.m_AuthenticationService=new PasswordService{call=(id,password)=>{owner.trace+="password:"+id+":"+(password??"_")+";";if(fault==5)throw new Exception();return fault!=4;}};
        if((mask&2)!=0)owner.m_GridService=new GridService{call=scope=>{owner.trace+="grid:"+scope+";";if(fault==7)throw new Exception();if(fault==6)return null;return fault==8?new List<GridRegion>{null}:new List<GridRegion>{new GridRegion{RegionID=five},null};}};
        if((mask&4)!=0)owner.m_GridUserService=new HomeService{call=(id,region,position,look)=>{owner.trace+="home:"+id+":"+region+":"+(int)position.X+","+(int)position.Y+","+(int)position.Z+":"+(int)look.X+","+(int)look.Y+","+(int)look.Z+";";if(fault==11)throw new Exception();return fault!=10;}};
        if((mask&8)!=0)owner.m_InventoryService=new InventoryService{call=id=>{owner.trace+="inventory:"+id+";";if(fault==13)throw new Exception();return fault!=12;}};
        string first=" First ",last=" Last ",password="secret",email="mail",model=(mask&32)!=0?"Model Avatar":"";
        if(variant==1)first=null;if(variant==2)last=null;if(variant==3)model=null;if(variant==4){first="\u00a0試験\u2003";last="";}if(variant==5)email=null;if(variant==6)password=null;
        string result="null";try{var account=owner.CreateUser(five,seven,first,last,password,email,model);if(account!=null)result=Snapshot(account);}catch{result="EX";}
        Console.WriteLine(Convert.ToHexString(Encoding.UTF8.GetBytes(result)).ToLowerInvariant()+"|"+Convert.ToHexString(Encoding.UTF8.GetBytes(owner.trace)).ToLowerInvariant());
    }}
}
