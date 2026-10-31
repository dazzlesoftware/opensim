// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;using System.Text;using System.Linq;using OpenSim.Framework;
using OpenMetaverse;using OpenMetaverse.StructuredData;
static class Program {
 static UUID Id(int n)=>new UUID("00000000-0000-0000-0000-"+n.ToString("x12"));
 static string Snapshot(AvatarWearable wearable){
  string result=wearable.Count+"|";var packed=(OSDArray)wearable.Pack();if(packed.Count!=wearable.Count)throw new Exception();
  for(int i=0;i<wearable.Count;i++){var item=wearable[i];var row=(OSDMap)packed[i];bool typed=row.Count==2&&row["item"].Type==OSDType.UUID&&row["asset"].Type==OSDType.UUID;
   result+=item.ItemID+":"+item.AssetID+":"+row["item"].AsUUID()+":"+row["asset"].AsUUID()+":"+(typed?"1":"0")+",";
  }
  result+="|";for(int i=0;i<10;i++)result+=wearable.GetAsset(Id(i))+",";
  foreach(int index in new[]{-1,5,99}){var value=wearable[index];result+="|"+value.ItemID+":"+value.AssetID;}return result;
 }
 static void Main(){
  System.Globalization.CultureInfo.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;
  var wearable=new AvatarWearable();string line;
  while((line=Console.ReadLine())!=null){var parts=line.Split(' ',3);string command=parts[0],prefix="";
   try{
    if(command=="add")wearable.Add(Id(int.Parse(parts[1])),Id(int.Parse(parts[2])));
    else if(command=="wear")wearable.Wear(Id(int.Parse(parts[1])),Id(int.Parse(parts[2])));
    else if(command=="wear-item")wearable.Wear(new WearableItem(Id(int.Parse(parts[1])),Id(int.Parse(parts[2]))));
    else if(command=="ctor")wearable=new AvatarWearable(Id(int.Parse(parts[1])),Id(int.Parse(parts[2])));
    else if(command=="remove")wearable.RemoveItem(Id(int.Parse(parts[1])));
    else if(command=="asset")wearable.RemoveAsset(Id(int.Parse(parts[1])));
    else if(command=="clear")wearable.Clear();
    else if(command=="unpack-null")wearable.Unpack(null);
    else if(command=="unpack"||command=="construct"){
     var data=(OSDArray)OSDParser.DeserializeLLSDXml(Encoding.UTF8.GetBytes(line.Substring(line.IndexOf(' ')+1)));
     if(command=="construct")wearable=new AvatarWearable(data);else wearable.Unpack(data);
    }else if(command=="defaults"){
     var first=AvatarWearable.DefaultWearables;var second=AvatarWearable.DefaultWearables;var alias=(AvatarWearable[])first.Clone();first[0].Clear();prefix="DEFAULTS:"+first.Length+":"+(first[0]!=first[1]&&first[0]!=second[0]&&second[0].Count==1&&alias[0].Count==0?"1":"0")+":";
     foreach(var value in second)prefix+=Snapshot(value)+"/";
    }else if(command=="constants"){
     prefix="CONSTANTS:";foreach(int value in new[]{AvatarWearable.BODY,AvatarWearable.SKIN,AvatarWearable.HAIR,AvatarWearable.EYES,AvatarWearable.SHIRT,AvatarWearable.PANTS,AvatarWearable.SHOES,AvatarWearable.SOCKS,AvatarWearable.JACKET,AvatarWearable.GLOVES,AvatarWearable.UNDERSHIRT,AvatarWearable.UNDERPANTS,AvatarWearable.SKIRT,AvatarWearable.MAX_BASICWEARABLES,AvatarWearable.ALPHA,AvatarWearable.TATTOO,AvatarWearable.LEGACY_VERSION_MAX_WEARABLES,AvatarWearable.PHYSICS,AvatarWearable.UNIVERSAL})prefix+=value+",";
     prefix+=AvatarWearable.MAX_WEARABLES_PV7+","+AvatarWearable.MAX_WEARABLES+"|";
     foreach(var value in new[]{AvatarWearable.DEFAULT_BODY_ITEM,AvatarWearable.DEFAULT_BODY_ASSET,AvatarWearable.DEFAULT_HAIR_ITEM,AvatarWearable.DEFAULT_HAIR_ASSET,AvatarWearable.DEFAULT_SKIN_ITEM,AvatarWearable.DEFAULT_SKIN_ASSET,AvatarWearable.DEFAULT_EYES_ITEM,AvatarWearable.DEFAULT_EYES_ASSET,AvatarWearable.DEFAULT_SHIRT_ITEM,AvatarWearable.DEFAULT_SHIRT_ASSET,AvatarWearable.DEFAULT_PANTS_ITEM,AvatarWearable.DEFAULT_PANTS_ASSET,AvatarWearable.DEFAULT_ALPHA_ITEM,AvatarWearable.DEFAULT_ALPHA_ASSET,AvatarWearable.DEFAULT_TATTOO_ITEM,AvatarWearable.DEFAULT_TATTOO_ASSET})prefix+=value+",";
    }else if(command=="limits"){
     AvatarWearable.MAX_WEARABLES=int.Parse(parts[1]);AvatarWearable.MAX_WEARABLES_PV7=int.Parse(parts[2]);prefix="LIMITS:"+AvatarWearable.MAX_WEARABLES+":"+AvatarWearable.MAX_WEARABLES_PV7+":";
    }else throw new Exception();
   }catch{prefix="EX:";}
   Console.WriteLine(prefix+Snapshot(wearable));
  }
 }
}
