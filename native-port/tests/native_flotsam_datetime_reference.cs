// Copyright (c) Contributors, http://opensimulator.org/
// Actual BCL invariant parsing and source comparison operation.
using System;using System.Globalization;using System.Runtime.CompilerServices;using System.Text;
class Program {static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;for(string? line;(line=Console.ReadLine())!=null;){string? text=line=="~"?null:Encoding.UTF8.GetString(Convert.FromHexString(line));bool parsed=DateTime.TryParse(text,out var date);Console.WriteLine($"{(parsed?1:0)}|{Unsafe.BitCast<DateTime,ulong>(date)}|{(string.Equals(text,"now",StringComparison.InvariantCultureIgnoreCase)?1:0)}");}}}
