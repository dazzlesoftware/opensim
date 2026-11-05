using System;
using System.Globalization;
class Program {
static string Decode(string value){if(value=="-")return null;if(value==".")return "";var chars=new char[value.Length/4];for(int i=0;i<chars.Length;i++)chars[i]=(char)(Convert.ToByte(value.Substring(i*4,2),16)|(Convert.ToByte(value.Substring(i*4+2,2),16)<<8));return new string(chars);}
static string Hex(string value){if(value.Length==0)return ".";var result=new System.Text.StringBuilder();foreach(char c in value){result.Append(((byte)c).ToString("x2"));result.Append(((byte)(c>>8)).ToString("x2"));}return result.ToString();}
static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;CultureInfo.CurrentUICulture=CultureInfo.InvariantCulture;string line;while((line=Console.ReadLine())!=null){var row=line.Split(' ');int op=int.Parse(row[0]);string value=Decode(row[1]);try{string output=op switch {0=>Convert.ToInt32(value,CultureInfo.InvariantCulture).ToString(),1=>Convert.ToInt64(value,CultureInfo.InvariantCulture).ToString(),2=>unchecked((uint)BitConverter.SingleToInt32Bits(Convert.ToSingle(value,CultureInfo.InvariantCulture))).ToString(),_=>unchecked((ulong)BitConverter.DoubleToInt64Bits(Convert.ToDouble(value,CultureInfo.InvariantCulture))).ToString()};Console.WriteLine("V:"+output);}catch(Exception e){Console.WriteLine("EX:"+e.GetType().FullName+":"+unchecked((uint)e.HResult)+":"+Hex(e.Message));}}}
}
