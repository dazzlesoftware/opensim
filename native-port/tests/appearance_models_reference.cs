// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;using System.Globalization;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenSim.Framework;
static class Program {
 static UUID Id(int n)=>new UUID("00000000-0000-0000-0000-"+n.ToString("x12"));
 static string Bits(float value)=>BitConverter.SingleToInt32Bits(value).ToString("x8");
 static string Snapshot(AvatarAttachment attachment,EntityTransferContext context){
  var a=attachment.Pack();var c=(OSDMap)context.Pack();
  bool aTypes=a.Count==3&&a["point"].Type==OSDType.Integer&&a["item"].Type==OSDType.UUID&&a["asset"].Type==OSDType.UUID;
  bool cTypes=c.Count==3&&c["InboundVersion"].Type==OSDType.Real&&c["OutboundVersion"].Type==OSDType.Real&&c["WearablesCount"].Type==OSDType.Integer;
  return attachment.AttachPoint+"|"+attachment.ItemID+"|"+attachment.AssetID+"|"+(aTypes?1:0)+":"+a["point"].AsInteger()+":"+a["item"].AsUUID()+":"+a["asset"].AsUUID()+"|"+Bits(context.InboundVersion)+":"+Bits(context.OutboundVersion)+":"+context.WearablesCount+"|"+(cTypes?1:0)+":"+Bits((float)c["InboundVersion"].AsReal())+":"+Bits((float)c["OutboundVersion"].AsReal())+":"+c["WearablesCount"].AsInteger();
 }
 static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;var attachment=new AvatarAttachment(17,Id(1),Id(2));var context=new EntityTransferContext();string line;
  while((line=Console.ReadLine())!=null){try{
   int space=line.IndexOf(' ');string command=space<0?line:line.Substring(0,space),text=space<0?"":line.Substring(space+1),aStatus="AOK",cStatus="COK";
   if(command=="reset"){attachment=new AvatarAttachment(17,Id(1),Id(2));context=new EntityTransferContext();}
   else if(command=="copy"){var previous=attachment;attachment=new AvatarAttachment(previous);++previous.AttachPoint;}
   else{
    OSD a=new OSDMap(),c=new OSDMap();
    if(command=="wrong")a=c=new OSDArray();
    else if(command=="missing"){((OSDMap)a)["item"]=OSD.FromUUID(Id(1));((OSDMap)c)["OutboundVersion"]=OSD.FromReal(.55);}
    else if(command=="set"||command=="partial"||command=="ctor"){
     var value=FixtureValue.Build(text);((OSDMap)a)["point"]=value;((OSDMap)c)["InboundVersion"]=value;
     if(command!="partial"){((OSDMap)a)["item"]=value;((OSDMap)a)["asset"]=value;((OSDMap)c)["OutboundVersion"]=value;((OSDMap)c)["WearablesCount"]=value;}
    }
    try{if(command=="null")attachment.Unpack(null);else if(command=="ctor")attachment=new AvatarAttachment((OSDMap)a);else attachment.Unpack((OSDMap)a);}catch{aStatus="AEX";}
    try{if(command=="null")context.Unpack(null);else{if(command=="ctor")context=new EntityTransferContext();context.Unpack(c);}}catch{cStatus="CEX";}
   }
   Console.WriteLine(aStatus+"|"+cStatus+"|"+Snapshot(attachment,context));
  }catch{Console.WriteLine("EX");}}
 }
}
