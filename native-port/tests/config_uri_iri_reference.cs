using System;using System.Text;using System.Reflection;
unsafe class Program{
delegate string Iri(char* input,int start,int end,UriComponents component);
static string D(string s){var b=new StringBuilder();if(s!="-")for(int i=0;i<s.Length;i+=4)b.Append((char)Convert.ToInt32(s.Substring(i,4),16));return b.ToString();}static string H(string s){var b=new StringBuilder();foreach(char c in s)b.Append(((int)c).ToString("x4"));return b.Length==0?"-":b.ToString();}
static void Main(){var method=typeof(Uri).Assembly.GetType("System.IriHelper").GetMethod("EscapeUnescapeIri",BindingFlags.Static|BindingFlags.NonPublic).CreateDelegate<Iri>();var trigger=typeof(Uri).GetMethod("CheckForUnicodeOrEscapedUnreserved",BindingFlags.Static|BindingFlags.NonPublic).CreateDelegate<Func<string,bool>>();string row;while((row=Console.ReadLine())!=null){var a=row.Split(' ');var text=D(a[1]);fixed(char* p=text)Console.WriteLine((trigger(text)?"1/":"0/")+H(method(p,0,text.Length,unchecked((UriComponents)uint.Parse(a[0])))));}}
}
