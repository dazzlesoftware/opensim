using System;using System.Reflection;using System.Security.Cryptography.X509Certificates;
class Program {
 static void Main(){var type=typeof(X509Certificate2).Assembly.GetType("System.Security.Cryptography.X509Certificates.OpenSslX509ChainProcessor",true);var method=type.GetMethod("FindHttpAiaRecord",BindingFlags.Static|BindingFlags.NonPublic);string line;
  while((line=Console.ReadLine())!=null){var fields=line.Split(' ');var bytes=fields[1]=="_"?Array.Empty<byte>():Convert.FromHexString(fields[1]);var value=(string)method.Invoke(null,new object[]{new ReadOnlyMemory<byte>(bytes),fields[0]=="_"?"":fields[0]});if(value==null)Console.Write('_');else foreach(var c in value)Console.Write(((ushort)c).ToString("x4"));Console.WriteLine();}
 }
}
