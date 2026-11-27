// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using OpenSim.Region.ScriptEngine.Yengine;

static class Program {
    static void Main(string[] args) {
        string binaries = Path.GetFullPath(args[0]);
        AppDomain.CurrentDomain.AssemblyResolve += (_, request) => {
            string path = Path.Combine(binaries, new AssemblyName(request.Name).Name + ".dll");
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        };
        Generate(args[1]);
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static void Generate(string fixtures) {
        // Execute the unchanged private encoder used by InstantiateScript.
        var encoder =
            typeof(XMRInstance).GetMethod("ByteArrayToSixbitStr", BindingFlags.Static | BindingFlags.NonPublic);
        if (encoder == null)
            throw new Exception("Original cache encoder not found");
        foreach (string directory in Directory.GetDirectories(fixtures)) {
            string source = Path.Combine(directory, "source.lsl");
            if (!File.Exists(source))
                continue;
            byte[] digest = SHA256.HashData(Encoding.UTF8.GetBytes(File.ReadAllText(source)));
            var key = new StringBuilder();
            encoder.Invoke(null, new object[] { key, digest });
            File.WriteAllText(Path.Combine(directory, "cache_key.txt"), key + "\n", new UTF8Encoding(false));
            Console.WriteLine(Path.GetFileName(directory) + ": " + key);
        }
    }
}
