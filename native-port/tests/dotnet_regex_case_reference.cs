using System;using System.Globalization;using System.Reflection;using System.Reflection.Emit;using System.Text;using System.Text.RegularExpressions;
class NamedCulture:CultureInfo{string name;public int Reads;public NamedCulture(string value):base(""){name=value;}public override string Name{get{Reads++;return name;}}}
class Program{
 delegate string Find(char c,CultureInfo culture,ref int behavior);
 static Find Bind(Type type){
  var target=type.GetMethod("TryFindCaseEquivalencesForCharWithIBehavior",BindingFlags.Public|BindingFlags.Static);
  var method=new DynamicMethod("CaseEquivalences",typeof(string),new[]{typeof(char),typeof(CultureInfo),typeof(int).MakeByRefType()},typeof(Program).Module,true);var il=method.GetILGenerator();
  var behavior=il.DeclareLocal(target.GetParameters()[2].ParameterType.GetElementType());var span=il.DeclareLocal(typeof(ReadOnlySpan<char>));var found=il.DeclareLocal(typeof(bool));var empty=il.DefineLabel();
  il.Emit(OpCodes.Ldarg_2);il.Emit(OpCodes.Ldind_I4);il.Emit(OpCodes.Stloc,behavior);
  il.Emit(OpCodes.Ldarg_0);il.Emit(OpCodes.Ldarg_1);il.Emit(OpCodes.Ldloca,behavior);il.Emit(OpCodes.Ldloca,span);il.Emit(OpCodes.Call,target);il.Emit(OpCodes.Stloc,found);
  il.Emit(OpCodes.Ldarg_2);il.Emit(OpCodes.Ldloc,behavior);il.Emit(OpCodes.Stind_I4);il.Emit(OpCodes.Ldloc,found);il.Emit(OpCodes.Brfalse,empty);
  il.Emit(OpCodes.Ldloc,span);il.Emit(OpCodes.Newobj,typeof(string).GetConstructor(new[]{typeof(ReadOnlySpan<char>)}));il.Emit(OpCodes.Ret);
  il.MarkLabel(empty);il.Emit(OpCodes.Ldstr,"-");il.Emit(OpCodes.Ret);return (Find)method.CreateDelegate(typeof(Find));
 }
 static void Main(){
  var type=typeof(Regex).Assembly.GetType("System.Text.RegularExpressions.RegexCaseEquivalences");var find=Bind(type);var get=type.GetMethod("GetRegexBehavior",BindingFlags.Public|BindingFlags.Static);
  string[] names={"","en-US","tr","tr-TR","az","az-Latn-AZ","TR","trick","tr_TR","az-","t","a","tr-unknown","Az"};
  for(int n=0;n<names.Length;n++){var culture=new NamedCulture(names[n]);int value=Convert.ToInt32(get.Invoke(null,new object[]{culture}));Console.WriteLine("C:"+n+":"+value+":"+culture.Reads);}
  foreach(int n in new[]{0,1,3,5}){var culture=new NamedCulture(names[n]);for(int initial=0;initial<4;initial++)for(int code=0;code<65536;code++){
   culture.Reads=0;int behavior=initial;string value=find((char)code,culture,ref behavior);var hex=new StringBuilder();if(value=="-")hex.Append('-');else foreach(char c in value)hex.Append(((int)c).ToString("x4"));Console.WriteLine(n+":"+initial+":"+code+":"+behavior+":"+culture.Reads+":"+hex);
  }}
 }
}
