// Copyright (c) Contributors, http://opensimulator.org/
// Actual .NET 8 BCL provider oracle. No translated method substitute.
using System;using System.Reflection;using System.Runtime.CompilerServices;using System.Globalization;
class Program {
 public static ulong RawDate(DateTime d)=>Unsafe.BitCast<DateTime,ulong>(d);
 static DateTime Date(ulong raw)=>Unsafe.BitCast<ulong,DateTime>(raw);
 static BindingFlags Flags=BindingFlags.Static|BindingFlags.NonPublic;
 static MethodInfo Utc=typeof(TimeZoneInfo).GetMethod("GetUtcOffsetFromUtc",Flags,null,new[]{typeof(DateTime),typeof(TimeZoneInfo),typeof(bool).MakeByRefType(),typeof(bool).MakeByRefType()},null)!;
 static MethodInfo Wall=typeof(TimeZoneInfo).GetMethod("GetUtcOffset",Flags,null,new[]{typeof(DateTime),typeof(TimeZoneInfo)},null)!;
 public static string Transition(TimeZoneInfo.TransitionTime t)=>$"{t.TimeOfDay.TimeOfDay.Ticks} {t.Month} {t.Week} {t.Day} {(int)t.DayOfWeek} {(t.IsFixedDateRule?1:0)}";
 public static TimeZoneInfo zone=TimeZoneInfo.Local,current=TimeZoneInfo.Local;
 static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;for(string? line;(line=Console.ReadLine())!=null;){try{var a=line.Split(' ');switch(a[0]){
 case "TZIF_LOCAL":UnixSourceTimeZone.WriteModel(Convert.FromHexString(a[1]),a.Length>2?a[2]:"Audit",false,true);break;
 case "TZIF":UnixSourceTimeZone.WriteModel(Convert.FromHexString(a[1]),a.Length>2?a[2]:"Audit",false);break;
 case "CONVERT":var ib=Convert.FromHexString(a[1]);var chars=new char[ib.Length/2];for(int i=0;i<chars.Length;i++)chars[i]=(char)(ib[i*2]|ib[i*2+1]<<8);if(TimeZoneInfo.TryConvertIanaIdToWindowsId(new string(chars),out var windows)){var bytes=System.Text.Encoding.Unicode.GetBytes(windows);Console.WriteLine(Convert.ToHexString(bytes).ToLowerInvariant());}else Console.WriteLine("~");break;
 case "SPAN":Console.WriteLine(TimeSpan.TryParseExact(a[1],"g",CultureInfo.InvariantCulture,out var span)?span.Ticks.ToString():"false");break;
 case "DIGITS":for(int c=0;c<65536;c++)if(char.IsDigit((char)c))Console.WriteLine(c);Console.WriteLine("END");break;
 case "TOLOCAL":Console.WriteLine(RawDate(Date(ulong.Parse(a[1])).ToLocalTime()));break;
 case "FROMFILE":try{Console.WriteLine(RawDate(DateTime.FromFileTimeUtc(long.Parse(a[1]))));}catch(ArgumentOutOfRangeException e){Console.WriteLine($"ERR|{unchecked((uint)e.HResult)}|{e.ParamName??"~"}|{e.Message}");}break;
 case "TOFILE":try{Console.WriteLine(Date(ulong.Parse(a[1])).ToFileTimeUtc());}catch(ArgumentOutOfRangeException e){Console.WriteLine($"ERR|{unchecked((uint)e.HResult)}|{e.ParamName??"~"}|{e.Message}");}break;
 case "CUTOFF":Console.WriteLine(new GregorianCalendar().TwoDigitYearMax);break;
 case "UTCNOW":Console.WriteLine(RawDate(DateTime.UtcNow));break;
 case "NOW":Console.WriteLine(RawDate(DateTime.Now));break;
 case "LEAP":var create=typeof(DateTime).GetMethod("TryCreate",BindingFlags.Static|BindingFlags.NonPublic)!;var values=new object[]{int.Parse(a[1]),int.Parse(a[2]),int.Parse(a[3]),int.Parse(a[4]),int.Parse(a[5]),60,0,DateTime.MinValue};Console.WriteLine((bool)create.Invoke(null,values)!?1:0);break;
 case "META":Console.WriteLine($"{Environment.Version}|{System.Runtime.InteropServices.RuntimeInformation.OSDescription}|{TimeZoneInfo.Local.Id}");break;
 case "IDS":foreach(var z in TimeZoneInfo.GetSystemTimeZones())Console.WriteLine(z.Id);Console.WriteLine("END");break;
 case "CURRENT_ZONE":goto case "ZONE";
 case "ZONE":var target=a[1]=="LOCAL"?TimeZoneInfo.Local:TimeZoneInfo.FindSystemTimeZoneById(string.Join(" ",a,1,a.Length-1));if(a[0]=="CURRENT_ZONE")current=target;else zone=target;var rules=(TimeZoneInfo.AdjustmentRule[])typeof(TimeZoneInfo).GetField("_adjustmentRules",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(target)!??Array.Empty<TimeZoneInfo.AdjustmentRule>();Console.WriteLine($"ZONE {target.BaseUtcOffset.Ticks} {rules.Length}");foreach(var r in rules){var no=(bool)typeof(TimeZoneInfo.AdjustmentRule).GetProperty("NoDaylightTransitions",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(r)!;Console.WriteLine($"{RawDate(r.DateStart)} {RawDate(r.DateEnd)} {r.DaylightDelta.Ticks} {r.BaseUtcOffsetDelta.Ticks} {(no?1:0)} {Transition(r.DaylightTransitionStart)} {Transition(r.DaylightTransitionEnd)}");}Console.WriteLine("END");break;
 case "NOWOFFSET":var time=new DateTime(long.Parse(a[1]));var crules=(TimeZoneInfo.AdjustmentRule[]?)typeof(TimeZoneInfo).GetField("_adjustmentRules",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(current);var amount=current.BaseUtcOffset;bool ambiguous=false;if(crules!=null&&crules.Length>0){var r=crules[0];amount+=r.BaseUtcOffsetDelta;if((bool)typeof(TimeZoneInfo.AdjustmentRule).GetProperty("HasDaylightSaving",BindingFlags.Instance|BindingFlags.NonPublic)!.GetValue(r)!){var method=typeof(TimeZoneInfo).GetMethod("GetIsDaylightSavingsFromUtc",Flags)!;var values2=new object?[]{time,time.Year,current.BaseUtcOffset,r,null,false,zone};if((bool)method.Invoke(null,values2)!)amount+=r.DaylightDelta;ambiguous=(bool)values2[5]!;}}Console.WriteLine($"{amount.Ticks}|{(ambiguous?1:0)}");break;
 case "U":var args=new object[]{new DateTime(long.Parse(a[1])),zone,false,false};var offset=(TimeSpan)Utc.Invoke(null,args)!;Console.WriteLine($"{offset.Ticks}|{((bool)args[3]?1:0)}");break;
 case "L":var d=Date(ulong.Parse(a[1]));Console.WriteLine(d.Kind==DateTimeKind.Utc?zone.GetUtcOffset(d).Ticks:((TimeSpan)Wall.Invoke(null,new object[]{d,zone})!).Ticks);break;
 default:throw new Exception("Unknown query");}}catch(Exception e){Console.WriteLine("EX:"+(e.InnerException??e).GetType().Name);}}}
}
