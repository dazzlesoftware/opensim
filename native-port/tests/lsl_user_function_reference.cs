// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Uses the unchanged original YEngine compiler assembly, not a reimplementation.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Text.Json;
using OpenSim.Region.ScriptEngine.Yengine;

static class Program
{
    static void Main(string[] args)
    {
        string directory = Path.GetFullPath(args[0]);
        AppDomain.CurrentDomain.AssemblyResolve += (_, request) =>
        {
            string path = Path.Combine(directory, new AssemblyName(request.Name).Name + ".dll");
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        };
        CompileInputs();
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static void CompileInputs()
    {
        // Mirror the original engine's compiler setup without starting a region.
        var api = typeof(XMRInstance).Assembly.GetType("OpenSim.Region.ScriptEngine.Yengine.XMRLSL_Api", true);
        var module = AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("FunctionReferenceContext"),
            AssemblyBuilderAccess.Run).DefineDynamicModule("Context");
        var builder = module.DefineType("FunctionReferenceInstance", TypeAttributes.Public, typeof(XMRInstance));
        builder.DefineField("m_ApiManager_LSL", api, FieldAttributes.Public);
        ScriptCodeGen.xmrInstSuperType = builder.CreateType();
        ScriptObjWriter.DefineInternalType("xmrsuper", ScriptCodeGen.xmrInstSuperType);
        ScriptObjWriter.DefineInternalType("apimanager_LSL", api);
        ScriptConst.AddInterfaceConstants(null, api.GetFields());
        TokenDeclInline.AddInterfaceMethods(null, api.GetMethods(),
            ScriptCodeGen.xmrInstSuperType.GetField("m_ApiManager_LSL"));
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var errors = new List<string>();
            bool accepted = false;
            string exception = null;
            try
            {
                string source = JsonSerializer.Deserialize<string>(line);
                var tokens = TokenBegin.Construct("function-reference", null,
                    (token, message) => errors.Add(message), source, out string hash);
                if (tokens != null)
                {
                    var script = ScriptReduce.Reduce(tokens);
                    if (script != null)
                    {
                        using var bytes = new MemoryStream();
                        using var writer = new BinaryWriter(bytes);
                        accepted = ScriptCodeGen.CodeGen(script, writer, hash);
                    }
                }
            }
            catch (Exception error) { exception = error.ToString(); }
            Console.WriteLine(JsonSerializer.Serialize(new { accepted, errors, exception }));
        }
    }
}
