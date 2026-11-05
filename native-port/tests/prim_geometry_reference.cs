// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using OpenMetaverse;
// Framework enums and both SceneObjectPart methods are inserted unchanged by comparator.
/* ENUMS */
sealed class ShapeValues {
 public bool SculptEntry;
 public byte SculptType, ProfileCurve, PathCurve, PathRevolutions;
 public ushort ProfileBegin, ProfileEnd, ProfileHollow, PathBegin, PathEnd;
 public sbyte PathSkew, PathTwistBegin, PathTwist, PathTaperX, PathTaperY, PathRadiusOffset;
}
sealed class Part {
 public ShapeValues Shape = new ShapeValues();
/* METHODS */
}
static class Program {
 static void Main(){
 using var output=new System.IO.StreamWriter(Console.OpenStandardOutput(),new System.Text.UTF8Encoding(false),65536);Console.SetOut(output);
 if((byte)SculptType.Mesh!=5)throw new Exception("Bundled SDK mesh enum changed");
 string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);var p=new Part();var s=p.Shape;
 s.SculptEntry=a[0]!=0;s.SculptType=(byte)a[1];s.ProfileCurve=(byte)a[2];s.PathCurve=(byte)a[3];s.ProfileBegin=(ushort)a[4];s.ProfileEnd=(ushort)a[5];s.ProfileHollow=(ushort)a[6];s.PathBegin=(ushort)a[7];s.PathEnd=(ushort)a[8];s.PathSkew=(sbyte)a[9];s.PathTwistBegin=(sbyte)a[10];s.PathTwist=(sbyte)a[11];s.PathTaperX=(sbyte)a[12];s.PathTaperY=(sbyte)a[13];s.PathRadiusOffset=(sbyte)a[14];s.PathRevolutions=(byte)a[15];
 Console.WriteLine($"{(int)p.GetPrimType()} {(int)p.GetPrimType(true)} {p.GetNumberOfSides()}");}
 }
}
