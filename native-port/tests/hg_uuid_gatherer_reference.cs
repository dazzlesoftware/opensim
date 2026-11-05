using System;using System.Collections.Generic;using System.Linq;using System.Text;using OpenMetaverse;
class AssetBase{public byte[] Data;public sbyte Type;public int Tag;}
interface IAssetService{AssetBase Get(string id);AssetBase Get(string id,string url,bool local);}
class Service:IAssetService{public AssetBase Get(string id)=>Probe.Get(new UUID(id),false,null,false);public AssetBase Get(string id,string url,bool local)=>Probe.Get(new UUID(id),true,url,local);}
class Logger{public void Error(string text){Probe.Log("E:"+text);Probe.Hook(5);}public void Debug(string text){bool hg=text.StartsWith("[HGUUIDGatherer]");Probe.Log((hg?"H:":"B:")+text);Probe.Hook(hg?4:6);}}
static class WebUtil{public static int DebugLevel{get{Probe.Log("D:"+Probe.Level);Probe.Hook(3);return Probe.Level;}}}
static class Constants{public static UUID DefaultMaterialID=new("968cbad0-4dad-d64e-71b5-72bf13ad051a");}
partial class UuidGatherer{
 static Logger m_log=new();
 void RecordWearableAssetUuids(AssetBase a)=>Probe.Inspect(this,a);void RecordGestureAssetUuids(AssetBase a)=>Probe.Inspect(this,a);void RecordNoteCardEmbeddedAssetUuids(AssetBase a)=>Probe.Inspect(this,a);void RecordMaterialAssetUuids(AssetBase a)=>Probe.Inspect(this,a);void RecordSceneObjectAssetUuids(AssetBase a)=>Probe.Inspect(this,a);void RecordEmbeddedAssetDataUuids(AssetBase a)=>Probe.Inspect(this,a);
 public string Snapshot()=>string.Join(",",GatheredUuids.OrderBy(k=>Probe.Number(k.Key)).Select(k=>Probe.Number(k.Key)+":"+k.Value))+"|"+string.Join(",",FailedUUIDs.Select(Probe.Number))+"|"+string.Join(",",UncertainAssetsUUIDs.Select(Probe.Number))+"|"+ErrorCount+":"+AssetGetCount+":"+possibleNotAssetCount+":"+(verbose?1:0)+"|"+string.Join(",",m_assetUuidsToInspect.Select(Probe.Number));
}
class TestHG:HGUuidGatherer{public TestHG(IAssetService service,string url,IDictionary<UUID,sbyte> collector):base(service,url,collector){}public string Url{get=>m_assetServerURL;set=>m_assetServerURL=value;}}
static class Probe{
 public static int Level;static int mode,point,nth,seen,mutUrl,mutLevel,depth;static List<string> trace=new();public static TestHG HG;
 public static UUID Id(int n)=>n==-1?Constants.DefaultMaterialID:new UUID("00000000-0000-0000-0000-"+n.ToString("x12"));public static int Number(UUID key)=>key==Id(-1)?-1:Convert.ToInt32(key.ToString().Substring(24),16);
 public static void Log(string s)=>trace.Add(s);public static void Hook(int p){if(p==point&&++seen==nth)throw new Exception("fault");}
 static string Hex(string s)=>Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();static string Url(string s)=>s==null?"N":"V:"+Hex(s);static string Unhex(string s)=>s=="-"?null:s=="."?"":Encoding.UTF8.GetString(Convert.FromHexString(s));
 static void Mutate(){if(mutUrl==1)HG.Url=null;else if(mutUrl==2)HG.Url="changed";else if(mutUrl==3)HG.Url=" \t";if(mutLevel!=0)Level=mutLevel-2;}
 public static AssetBase Get(UUID key,bool foreign,string url,bool local){int n=Number(key);Log((foreign?"F:":"L:")+n+(foreign?":"+Url(url)+":"+(local?1:0):""));Hook(foreign?2:1);Mutate();if(foreign)Log("ARG:"+Url(url));if(mode==2||(mode==6&&n==2))throw new Exception("fault");if(mode==1||(mode==5&&n==2))return null;return new(){Tag=n,Type=10,Data=mode==3?null:new byte[mode==4||(mode==7&&n==2)?0:36]};}
 public static void Inspect(UuidGatherer g,AssetBase a){Log("I:"+a.Tag);Hook(7);if(a.Tag>0&&a.Tag<depth)g.AddForInspection(Id(a.Tag+1));else if(depth>1)g.AddForInspection(Id(1));}
 static string Run(string line){var a=line.Split(' ');string url=Unhex(a[0]);int op=int.Parse(a[1]);Level=int.Parse(a[2]);mode=int.Parse(a[3]);point=int.Parse(a[4]);nth=int.Parse(a[5]);mutUrl=int.Parse(a[6]);mutLevel=int.Parse(a[7]);int collector=int.Parse(a[8]),target=int.Parse(a[9]);depth=int.Parse(a[10]);seen=0;trace.Clear();var state=new Dictionary<UUID,sbyte>();if(collector==1)state[Id(1)]=7;if(collector==4)state[Id(2)]=7;var first=new TestHG(new Service(),url,state);HG=first;if(collector==2)first.FailedUUIDs.Add(Id(1));if(collector==3)first.UncertainAssetsUUIDs.Add(Id(1));TestHG second=null;string result="";
 try{if(op==0||op==4){var asset=first.FetchAsset(Id(target));result=asset==null?"-":"A:"+asset.Tag+":"+asset.Type+":"+(asset.Data==null?"N":asset.Data.Length.ToString());}if(op!=0){first.AddForInspection(Id(target));bool gathered=first.GatherAll(op==2);if(op!=4)result=gathered?"1":"0";if(op==3){second=new(new Service(),url,state);HG=second;second.AddForInspection(Id(target));result+=second.GatherAll()?":1":":0";}}}catch{result="EX";}
 return result+"|"+Hex(string.Join(";",trace))+"|"+Url(first.Url)+"|"+Level+"|"+first.Snapshot()+(second==null?"":"|S:"+second.Snapshot());
 }
 public static void Main(){Console.OutputEncoding=new UTF8Encoding(false);string line;while((line=Console.ReadLine())!=null)Console.WriteLine(Run(line));}
}
