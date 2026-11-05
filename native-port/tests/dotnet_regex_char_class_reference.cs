using System;using System.Globalization;using System.IO;using System.Reflection;using System.Text;using System.Text.RegularExpressions;
class Program{
 delegate bool Cached(char c,string set,ref uint[] cache);
 static string Decode(string s){var chars=new char[s.Length/4];for(int i=0;i<chars.Length;i++)chars[i]=(char)Convert.ToUInt16(s.Substring(i*4,4),16);return new string(chars);}
 static string Hex(string s){var b=new StringBuilder();foreach(char c in s)b.Append(((int)c).ToString("x4"));return b.ToString();}
 static string Cache(uint[] values)=>values==null?"-":values.Length==0?".":string.Join(",",Array.ConvertAll(values,v=>v.ToString("x8")));
 static void Main(string[] args){
  if(args.Length==2&&args[0]=="--export-category"){var categories=new byte[65536];for(int i=0;i<65536;i++)categories[i]=(byte)char.GetUnicodeCategory((char)i);File.WriteAllBytes(args[1],categories);return;}
  var type=typeof(Regex).Assembly.GetType("System.Text.RegularExpressions.RegexCharClass");var flags=BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static;
  var contains=(Func<char,string,bool>)type.GetMethod("CharInClass",flags,null,new[]{typeof(char),typeof(string)},null).CreateDelegate(typeof(Func<char,string,bool>));
  var cached=(Cached)type.GetMethod("CharInClass",flags,null,new[]{typeof(char),typeof(string),typeof(uint[]).MakeByRefType()},null).CreateDelegate(typeof(Cached));
  Func<char,bool> word=(Func<char,bool>)type.GetMethod("IsWordChar",flags).CreateDelegate(typeof(Func<char,bool>)),boundary=(Func<char,bool>)type.GetMethod("IsBoundaryWordChar",flags).CreateDelegate(typeof(Func<char,bool>)),ecma=(Func<char,bool>)type.GetMethod("IsECMAWordChar",flags).CreateDelegate(typeof(Func<char,bool>));
  string row;while((row=Console.ReadLine())!=null){var parts=row.Split(' ');uint[] cache=null;try{
   if(parts[0]=="W"){var b=new StringBuilder("W:");for(int i=0;i<65536;i++){char c=(char)i;b.Append(((int)char.GetUnicodeCategory(c)).ToString("x2")).Append(((ecma(c)?1:0)|(word(c)?2:0)|(boundary(c)?4:0)|(char.IsWhiteSpace(c)?8:0)).ToString("x1"));}Console.WriteLine(b);}
   else if(parts[0]=="C"){string set=Decode(parts[1]);var bytes=new byte[8192];for(int i=0;i<65536;i++)if(contains((char)i,set))bytes[i>>3]|=(byte)(1<<(i&7));Console.WriteLine("C:"+Convert.ToHexString(bytes).ToLowerInvariant());}
   else if(parts[0]=="P"){
    var parser=typeof(Regex).Assembly.GetType("System.Text.RegularExpressions.RegexParser");var parse=parser.GetMethod("Parse",flags,null,new[]{typeof(string),typeof(RegexOptions),typeof(CultureInfo)},null);var parseTyped=(Func<string,RegexOptions,CultureInfo,object>)parse.CreateDelegate(typeof(Func<string,RegexOptions,CultureInfo,object>));var tree=parseTyped(Decode(parts[1]),(RegexOptions)int.Parse(parts[2]),CultureInfo.InvariantCulture);var root=tree.GetType().GetField("Root").GetValue(tree);var sets=new System.Collections.Generic.List<string>();
    void Visit(object node){var t=node.GetType();string kind=t.GetProperty("Kind").GetValue(node).ToString();if(kind.StartsWith("Set"))sets.Add((string)t.GetProperty("Str").GetValue(node));int count=(int)t.GetMethod("ChildCount").Invoke(node,null);for(int i=0;i<count;i++)Visit(t.GetMethod("Child").Invoke(node,new object[]{i}));}Visit(root);Console.WriteLine("P:"+string.Join(":",sets.ConvertAll(Hex)));
   }else if(parts[0]=="T"){string set=Decode(parts[1]);uint[] shared=null;int errors=0;System.Threading.Tasks.Parallel.For(0,8,n=>{for(int i=0;i<4096;i++){char c=(char)((i*31+n*17)%128);if(cached(c,set,ref shared)!=contains(c,set))System.Threading.Interlocked.Increment(ref errors);}});Console.WriteLine("T:"+errors+":"+Cache(shared));
   }else{
    if(parts[3]!="-")cache=parts[3]=="."?Array.Empty<uint>():Array.ConvertAll(parts[3].Split(','),v=>Convert.ToUInt32(v,16));
    bool result=cached((char)int.Parse(parts[1]),Decode(parts[2]),ref cache);Console.WriteLine("L:"+(result?"1":"0")+":"+Cache(cache));
   }
  }catch(Exception e){if(e is TargetInvocationException&&e.InnerException!=null)e=e.InnerException;Console.WriteLine("E:"+e.GetType().FullName+":"+unchecked((uint)e.HResult).ToString("x8")+":"+Hex(e.Message)+":"+Cache(cache));}}
 }
}
