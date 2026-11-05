using System;using System.Collections.Generic;using System.Globalization;using OpenMetaverse;using OpenSim.Framework;
partial class Rpc{partial void Update(ref object data,string method,string uri,string id,ref bool? handled){if(method!="classified_update")return;int previous=F.Mode;F.Mode=UpdateFixture.TypedMode;try{handled=InfoFixture.Rpc(ref data,method,uri,id);}finally{F.Mode=previous;}}}
public class UpdateAgent:ProfileFixtureAgent{public override Vector3 AbsolutePosition{get{UpdateFixture.Fail(16,"position");return new Vector3(10,20,30);}}}
class UpdateLand{public UpdateLand LandData=>(UpdateFixture.Seams&256)!=0?null:this;public string Name{get{F.Trace+="landname;";return F.Text;}}}
class UpdateMoney{public bool AmountCovered(UUID agent,int price){F.Trace+="covered:"+agent+":"+price+";";UpdateFixture.Fail(4096,"covered");return (UpdateFixture.Seams&2048)==0;}public void ApplyCharge(UUID agent,int price,MoneyTransactionType type){F.Trace+="charge:"+agent+":"+price+":"+(int)type+";";UpdateFixture.Fail(8192,"charge");}}
class UpdateScene:ProfileFixtureScene{
 public UpdateScene LandChannel=>(UpdateFixture.Seams&32)!=0?null:this;
 public UpdateLand GetLandObject(float x,float y){F.Trace+="land:"+InfoFixture.Vector(new Vector3(x,y,0))+";";UpdateFixture.Fail(64,"land");return (UpdateFixture.Seams&128)!=0?null:new UpdateLand();}
 public T RequestModuleInterface<T>()where T:class{F.Trace+="money;";UpdateFixture.Fail(1024,"money");return (UpdateFixture.Seams&512)!=0?null:new UpdateMoney() as T;}
 public override ProfileFixtureScene RegionInfo=>(UpdateFixture.Seams&16384)!=0?null:this;
 public override string RegionName{get{F.Trace+="region;";UpdateFixture.Fail(32768,"region");return F.Text;}}
}
class UpdateClient:InfoClient{
 int scenes;public override ProfileFixtureScene Scene{get{F.Trace+="scene;";UpdateFixture.Fail(2,"scene");return (UpdateFixture.Seams&1)!=0||((UpdateFixture.Seams&65536)!=0&&scenes++>0)?null:new UpdateScene();}}
 public override ProfileFixtureAgent SceneAgent{get{F.Trace+="sceneagent;";UpdateFixture.Fail(8,"sceneagent");return (UpdateFixture.Seams&4)!=0?null:new UpdateAgent();}}
 public override void SendAvatarClassifiedReply(UUID id,Dictionary<UUID,string> list){F.Trace+="classifieds:"+id+":"+PicksFixture.Dump(list)+":"+PicksFixture.Identity(list)+";";F.Fail(16384,"classifieds");}
}
static class UpdateFixture{
 public static int Seams,TypedMode;public static void Fail(int mask,string message){if((Seams&mask)!=0)throw new Exception(message);}
 static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');F.Mode=int.Parse(p[0]);TypedMode=int.Parse(p[1]);F.Flags=int.Parse(p[2]);Seams=int.Parse(p[3]);F.Uri=PicksFixture.Decode(p[4]);F.Text=PicksFixture.Decode(p[5]);uint category=uint.Parse(p[6]),estate=uint.Parse(p[7]);int price=int.Parse(p[8]);string position=PicksFixture.Decode(p[9]);Vector3.TryParse(position,out var vector);F.Trace="";InfoFixture.Last=null;InfoFixture.Position=position;PicksFixture.Reset();var m=new Original();PicksFixture.Current=m;ClassifiedsFixture.Current=m;IClientAPI client=(F.Flags&1)!=0?null:new UpdateClient();var output=new List<string>();foreach(var token in p[10..]){string result="OK";try{switch(token){case "U":m.ClassifiedInfoUpdate(F.Id(3),category,F.Text,F.Text,F.Id(99),estate,F.Id(9),vector,128,price,client);break;case "C":m.Cache.Clear();break;case "M":TypedMode=0;break;case "R":F.Mode=12;break;default:var e=new UserProfileCacheEntry{flags=55,born="seed",picksList=new Dictionary<UUID,string>{{F.Id(7),"other"}},classifiedsLists=new Dictionary<UUID,string>{{F.Id(3),"cached"}},classifieds=new Dictionary<UUID,UserClassifiedAdd>{{F.Id(3),InfoFixture.Fill(F.Id(3))}}};m.Cache.AddOrUpdate(token=="B"?F.Id(2):F.Id(1),e,300.0);m.Owners[F.Id(3)]=F.Id(2);m.Interests[F.Id(3)]=2;break;}}catch(Exception error){result="EX:"+F.H(error.Message);}output.Add(result+":"+InfoFixture.State(m));}Console.WriteLine(string.Join('/',output)+"|"+F.H(F.Trace));}}
}
