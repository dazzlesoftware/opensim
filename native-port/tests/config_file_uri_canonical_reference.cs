using System;
using System.Reflection;
unsafe class Program
{
    static void Main()
    {
        var method = typeof(Uri).GetMethod("CheckCanonical", BindingFlags.Instance | BindingFlags.NonPublic);
        var flagsField = typeof(Uri).GetField("_flags", BindingFlags.Instance | BindingFlags.NonPublic);
        var flagsType = typeof(Uri).GetNestedType("Flags", BindingFlags.NonPublic);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int flags = int.Parse(p[0]), delimiter = int.Parse(p[1]);
            string text = "";
            if (p[2] != "-")
                for (int i = 0; i < p[2].Length; i += 4)
                    text += (char)Convert.ToUInt16(p[2].Substring(i, 4), 16);
            var uri = new Uri("file:///x");
            ulong value = ((flags & 1) != 0 ? 0x20000000UL : 0) | ((flags & 2) != 0 ? 0x200000000UL : 0);
            flagsField.SetValue(uri, Enum.ToObject(flagsType, value));
            fixed(char *ptr = text)
            {
                object[] args = { Pointer.Box(ptr, typeof(char *)), 0, text.Length, (char)delimiter };
                var result = method.Invoke(uri, args);
                Console.WriteLine($"{Convert.ToInt32(result)}:{(int)args[1]}");
            }
        }
    }
}
