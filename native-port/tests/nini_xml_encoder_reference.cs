using System;
using System.IO;
using System.Text;
using System.Reflection;
using System.Collections.Generic;
delegate void SpanWrite(ReadOnlySpan<char> value);
sealed class RecordingWriter:TextWriter {
 public override Encoding Encoding=>Encoding.Unicode;
 public readonly List<string> Calls=new(); public int FailAt;
 public Action<int>? OnCall;
 public RecordingWriter(int fail){FailAt=fail;}
 static string Hex(string? s){if(s==null)return "null";var b=new StringBuilder();foreach(char c in s){b.Append(((int)c&255).ToString("X2"));b.Append(((int)c>>8).ToString("X2"));}return b.ToString();}
 void Emit(string kind,string? value){Calls.Add(kind+":"+Hex(value));int index=Calls.Count;OnCall?.Invoke(index);if(index==FailAt)throw new IOException("injected writer failure");}
 public override void Write(char c)=>Emit("C",new string(c,1));
 public override void Write(string? s)=>Emit("T",s);
 public override void Write(ReadOnlySpan<char> s)=>Emit("S",s.ToString());
 public override void Write(char[] b,int o,int n)=>Emit("A",new string(b,o,n));
}
class Program {
 static string Hex(string s){var b=new StringBuilder();foreach(char c in s)b.Append(((int)c).ToString("x4"));return b.Length==0?"-":b.ToString();}
 static string Decode(string s){if(s=="-")return "";var b=new StringBuilder();for(int i=0;i<s.Length;i+=4)b.Append((char)Convert.ToInt32(s.Substring(i,4),16));return b.ToString();}
 static void Main(){
  var type=typeof(System.Xml.XmlDocument).Assembly.GetType("System.Xml.XmlTextEncoder")!;
  const BindingFlags flags=BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.Public;
  foreach(string line in ReadLines()){
   var p=line.Split(' ');int mode=int.Parse(p[0]),attribute=int.Parse(p[1]),quote=int.Parse(p[2]),fail=int.Parse(p[3]);string s=p[4]=="null"?"":Decode(p[4]);
   char[]? inputArray=p[4]=="null"?null:s.ToCharArray();var writer=new RecordingWriter(fail);if(p.Length>7){int at=int.Parse(p[7]),position=int.Parse(p[8]),unit=int.Parse(p[9]);writer.OnCall=number=>{if(number==at)inputArray![position]=(char)unit;};}var encoder=Activator.CreateInstance(type,flags,null,new object[]{writer},null)!;
   type.GetProperty("QuoteChar",flags)!.SetValue(encoder,(char)quote);
   if(attribute!=0)type.GetMethod("StartAttribute",flags)!.Invoke(encoder,new object[]{attribute==2});
   string error="-";
   try{
    if(mode==0)((SpanWrite)type.GetMethod("Write",flags,null,new[]{typeof(ReadOnlySpan<char>)},null)!.CreateDelegate(typeof(SpanWrite),encoder))(s.AsSpan());
    else if(mode==1)type.GetMethod("WriteRawWithSurrogateChecking",flags)!.Invoke(encoder,new object?[]{s});
    else if(mode==2)type.GetMethod("WriteCharEntity",flags)!.Invoke(encoder,new object[]{s[0]});
    else if(mode==3)type.GetMethod("WriteEntityRef",flags)!.Invoke(encoder,new object?[]{s});
    else if(mode==4)type.GetMethod("WriteSurrogateCharEntity",flags)!.Invoke(encoder,new object[]{s[0],s[1]});
    else if(mode==5)type.GetMethod("WriteSurrogateChar",flags)!.Invoke(encoder,new object[]{s[0],s[1]});
    else if(mode==8)((SpanWrite)type.GetMethod("Write",flags,null,new[]{typeof(ReadOnlySpan<char>)},null)!.CreateDelegate(typeof(SpanWrite),encoder))(inputArray.AsSpan());
    else if(mode==6||mode==7)type.GetMethod(mode==6?"Write":"WriteRaw",flags,null,new[]{typeof(char[]),typeof(int),typeof(int)},null)!.Invoke(encoder,new object?[]{inputArray,p.Length>5?int.Parse(p[5]):0,p.Length>6?int.Parse(p[6]):s.Length});
   }catch(Exception e){while(e is TargetInvocationException&&e.InnerException!=null)e=e.InnerException;error=e.GetType().FullName+":"+e.HResult.ToString("x8")+":"+Hex(e.Message);}
   Console.WriteLine(string.Join("|",writer.Calls)+";"+Hex((string)type.GetProperty("AttributeValue",flags)!.GetValue(encoder)!)+";"+error);
  }
 }
 static IEnumerable<string> ReadLines(){string? s;while((s=Console.ReadLine())!=null)yield return s;}
}
