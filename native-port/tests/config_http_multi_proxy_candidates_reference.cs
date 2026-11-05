using System;using System.Net.Http;using System.Reflection;
class Reference{
 static string Hex(string s)=>Convert.ToHexString(System.Text.Encoding.BigEndianUnicode.GetBytes(s)).ToLowerInvariant();
 static void Main(){var assembly=typeof(HttpClient).Assembly;var type=assembly.GetType("System.Net.Http.MultiProxy",true)!;var cacheType=assembly.GetType("System.Net.Http.FailedProxyCache",true)!;var read=type.GetMethod("ReadNextHelper",BindingFlags.Instance|BindingFlags.NonPublic)!;var index=type.GetField("_currentIndex",BindingFlags.Instance|BindingFlags.NonPublic)!;string? row;
 while((row=Console.ReadLine())!=null){var pieces=row.Split(' ');try{string? text=pieces[2]=="null"?null:pieces[2]=="-"?"":System.Text.Encoding.BigEndianUnicode.GetString(Convert.FromHexString(pieces[2]));var cache=Activator.CreateInstance(cacheType,true);var method=type.GetMethod(pieces[0]=="1"?"CreateLazy":"Parse",BindingFlags.Public|BindingFlags.Static)!;var plan=method.Invoke(null,new object?[]{cache,text,pieces[1]=="1"})!;
 for(int i=0;i<1024;++i){var args=new object?[]{null,false};var found=(bool)read.Invoke(plan,args)!;if(i!=0)Console.Write("|");Console.Write((found?"1":"0")+":"+((bool)args[1]!?"1":"0")+":"+index.GetValue(plan)+":"+(args[0] is Uri uri?Hex(uri.AbsoluteUri):"-"));if(!found)break;}
 }catch(Exception e){while(e is TargetInvocationException&&e.InnerException!=null)e=e.InnerException;Console.Write("E:"+e.GetType().FullName+":"+e.HResult+":"+Hex(e.Message));}Console.WriteLine();}
 }
}
