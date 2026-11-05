using System;using System.IO;using System.Text;using System.Collections.Generic;using System.Linq;using log4net.Core;using log4net.Appender;using log4net.Layout;
LEVEL
public class ConsoleBase {public Action<string,ConsoleLevel> Sink;public virtual void Output(string text,params object[] components)=>Sink(text,(ConsoleLevel)components[0]);}
APPENDER
public class Failure:Exception {string text;public Failure(string value){text=value;}public override string ToString()=>text;}
public class Probe:OpenSimAppender {public void Emit(LoggingEvent value)=>Append(value);}
public class Layout:LayoutSkeleton {public Func<string> Render;public Layout(){IgnoresException=false;}public override void ActivateOptions(){}public override void Format(TextWriter writer,LoggingEvent value){writer.Write(Render());}}
public class Capture:TextWriter {
 public int Mode,Calls;public override Encoding Encoding=>Encoding.UTF8;
 void Emit(string text,bool newline){if(text.StartsWith("Couldn't write out log message: "))text=text.Split('\n')[0].TrimEnd('\r');Program.Log((newline?"line:":"write:")+Program.Hex(text));++Calls;if(Mode==2||(Mode==1&&Calls==1)||(Mode==3&&text.StartsWith("Couldn't write out log message: ")))throw new Failure("write failure");}
 public override void Write(string text)=>Emit(text,false);public override void WriteLine(string text)=>Emit(text,true);
}
public class Program {
 static List<string> logs=new();public static void Log(string text)=>logs.Add(text);
 public static string Hex(string value)=>string.Concat(value.Select(c=>((int)c).ToString("x4")));
 static string Decode(string value)=>new string(Enumerable.Range(0,value.Length/4).Select(i=>(char)Convert.ToUInt16(value.Substring(i*4,4),16)).ToArray());
 public static void Main(){System.Threading.Thread.CurrentThread.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;var stdout=Console.Out;string row;while((row=Console.ReadLine())!=null){logs.Clear();var f=row.Split('|');var capture=new Capture{Mode=int.Parse(f[3])};Console.SetOut(capture);var appender=new Probe();Func<int,ConsoleBase> target=mode=>mode==0?null:new ConsoleBase{Sink=(text,level)=>{Log("output:"+Hex(text)+":"+Hex(level.ToString()));if(mode==2)throw new Failure("output failure");if(mode==3)appender.Console=null;}};appender.Console=target(int.Parse(f[1]));var layout=new Layout{Render=()=>{Log("render");int mutation=int.Parse(f[2]);if(mutation==1)throw new Failure("render failure");if(mutation>=2)appender.Console=target(mutation==2?0:mutation==3?1:2);return Decode(f[4]);}};appender.Layout=layout;LoggingEvent value=f[0]=="~"?null:new LoggingEvent(typeof(Program),null,"Fixture",f[0]=="#"?null:new Level(int.Parse(f[0]),Decode(f[5])),null,null);string result="ok";try{appender.Emit(value);}catch(Exception error){result="err:"+Hex(error.ToString());}stdout.WriteLine(result+"|"+(appender.Console==null?"0":"1")+"|"+string.Join(";",logs));Console.SetOut(stdout);appender.Close();}}
}
