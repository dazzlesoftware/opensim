using System;
using System.Net.Http;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text;
class Program
{
    static void Main()
    {
        var type = typeof(HttpClient).Assembly.GetType("System.Net.Http.HttpConnection", true);
        var method = type.GetMethod("ProcessKeepAliveHeader", BindingFlags.Instance | BindingFlags.NonPublic);
        var close = type.GetField("_connectionClose", BindingFlags.Instance | BindingFlags.NonPublic);
        var timeout =
            type.GetField("_keepAliveTimeoutSeconds", BindingFlags.Instance | BindingFlags.NonPublic);
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var instance = RuntimeHelpers.GetUninitializedObject(type);
            GC.SuppressFinalize(instance);
            foreach (var hex in line.Split('|'))
            {
                var s = new StringBuilder();
                if (hex != "-")
                    for (int i = 0; i < hex.Length; i += 4)
                        s.Append((char)Convert.ToUInt16(hex.Substring(i, 4), 16));
                method.Invoke(instance, new object[] { s.ToString() });
            }
            Console.WriteLine(((bool)close.GetValue(instance) ? "1" : "0") + ":" +
                              timeout.GetValue(instance));
        }
    }
}
