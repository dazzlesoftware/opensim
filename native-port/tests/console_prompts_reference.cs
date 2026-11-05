using System;using System.Collections.Generic;using System.Linq;using System.IO;using System.Text;
public interface IConsole{}public interface IScene{}
LEVEL
BASE
public class Limit:Exception{}
public class Probe:ConsoleBase {
 public List<string> Input;public int At;public Probe(List<string> input):base("Region"){Input=input;}
 public override string ReadLine(string prompt,bool command,bool echo){if(At>=32)throw new Limit();Program.Log("read:"+Program.Hex(prompt)+":"+(command?"1":"0")+":"+(echo?"1":"0"));Program.AfterRead();return At<Input.Count?Input[At++]: (++At>0?null:null);}
}
public class Capture:TextWriter {public override Encoding Encoding=>Encoding.UTF8;public override void WriteLine(string value){Program.Log("diag:"+Program.Hex(value));Program.AfterDiag();}}
public class Program {
 public static Action AfterRead=()=>{},AfterDiag=()=>{};
 static List<string> logs=new();public static void Log(string value)=>logs.Add(value);
 public static string Hex(string value)=>value==null?"~":string.Concat(value.Select(c=>((int)c).ToString("x4")));
 static string Decode(string value)=>value=="~"?null:new string(Enumerable.Range(0,value.Length/4).Select(i=>(char)Convert.ToUInt16(value.Substring(i*4,4),16)).ToArray());
 static List<string> List(string value)=>value=="!"?new():value.Substring(1).Split(',').Select(Decode).ToList();
 public static void Main(){System.Threading.Thread.CurrentThread.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;var stdout=Console.Out;Console.SetOut(new Capture());string row;while((row=Console.ReadLine())!=null){logs.Clear();var f=row.Split('|');var probe=new Probe(List(f[6]));var chars=f[3]=="~"?null:Decode(f[3]).ToList();var options=f[4]=="~"?null:List(f[4]);Action<string> mutate=action=>{if(action.Length==0)return;Log("mutation:"+action);var parts=action.Split(':');switch(parts[0]){case "cx":chars=new List<char>{'Z'};break;case "ox":options=new List<string>{"yes"};break;case "ca":chars.Add(Decode(parts[1])[0]);break;case "cc":chars.Clear();break;case "cs":chars[int.Parse(parts[1])]=Decode(parts[2])[0];break;case "cr":chars.RemoveAt(int.Parse(parts[1]));break;case "oa":options.Add(Decode(parts[1]));break;case "oc":options.Clear();break;case "os":options[int.Parse(parts[1])]=Decode(parts[2]);break;case "or":options.RemoveAt(int.Parse(parts[1]));break;}};bool readMutated=false,diagMutated=false;AfterRead=()=>{if(!readMutated&&f.Length>7){readMutated=true;mutate(f[7]);}};AfterDiag=()=>{if(!diagMutated&&f.Length>8){diagMutated=true;mutate(f[8]);}};string result;try{string value=f[0] switch {"P"=>probe.Prompt(Decode(f[1])),"D"=>probe.Prompt(Decode(f[1]),Decode(f[2])),"X"=>probe.Prompt(Decode(f[1]),chars),"E"=>probe.Prompt(Decode(f[1]),Decode(f[2]),chars,f[5]=="1"),"O"=>probe.Prompt(Decode(f[1]),Decode(f[2]),options),_=>throw new Exception()};result="ok:"+Hex(value);}catch(Exception e){result="err:"+(e is Limit?"Limit":e.GetType().Name);}stdout.WriteLine(result+"|"+string.Join(";",logs));}}
}
