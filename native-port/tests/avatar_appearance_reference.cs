using System;
using System.Linq;
using System.Text;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
namespace log4net {public interface ILog {void Debug(object o);void Warn(object o);void ErrorFormat(string f,params object[] a);}public static class LogManager {class Log:ILog {public void Debug(object o){}public void Warn(object o){}public void ErrorFormat(string f,params object[] a){}}public static ILog GetLogger(Type t)=>new Log();}}
class Program {
    static UUID Id(uint n)=>new UUID("00000000-0000-0000-0000-"+n.ToString("x12"));
    static string Hex(byte[] b)=>Convert.ToHexString(b).ToLowerInvariant();
    static string Number(float n)=>(float.IsNaN(n)?0x7fc00000:BitConverter.SingleToUInt32Bits(n)).ToString("x");
    static string Canonical(OSD v){switch(v.Type){
        case OSDType.Integer:return "i"+v.AsInteger();
        case OSDType.Real:return "r"+(double.IsNaN(v.AsReal())?0x7ff8000000000000ul:BitConverter.DoubleToUInt64Bits(v.AsReal())).ToString("x");
        case OSDType.UUID:return "q"+v.AsUUID();case OSDType.Binary:return "x"+Hex(v.AsBinary());
        case OSDType.Array:{string s="[";foreach(OSD n in (OSDArray)v)s+=Canonical(n)+",";return s+"]";}
        case OSDType.Map:{var rows=new System.Collections.Generic.SortedDictionary<string,OSD>(StringComparer.Ordinal);foreach(System.Collections.Generic.KeyValuePair<string,OSD> n in (OSDMap)v)rows.Add(n.Key,n.Value);string s="{";foreach(var n in rows)s+=n.Key+"="+Canonical(n.Value)+",";return s+"}";}
        default:return "u";
    }}
    static string Snapshot(AvatarAppearance a){
        string s=a.Serial+":"+Number(a.AvatarHeight)+":"+Number(a.AvatarPreferencesHoverZ);
        foreach(var v in new[]{a.AvatarSize,a.AvatarBoxSize})s+=":"+Number(v.X)+","+Number(v.Y)+","+Number(v.Z);
        s+=":"+Number(a.AvatarFeetOffset)+":"+(a.VisualParams==null?"null":Hex(a.VisualParams))+":";
        if(a.Wearables==null)s+="null";else foreach(var w in a.Wearables){if(w==null){s+="null;";continue;}s+="[";for(int i=0;i<w.Count;i++)s+=w[i].ItemID+","+w[i].AssetID+";";s+="]";}
        s+=":"+(a.Texture==null?"null":Hex(a.Texture.GetBytes()))+":";
        foreach(var t in a.GetAttachments())s+=t.AttachPoint+","+t.ItemID+","+t.AssetID+";";
        s+=":";if(a.WearableCacheItems==null)s+="null";else foreach(var c in a.WearableCacheItems)s+=c==null?"null;":c.TextureIndex+","+c.CacheId+","+c.TextureID+";";
        return s;
    }
    static void Main(){AvatarAppearance a=new AvatarAppearance(),saved=null;string line;
        while((line=Console.ReadLine())!=null){var p=line.Split(' ');string op=p[0],status="OK",output="-";
            try{
                if(op=="reset")a=new AvatarAppearance();
                else if(op=="description")output=Hex(Encoding.UTF8.GetBytes(a.ToString()));
                else if(op=="initialized"){if(p[1]=="0")a=new AvatarAppearance(null,null,null);else{saved=a;a=new AvatarAppearance(a.Wearables,a.Texture,a.VisualParams);}}
                else if(op=="saved-alias")output=saved==null?"-":(ReferenceEquals(saved.Wearables,a.Wearables)?"1":"0")+(ReferenceEquals(saved.Texture,a.Texture)?"1":"0")+(ReferenceEquals(saved.VisualParams,a.VisualParams)?"1":"0")+(ReferenceEquals(saved.WearableCacheItems,a.WearableCacheItems)?"1":"0");
                else if(op=="copy"){saved=a;a=new AvatarAppearance(a,int.Parse(p[1])!=0,int.Parse(p[2])!=0);}
                else if(op=="copy-null")a=new AvatarAppearance((AvatarAppearance)null);
                else if(op=="map-null")a=new AvatarAppearance((OSDMap)null);
                else if(op=="size")a.SetSize(new Vector3(BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[1],16)),BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[2],16)),BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[3],16))));
                else if(op=="scalar"){a.Serial=int.Parse(p[1]);a.AvatarHeight=BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[2],16));a.AvatarPreferencesHoverZ=BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[3],16));}
                else if(op=="clear-wearables")a.ClearWearables();
                else if(op=="wear"){int i=int.Parse(p[1]),mode=int.Parse(p[2]);var w=mode==1?a.Wearables[i]:mode==2?null:new AvatarWearable(Id(uint.Parse(p[3])),Id(uint.Parse(p[4])));a.SetWearable(i,w);}
                else if(op=="add-wear")a.Wearables[int.Parse(p[1])].Add(Id(uint.Parse(p[2])),Id(uint.Parse(p[3])));
                else if(op=="visual"){int mode=int.Parse(p[1]);byte[] b=mode==2?null:Enumerable.Repeat((byte)int.Parse(p[3]),int.Parse(p[2])).ToArray();if(mode==1)a.VisualParams=b;else output=a.SetVisualParams(b)?"1":"0";}
                else if(op=="visual-null")a.VisualParams=null;
                else if(op=="textures"){int mode=int.Parse(p[1]),index=int.Parse(p[2]);if(mode==2)a.Texture=null;else{var t=mode==1?a.Texture:new Primitive.TextureEntry(AppearanceManager.DEFAULT_AVATAR_TEXTURE);if(index>=0)t.GetFace((uint)index).TextureID=Id(uint.Parse(p[3]));output=a.SetTextureEntries(t)?"1":"0";}}
                else if(op=="reset-appearance")a.ResetAppearance();else if(op=="reset-bakes")a.ResetBakedTextures();
                else if(op=="attach")output=a.SetAttachment(int.Parse(p[1]),Id(uint.Parse(p[2])),Id(uint.Parse(p[3])))?"1":"0";
                else if(op=="append"||op=="replace"){var t=new AvatarAttachment(int.Parse(p[1]),Id(uint.Parse(p[2])),Id(uint.Parse(p[3])));if(op=="append")a.AppendAttachment(t);else a.ReplaceAttachment(t);}
                else if(op=="detach")output=a.DetachAttachment(Id(uint.Parse(p[1])))?"1":"0";
                else if(op=="remove")output=a.RemoveAttachment(int.Parse(p[1]),Id(uint.Parse(p[2])))?"1":"0";
                else if(op=="lookup"){var id=Id(uint.Parse(p[1]));var t=a.GetAttachmentForItem(id);output=a.GetAttachpoint(id)+":"+(t==null?"null":t.AttachPoint+","+t.AssetID);}
                else if(op=="mutate-attachment"){var t=a.GetAttachmentForItem(Id(uint.Parse(p[1])));if(t!=null)t.AttachPoint=int.Parse(p[2]);}
                else if(op=="clear-attachments")a.ClearAttachments();
                else if(op=="can")output=a.CanTeleport(BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[1],16)))?"1":"0";
                else if(op=="cache"){a.WearableCacheItems=WearableCacheItem.GetDefaultCacheItem();foreach(int i in new[]{8,20,40,44}){a.WearableCacheItems[i].CacheId=Id((uint)i);a.WearableCacheItems[i].TextureID=Id((uint)i+1);}}
                else if(op=="pack"){EntityTransferContext ctx=null;if(p[1]!="null")ctx=new EntityTransferContext{OutboundVersion=BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[1],16))};output=Canonical(a.Pack(ctx));}
                else if(op=="notecard")output=Canonical(a.PackForNotecard(int.Parse(p[1])!=0));
                else if(op=="unpack"){if(p[1]=="null")a.Unpack(null);else{var map=new OSDMap();if(p[1]=="packed")map=a.Pack(null);else if(p[1]=="bad-wear")map["wearables"]=new OSDArray{OSD.FromInteger(3)};else if(p[1]=="extra-only")map["wrbls8"]=new OSDArray{OSD.FromInteger(3)};else if(p[1]=="height")map["height"]=OSD.FromReal(.25);else if(p[1]=="cache-extra"){OSDMap Row(int index,uint id)=>new OSDMap{{"textureindex",OSD.FromInteger(index)},{"cacheid",OSD.FromUUID(Id(id))},{"textureid",OSD.FromUUID(Id(id))}};map["bakedcache"]=new OSDArray{Row(8,888)};map["bc8"]=new OSDArray{Row(40,999)};}a.Unpack(map);}}
                else if(op=="saved-mutate"&&saved!=null){saved.VisualParams[0]^=255;saved.Wearables[0].Clear();if(saved.WearableCacheItems!=null)saved.WearableCacheItems[8].TextureID=Id(999);}
                else if(op=="assets"&&saved!=null)a.GetAssetsFrom(saved);
            }catch(Exception e){status="EX";if(Environment.GetEnvironmentVariable("APPEARANCE_FIXTURE_DEBUG")=="1")Console.Error.WriteLine(op+": "+e);}
            string state;try{state=Snapshot(a);}catch{state="STATEEX";}Console.WriteLine(status+"|"+output+"|"+state);
        }
    }
}
