// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;using System.Globalization;using System.Text;using OpenSim.Framework;using OpenMetaverse;
static class Program {
 static UUID Id(int n)=>new UUID("00000000-0000-0000-0000-"+n.ToString("x12"));
 static string Text(string value)=>value=="-"?null:value=="."?"":Encoding.UTF8.GetString(Convert.FromHexString(value));
 static string Encode(string value)=>value==null?"n":"s"+Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
 static string Bytes(byte[] value)=>value==null?"n":"b"+Convert.ToHexString(value).ToLowerInvariant();
 static int B(bool value)=>value?1:0;
 static string Snapshot(AssetBase a){string result=Bytes(a.Data)+"|"+a.UploadAttempts+"|";
  try{var m=a.Metadata;result+=a.FullID+"|"+Encode(a.ID)+"|"+Encode(a.Name)+"|"+Encode(a.Description)+"|"+Encode(a.CreatorID)+"|"+a.Type+"|"+B(a.Local)+"|"+B(a.Temporary)+"|"+(int)a.Flags+"|"+Encode(m.ContentType)+"|"+m.CreationDate.Ticks+":"+(int)m.CreationDate.Kind+"|"+Bytes(m.SHA1)+"|"+B(a.IsBinaryAsset)+B(a.IsTextualAsset)+B(a.ContainsReferences)+"|"+a.ToString();}catch{result+="METAEX";}return result;
 }
 static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;var asset=new AssetBase();string line;
  while((line=Console.ReadLine())!=null){var p=line.Split(' ');string prefix="";
   try{switch(p[0]){
    case "default":asset=new AssetBase();break;
    case "metadata-default":asset.Metadata=new AssetMetadata();break;
    case "metadata-null":asset.Metadata=null;break;
    case "uuid":asset=new AssetBase(Id(int.Parse(p[1])),Text(p[3]),unchecked((sbyte)int.Parse(p[2])),Text(p[4]));break;
    case "string":asset=new AssetBase(Text(p[1]),Text(p[3]),unchecked((sbyte)int.Parse(p[2])),Text(p[4]));break;
    case "full":asset.FullID=Id(int.Parse(p[1]));break;
    case "id":asset.ID=Text(p[1]);break;
    case "name":asset.Name=Text(p[1]);break;
    case "description":asset.Description=Text(p[1]);break;
    case "creator":asset.CreatorID=Text(p[1]);break;
    case "content":asset.Metadata.ContentType=Text(p[1]);break;
    case "type":asset.Type=unchecked((sbyte)int.Parse(p[1]));break;
    case "flags":asset.Flags=(AssetFlags)int.Parse(p[1]);break;
    case "upload":asset.UploadAttempts=int.Parse(p[1]);break;
    case "local":asset.Local=p[1]!="0";break;
    case "temporary":asset.Temporary=p[1]!="0";break;
    case "date":asset.Metadata.CreationDate=new DateTime(long.Parse(p[1]),(DateTimeKind)int.Parse(p[2]));break;
    case "data":asset.Data=p[1]=="-"?null:p[1]=="."?Array.Empty<byte>():Convert.FromHexString(p[1]);break;
    case "sha":asset.Metadata.SHA1=p[1]=="-"?null:p[1]=="."?Array.Empty<byte>():Convert.FromHexString(p[1]);break;
    case "share":var other=new AssetBase{Metadata=asset.Metadata,Data=asset.Data};other.Name="shared";other.Type=56;if(other.Data!=null&&other.Data.Length>0)other.Data[0]^=255;var sha=other.Metadata.SHA1;if(sha!=null&&sha.Length>0)sha[sha.Length-1]^=255;break;
    case "code":int n=int.Parse(p[1]);prefix="CODE:"+Convert.ToSByte(SLUtil.AssetTypeFromCode(unchecked((sbyte)n)))+":"+SLUtil.SLAssetTypeToContentType(n)+":"+SLUtil.SLInvTypeToContentType(n)+":"+SLUtil.SLAssetTypeToExtension(n)+"|";break;
    case "lookup":prefix="LOOKUP:"+SLUtil.ContentTypeToSLAssetType(Text(p[1]))+":"+SLUtil.ContentTypeToSLInvType(Text(p[1]))+"|";break;
    case "names":prefix="NAMES:"+(sbyte)SLUtil.SLAssetName2Type(Text(p[1]))+":"+(sbyte)SLUtil.SLInvName2Type(Text(p[1]))+"|";break;
    case "limits":prefix="LIMITS:"+AssetBase.MAX_ASSET_NAME+":"+AssetBase.MAX_ASSET_DESC+":"+(int)AssetFlags.Normal+":"+(int)AssetFlags.Maptile+":"+(int)AssetFlags.Rewritable+":"+(int)AssetFlags.Collectable+"|";break;
    default:throw new Exception();
   }}catch{prefix="EX|";}
   Console.WriteLine(prefix+Snapshot(asset));
  }
 }
}
