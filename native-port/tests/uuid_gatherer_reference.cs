using System;using System.Collections.Generic;using System.Linq;using System.Text;using OpenMetaverse;
class AssetBase {public byte[] Data;public sbyte Type;public int Tag;}
interface IAssetService{AssetBase Get(string id);}
class Service:IAssetService{public AssetBase Get(string id){var uuid=new UUID(id);Probe.Log("G:"+Probe.Label(uuid));if(ProbeScene.Active)ProbeScene.Hit(50,Probe.TryNumber(uuid,out var tag)?tag:0);Probe.Hook(1);if(!Probe.TryNumber(uuid,out int n))return null;if(!Probe.Assets.TryGetValue(n,out var p)||p.Mode==0)return null;if(p.Mode==1)throw new Exception("fault");return new(){Tag=n,Type=p.Type,Data=p.Mode==2?null:p.Bytes??new byte[p.Length]};}}
class Logger{public void Error(string s){Probe.Log("E:"+s);ProbeLive.Hit(25,0);Probe.Hook(3);}public void Debug(string s){Probe.Log("D:"+s);Probe.Hook(4);}public void Warn(string s)=>ProbeLive.Warn(s);public void WarnFormat(string s,params object[] args)=>ProbeLive.Warn(string.Format(System.Globalization.CultureInfo.InvariantCulture,s,args));}
static class Constants{public static UUID DefaultMaterialID=new("968cbad0-4dad-d64e-71b5-72bf13ad051a");}
partial class UuidGatherer {
 static Logger m_log=new();
 void RecordWearableAssetUuids(AssetBase a)=>Probe.Inspect(this,a,"wearable");
 void RecordGestureAssetUuids(AssetBase a)=>Probe.Inspect(this,a,"gesture");
 void RecordNoteCardEmbeddedAssetUuids(AssetBase a)=>Probe.Inspect(this,a,"notecard");
 void RecordMaterialAssetUuids(AssetBase a)=>Probe.Inspect(this,a,"material");
 void RecordSceneObjectAssetUuids(AssetBase a)=>Probe.Inspect(this,a,"object");
 void RecordEmbeddedAssetDataUuids(AssetBase a){Probe.Inspect(this,a,"embedded");if(Probe.Scan)RecordEmbeddedAssetDataUuids(new osUTF8(Probe.Bytes));}
 public string ToSkipContains(string value)=>ToSkip.Contains(new UUID(value))?"1":"0";
 public void Typed(UUID id,sbyte type)=>AddForInspection(id,type);
 public void ScanBytes(byte[] bytes)=>RecordEmbeddedAssetDataUuids(new osUTF8(bytes));
 public void ScanChars(string value)=>RecordEmbeddedAssetDataUuids(value.AsSpan());
 public void Counters(int e,int a,int p){ErrorCount=e;AssetGetCount=a;possibleNotAssetCount=p;}
 public string Snapshot()=>string.Join(",",GatheredUuids.OrderBy(k=>k.Key.ToString(),StringComparer.Ordinal).Select(k=>Probe.Label(k.Key)+":"+k.Value))+"|"+string.Join(",",FailedUUIDs.Select(Probe.Label))+"|"+string.Join(",",UncertainAssetsUUIDs.Select(Probe.Label))+"|"+ErrorCount+":"+AssetGetCount+":"+possibleNotAssetCount+":"+(verbose?1:0)+"|"+string.Join(",",m_assetUuidsToInspect.Select(Probe.Label));
}
class Profile{public int Mode,Length;public byte[] Bytes;public sbyte Type;public List<(int Child,sbyte Type,int Mode)> Edges=new();}
static class Probe {
 public static Dictionary<int,Profile> Assets=new();public static bool Scan,Formats;public static byte[] Bytes=Array.Empty<byte>();static List<string> trace=new();static int point,nth,seen,selected;static UuidGatherer[] gatherers;static Dictionary<UUID,sbyte> collected;static HashSet<UUID> failed,uncertain;
 public static UUID Id(int n)=>n==-1?Constants.DefaultMaterialID:n==-2?new UUID("11111111-1111-0000-0000-000100bba000"):n==-3?new UUID("ffffffff-ffff-ffff-ffff-ffffffffffff"):new UUID("00000000-0000-0000-0000-"+n.ToString("x12"));
 public static int Number(UUID id){if(id==Id(-1))return -1;if(id==Id(-2))return -2;if(id==Id(-3))return -3;int n=Convert.ToInt32(id.ToString().Substring(24),16);if(id!=Id(n))throw new Exception("Full UUID");return n;}
 public static bool TryNumber(UUID id,out int number){try{number=Number(id);return true;}catch{number=0;return false;}}
 public static string Label(UUID id)=>TryNumber(id,out int n)?n.ToString():id.ToString();
 public static void Log(string s)=>trace.Add(s);public static void Hook(int p){if(point==p&&++seen==nth)throw new Exception("fault");}
 public static void Inspect(UuidGatherer g,AssetBase a,string name){if(Formats){if(name=="embedded")g.ScanBytes(a.Data);else new AssetFormatReference(g).Call(name=="wearable"?0:name=="gesture"?1:name=="material"?2:name=="notecard"?3:4,a.Data);return;}Log("I:"+name+":"+a.Tag);Hook(2);foreach(var edge in Assets[a.Tag].Edges){var id=Id(edge.Child);switch(edge.Mode){case 0:g.AddForInspection(id);break;case 1:g.Typed(id,edge.Type);break;case 2:g.AddGathered(id,edge.Type);break;case 3:g.UncertainAssetsUUIDs.Add(id);g.AddForInspection(id);break;case 4:throw new Exception("fault");}}}
 static string Execute(string line){var p=line.Split(' ');int n=p.Length>1&&p[0]!="scan"&&p[0]!="chars"&&p[0]!="skip"&&p[0]!="index"?int.Parse(p[1]):0;var g=gatherers?[selected];switch(p[0]){
 case "init":Assets.Clear();point=seen=selected=0;nth=1;Scan=Formats=false;Bytes=Array.Empty<byte>();collected=new();failed=new();uncertain=new();gatherers=new[]{new UuidGatherer(new Service(),collected,failed,uncertain),new UuidGatherer(new Service(),collected,failed,uncertain)};return "init";
 case "select":selected=n;return "select";
 case "fault":point=n;nth=int.Parse(p[2]);seen=0;return "fault";
 case "asset":Assets[n]=new Profile{Mode=int.Parse(p[2]),Type=unchecked((sbyte)int.Parse(p[3])),Length=int.Parse(p[4])};return "asset";
 case "formats":Formats=n!=0;return "formats";
 case "raw":Assets[n].Bytes=p[2]=="-"?null:p[2]=="."?Array.Empty<byte>():Convert.FromHexString(p[2]);Assets[n].Length=Assets[n].Bytes?.Length??0;return "raw";
 case "edge":Assets[n].Edges.Add((int.Parse(p[2]),unchecked((sbyte)int.Parse(p[3])),int.Parse(p[4])));return "edge";
 case "add":return g.AddForInspection(Id(n))?"1":"0";
 case "known":return g.AddGathered(Id(n),unchecked((sbyte)int.Parse(p[2])))?"1":"0";
 case "typed":g.Typed(Id(n),unchecked((sbyte)int.Parse(p[2])));return "typed";
 case "failed":failed.Add(Id(n));return "failed";
 case "uncertain":uncertain.Add(Id(n));return "uncertain";
 case "unfail":failed.Remove(Id(n));return "unfail";
 case "uncollect":collected.Remove(Id(n));return "uncollect";
 case "clear-failed":failed.Clear();return "clear-failed";
 case "next":return g.GatherNext()?"1":"0";
 case "all":return g.GatherAll(n!=0)?"1":"0";
 case "skip":return g.ToSkipContains(p[1]);
 case "peek":return g.NextUuidToInspect.HasValue?Label(g.NextUuidToInspect.Value):"-";
 case "count":g.Counters(n,int.Parse(p[2]),int.Parse(p[3]));return "count";
 case "order":return string.Join(",",g.GatheredUuids.Keys.Select(Label));
 case "scene":return ProbeScene.Call(n,int.Parse(p[2]),int.Parse(p[3]));
 case "live":return ProbeLive.Call(g,n,int.Parse(p[2]),int.Parse(p[3]),int.Parse(p[4]));
 case "texture":{var entry=new Primitive.TextureEntry(Id(1));entry.DefaultTexture.MaterialID=Id(2);if((n&1)!=0){var face=entry.CreateFace(0);face.TextureID=Id(3);face.MaterialID=Id(4);}if((n&2)!=0)entry.CreateFace(5).TextureID=Id(5);if((n&4)!=0)entry.DefaultTexture.TextureID=Id(-2);return Convert.ToHexString(entry.GetBytes()).ToLowerInvariant();}
 case "index":return Decode16(p[1]).IndexOf(Decode16(p[2]),StringComparison.InvariantCulture).ToString();
 case "format":new AssetFormatReference(g).Call(n,p[2]=="-"?null:p[2]=="."?Array.Empty<byte>():Convert.FromHexString(p[2]));return "format";
 case "scan":g.ScanBytes(p[1]=="-"?Array.Empty<byte>():Convert.FromHexString(p[1]));return "scan";
 case "chars":g.ScanChars(p[1]=="-"?"":Encoding.Unicode.GetString(Convert.FromHexString(p[1])));return "chars";
 }throw new Exception("command");}
 static string Decode16(string hex){if(hex=="-")return "";var b=Convert.FromHexString(hex);var chars=new char[b.Length/2];for(int i=0;i<chars.Length;i++)chars[i]=(char)(b[2*i]|(b[2*i+1]<<8));return new string(chars);}
 public static void Main(){System.Globalization.CultureInfo.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;string line;Execute("init");while((line=Console.ReadLine())!=null){trace.Clear();string result;try{result=Execute(line);}catch{result="EX";}Console.WriteLine(result+"|"+Convert.ToHexString(Encoding.UTF8.GetBytes(string.Join(";",trace))).ToLowerInvariant()+"|"+gatherers[selected].Snapshot());}}
}

partial class AssetFormatReference {
 UuidGatherer gatherer;public AssetFormatReference(UuidGatherer g){gatherer=g;}
 IDictionary<UUID,sbyte> GatheredUuids=>gatherer.GatheredUuids;
 HashSet<UUID> UncertainAssetsUUIDs=>gatherer.UncertainAssetsUUIDs;
 void AddForInspection(UUID id)=>gatherer.AddForInspection(id);
 void AddForInspection(UUID id,sbyte type)=>gatherer.Typed(id,type);
 HashSet<UUID> FailedUUIDs=>gatherer.FailedUUIDs;
 void RecordEmbeddedAssetDataUuids(osUTF8 data)=>gatherer.ScanBytes(data.ToArray());
 public void Call(int kind,byte[] bytes){var a=new AssetBase{Data=bytes};switch(kind){case 0:RecordWearableAssetUuids(a);break;case 1:RecordGestureAssetUuids(a);break;case 2:RecordMaterialAssetUuids(a);break;case 3:RecordNoteCardEmbeddedAssetUuids(a);break;case 4:RecordSceneObjectAssetUuids(a);break;}}
}
