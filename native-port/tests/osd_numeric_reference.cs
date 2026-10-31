// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;using System.Text;using System.Globalization;using OpenMetaverse;using OpenMetaverse.StructuredData;
static class Program {
 static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;string line;
  while((line=Console.ReadLine())!=null){try{
   var value=FixtureValue.Build(line);string extra=line.StartsWith("long ")?"|"+Convert.ToBase64String(value.AsBinary()):"";
   string binary;try{binary=Convert.ToHexString(value.AsBinary()).ToLowerInvariant();}catch{binary="EX";}
   string alias="-";if(value.Type==OSDType.Binary||value.Type==OSDType.Boolean){var first=value.AsBinary();if(first.Length>0){byte saved=first[0];first[0]^=85;alias=ReferenceEquals(first,value.AsBinary())&&value.AsBinary()[0]==first[0]?"1":"0";first[0]=saved;}}
   Console.WriteLine(value.AsInteger()+"|"+BitConverter.DoubleToInt64Bits(value.AsReal()).ToString("x16")+"|"+value.AsUInteger()+"|"+Convert.ToHexString(Encoding.UTF8.GetBytes(value.AsString())).ToLowerInvariant()+"|"+binary+"|"+alias+extra);
  }catch{Console.WriteLine("EX");}}
 }
}
