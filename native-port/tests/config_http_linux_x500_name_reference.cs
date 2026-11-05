using System;
using System.Security.Cryptography.X509Certificates;
class Program {
 static void Main(){string line;while((line=Console.ReadLine())!=null){
  var bytes=line=="_"?Array.Empty<byte>():Convert.FromHexString(line);
  var value=new X500DistinguishedName(bytes).Name;
  if(value.Length==0)Console.Write("_");else foreach(var c in value)Console.Write(((ushort)c).ToString("x4"));
  Console.WriteLine();
 }}
}
