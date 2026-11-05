using System;using System.Collections.Generic;using System.Globalization;using System.Reflection;using OpenMetaverse;using OpenSim.Framework;
partial class Rpc{partial void PickEdit(ref object data,string method,string uri,string id,ref bool? handled){if(data is not UserProfilePick)return;int previous=F.Mode;F.Mode=method=="picks_update"?EditFixture.UpdateMode:EditFixture.GetMode;try{handled=PickFixture.Rpc(ref data,method,uri,id);}finally{F.Mode=previous;}}}
partial class World{partial void TransformPresence(ref Presence value){value=new EditPresence{IsChildAgent=value.IsChildAgent,IsNPC=value.IsNPC,currentParcelUUID=value.currentParcelUUID};}}
partial class Grid{public string GateKeeperURLNoEndSlash{get{F.Trace+="gatekeeper;";EditFixture.Fail(8192,"gatekeeper");return PickFixture.Gate;}}}
partial class Log{public void WarnFormat(string format,float x,float y,string scene){F.Trace+="warn:"+F.T(format)+":"+EditFixture.Float(x)+":"+EditFixture.Float(y)+":"+F.T(scene)+";";EditFixture.Fail(16384,"warn");}}
class EditPresence:Presence{
 public override bool IsViewerUIGod{get{F.Trace+="god;";EditFixture.Fail(1,"god");return (EditFixture.Seams&2)!=0;}}
 public override Vector3 AbsolutePosition{get{F.Trace+="position;";EditFixture.Fail(4,"position");return EditFixture.Position;}}
 public override ProfileFixtureScene Scene{get{F.Trace+="pscene;";EditFixture.Fail(8,"pscene");return (EditFixture.Seams&16)!=0?null:new EditScene(true);}}
}
class EditLand:ProfileFixtureLand{public override ProfileFixtureLand LandData=>(EditFixture.Seams&256)!=0?null:this;public override string Name{get{F.Trace+="landname;";EditFixture.Fail(512,"landname");return F.Text;}}}
class EditScene:ProfileFixtureScene{
 bool presence;public EditScene(bool p){presence=p;}
 public override ProfileFixtureScene RegionInfo=>(EditFixture.Seams&32)!=0?null:this;
 public override uint WorldLocX{get{F.Trace+="worldx;";EditFixture.Fail(64,"worldx");return EditFixture.WorldX;}}
 public override uint WorldLocY{get{F.Trace+="worldy;";EditFixture.Fail(128,"worldy");return EditFixture.WorldY;}}
 public override ulong RegionHandle{get{F.Trace+="handle;";EditFixture.Fail(1024,"handle");return ((ulong)EditFixture.WorldX<<32)|EditFixture.WorldY;}}
 public override ProfileFixtureScene LandChannel=>(EditFixture.Seams&2048)!=0?null:this;
 public override ProfileFixtureLand GetLandObject(float x,float y){F.Trace+="land:"+(presence?1:0)+":"+EditFixture.Float(x)+":"+EditFixture.Float(y)+";";EditFixture.Fail(4096,"land");return (EditFixture.Seams&32768)!=0?null:new EditLand();}
 public override string Name{get{F.Trace+="scenename;";EditFixture.Fail(65536,"scenename");return F.Text;}}
 public override string RegionName{get{F.Trace+="regionname;";EditFixture.Fail(131072,"regionname");return F.Text;}}
}
class EditClient:PickClient{
 public override ProfileFixtureScene Scene{get{F.Trace+="scene;";EditFixture.Fail(262144,"scene");return (EditFixture.Seams&524288)!=0?null:new EditScene(false);}}
 public override void SendAvatarPicksReply(UUID target,Dictionary<UUID,string> list){base.SendAvatarPicksReply(target,list);if((EditFixture.Seams&1048576)!=0&&PicksFixture.Current.Cache.TryGetValue(EditFixture.Creator,out var e)&&e?.picks!=null)foreach(var pick in e.picks.Values)if(pick!=null){pick.Name="callback-edit";pick.GlobalPos="<9,8,7>";}if((EditFixture.Seams&2097152)!=0)PicksFixture.Current.Cache.Clear();}
}
static class EditFixture{
 public static int Seams,UpdateMode,GetMode;public static uint WorldX,WorldY;public static UUID Creator;public static Vector3 Position;
 public static string Float(float value)=>BitConverter.SingleToUInt32Bits(value).ToString("x8");public static void Fail(int mask,string message){if((Seams&mask)!=0)throw new Exception(message);}
 static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');F.Mode=int.Parse(p[0]);GetMode=int.Parse(p[1]);UpdateMode=int.Parse(p[2]);F.Flags=int.Parse(p[3]);Seams=int.Parse(p[4]);WorldX=uint.Parse(p[5]);WorldY=uint.Parse(p[6]);F.Uri=PicksFixture.Decode(p[7]);UUID.TryParse(PicksFixture.Decode(p[8]),out Creator);F.Text=PicksFixture.Decode(p[9]);Vector3.TryParse(PicksFixture.Decode(p[10]),out Position);PickFixture.Position=PicksFixture.Decode(p[11]);PickFixture.Gate=PicksFixture.Decode(p[12]);PickFixture.Seams=0;PickFixture.Local=1;PickFixture.X=512;PickFixture.Y=768;PickFixture.Target=Creator;PickFixture.Query=F.Id(3);PickFixture.Parcel=F.Id(5);F.AssetUri="http://assets/";F.Trace="";PickFixture.Last=null;PickFixture.Saved=null;PicksFixture.Reset();var m=new Original();PicksFixture.Current=m;IClientAPI client=(F.Flags&1)!=0?null:new EditClient();var output=new List<string>();foreach(var token in p[13..]){string result="OK";try{if(token[0]=='t')PicksFixture.Now=double.Parse(token[1..],CultureInfo.InvariantCulture);else switch(token){case "U":m.PickInfoUpdate(client,F.Id(3),Creator,true,F.Text,F.Text,F.Id(9),int.MinValue,true);break;case "G":PickFixture.Saved=m.GetPick(Creator,F.Id(3));break;case "C":m.Cache.Clear();break;case "Z":UpdateMode=0;break;case "Y":UpdateMode=8;break;case "R":F.Mode=12;break;case "P":m.Cache.GetType().GetMethod("Purge",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(m.Cache,new object[]{null});break;case "N":m.Cache.AddOrUpdate(Creator,null,300.0);break;default:var e=new UserProfileCacheEntry{flags=55,born="seed",classifiedsLists=new Dictionary<UUID,string>{{F.Id(9),"ad"}},props=new UserProfileProperties{UserId=F.Id(7)}};if(token!="L"){e.picksList=new();e.picks=new();if(token=="A"||token=="D"||token=="E"){e.picksList[F.Id(3)]="existing";var seed=PickFixture.Fill(F.Id(3));if(token=="D")seed.GlobalPos="bad";e.picks[F.Id(3)]=token=="E"?null:seed;}if(token=="F"||token=="B")for(int i=0;i<(token=="F"?20:19);i++)e.picksList[F.Id(100+i)]="row";}m.Cache.AddOrUpdate(Creator,e,300.0);break;}}catch(Exception error){if(error is TargetInvocationException&&error.InnerException!=null)error=error.InnerException;result="EX:"+F.H(error.Message);}output.Add(result+":"+PickFixture.State(m));}Console.WriteLine(string.Join('/',output)+"|"+F.H(F.Trace));}}
}
