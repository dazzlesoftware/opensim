using System;using System.Text;using OpenMetaverse;
class ScriptAssetTextReference {
 static void Main(){string line;while((line=Console.ReadLine())!=null){try{byte[] bytes=line=="null"?null:Convert.FromHexString(line);Console.WriteLine(Convert.ToHexString(Encoding.UTF8.GetBytes(Utils.BytesToString(bytes))).ToLowerInvariant());}catch{Console.WriteLine("THROW");}}}
}
