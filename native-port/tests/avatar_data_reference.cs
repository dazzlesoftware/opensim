using System;using System.Text;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;
public partial class AvatarDataReference{
    static string Hex(byte[] b)=>Convert.ToHexString(b).ToLowerInvariant();static string Number(float n)=>(float.IsNaN(n)?0x7fc00000u:BitConverter.SingleToUInt32Bits(n)).ToString("x");
    static UUID Id(int n)=>new UUID("00000000-0000-0000-0000-"+n.ToString("x12"));
    static string Text(string s)=>s=="-"?null:s=="."?"":Encoding.UTF8.GetString(Convert.FromHexString(s));
    static string H(string s)=>Hex(Encoding.UTF8.GetBytes(s));
    static string View(AvatarData a){string s=a.AvatarType+"|";if(a.Data==null)s+="null";else foreach(var p in a.Data)s+=H(p.Key)+"="+(p.Value==null?"null":"s"+H(p.Value))+";";s+='|';try{foreach(var p in a.ToKeyValuePairs())s+=H(p.Key)+"="+H((string)p.Value)+";";}catch{s+="EX";}s+='|';try{var ap=a.ToAvatarAppearance();s+=ap==null?"null":Snapshot(ap);}catch{s+="EX";}return s;}
    public static void Main(){Culture.SetCurrentCulture();var a=new AvatarData();string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');string status="OK";try{
        switch(p[0]){
            case "default":a=new AvatarData();break;case "empty":a=new AvatarData{Data=new Dictionary<string,string>()};break;case "type":a.AvatarType=int.Parse(p[1]);break;
            case "set":if(a.Data==null)a.Data=new Dictionary<string,string>();a.Data[Text(p[1])]=Text(p[2]);break;case "remove":a.Data.Remove(Text(p[1]));break;
            case "kv":object v=p[2] switch{"string"=>Text(p[3]),"int"=>int.Parse(p[3]),"bool"=>p[3]=="1","float"=>BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[3],16)),_=>null};a=new AvatarData(new Dictionary<string,object>{{Text(p[1]),v}});break;
            case "export":a=new AvatarData(a.ToKeyValuePairs());break;case "round":a=new AvatarData(a.ToAvatarAppearance());break;
            case "ap":int mode=int.Parse(p[1]);var ap=new AvatarAppearance();if(mode==1){ap.Serial=-3;ap.AvatarHeight=BitConverter.UInt32BitsToSingle(Convert.ToUInt32(p[2],16));}if(mode==2)ap.VisualParams=null;if(mode==3)ap.VisualParams=Array.Empty<byte>();if(mode==4)ap.Wearables=null;if(mode==5)ap.Wearables=Array.Empty<AvatarWearable>();if(mode==6)ap.Wearables[0]=null;if(mode==7){var shortWearables=ap.Wearables;Array.Resize(ref shortWearables,6);ap.Wearables=shortWearables;}
                if(mode==8)ap.SetWearable(16,new AvatarWearable(Id(7),Id(8)));if(mode==9){ap.Wearables[0].Add(Id(7),Id(8));foreach(int n in new[]{1,2})ap.AppendAttachment(new AvatarAttachment(5,Id(n),Id(n+10)));ap.AppendAttachment(new AvatarAttachment(129,Id(3),Id(13)));}if(mode==10)ap.VisualParams=new byte[]{255};if(mode==11)ap.ClearWearables();a=new AvatarData(ap);break;
        }
    }catch{status="EX";}Console.WriteLine(status+"|"+View(a));}}
}
