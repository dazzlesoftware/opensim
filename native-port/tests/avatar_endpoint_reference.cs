using System;using System.IO;using System.Xml;using System.Text;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Framework;using OpenSim.Services.Interfaces;
public class IOSHttpRequest{}public class IOSHttpResponse{}
public static class AvatarEndpointUtil{public static Encoding UTF8NoBomEncoding=new UTF8Encoding(false);public static byte[] DocToBytes(XmlDocument doc){using var m=new MemoryStream();using(var writer=new XmlTextWriter(m,UTF8NoBomEncoding){Formatting=Formatting.Indented})doc.WriteTo(writer);return m.ToArray();}}
public class EndpointLogger{public void Debug(object o){}public void DebugFormat(string f,params object[] a){}}
public partial class AvatarHandler {
    IAvatarService m_AvatarService;EndpointLogger m_log=new();
    static string H(string s)=>Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    class Service:IAvatarService{
        public string trace="";bool Touch(UUID id){if(id.ToString().EndsWith("9"))throw new Exception();return !id.ToString().EndsWith("8");}
        public AvatarData GetAvatar(UUID id){trace+="get:"+id+";";if(!Touch(id))return null;var a=new AvatarData{AvatarType=1};if(id==UUID.Zero)return a;a.Data=new Dictionary<string,string>{{"Wearable 0:0","item:asset"},{"_x0041_","literal"},{"9name","<&>\r\n\t"},{"試験",null},{"Emoji 🦊","value"}};if(id.ToString().EndsWith("1"))a.Data["controls"]="\0\u0001\u000b\u001f\ufffe\uffff";return a;}
        public bool SetAvatar(UUID id,AvatarData a){trace+="set:"+id+":"+a.AvatarType+":";foreach(var p in a.Data)trace+=H(p.Key)+"="+(p.Value==null?"null":H(p.Value))+",";trace+=';';return Touch(id);}
        public bool ResetAvatar(UUID id){trace+="reset:"+id+";";return Touch(id);}
        void List(string[] a){foreach(var s in a)trace+=H(s)+",";trace+=';';}
        public bool SetItems(UUID id,string[] names,string[] values){trace+="items:"+id+":";List(names);List(values);return Touch(id)&&names.Length==values.Length;}
        public bool RemoveItems(UUID id,string[] names){trace+="remove:"+id+":";List(names);return Touch(id);}
        public AvatarAppearance GetAppearance(UUID id)=>throw new Exception();public bool SetAppearance(UUID id,AvatarAppearance a)=>throw new Exception();
    }
    public static void Main(){Culture.SetCurrentCulture();string line;while((line=Console.ReadLine())!=null){if(line.StartsWith("name ")){Console.WriteLine(H(XmlConvert.EncodeLocalName(Encoding.UTF8.GetString(Convert.FromHexString(line.Substring(5))))));continue;}var service=new Service();var handler=new AvatarHandler{m_AvatarService=service};byte[] b=handler.ProcessRequest("/avatar",new MemoryStream(Convert.FromHexString(line)),new(),new());Console.WriteLine(Convert.ToHexString(b).ToLowerInvariant()+"|"+H(service.trace));}}
}
