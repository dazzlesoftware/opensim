// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Executes unchanged original YEngine-generated methods; no expression oracle.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Text.Json;
using OpenSim.Region.ScriptEngine.Yengine;

public static class SwitchReference
{
    static readonly Dictionary<object,int> refs = new(System.Collections.Generic.ReferenceEqualityComparer.Instance);
    static object Value(object value) {
        if (value == null) return new { type = "null" };
        if (value.GetType().FullName == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+list") {
            var data=(object[])value.GetType().GetProperty("Data").GetValue(value);
            var entries=new List<object>();foreach(var item in data)entries.Add(Value(item));
            return new { type="list",clr=value.GetType().FullName,entries };
        }
        if (value is Array array) {
            if (refs.TryGetValue(value,out int prior)) return new { type="reference", id=prior };
            int id=refs.Count; refs[value]=id;
            var entries=new List<object>(); foreach(var item in array) entries.Add(Value(item));
            return new { type="array", clr=value.GetType().FullName, id, entries };
        }
        if (value is double number) return new {type="float", clr=value.GetType().FullName, bits=BitConverter.DoubleToInt64Bits(number).ToString("x16"), text=number.ToString("R",System.Globalization.CultureInfo.InvariantCulture)};
        if (value is char character) return new {type="char", clr=value.GetType().FullName, utf16=(int)character};
        if (value is int integer) return new {type="integer",clr=value.GetType().FullName,value=integer};
        return new {type="other",clr=value.GetType().FullName,text=value.ToString()};
    }
    static readonly List<int> StackSamples = new();
    static int stackSize = 1000000, stackLimit = 1024;
    public static void RecordStack(XMRInstAbstract instance) => StackSamples.Add(instance.m_StackLeft);

    static void Main(string[] args)
    {
        string directory = Path.GetFullPath(args[0]);
        if (args.Length > 1) stackSize = int.Parse(args[1]);
        if (args.Length > 2) stackLimit = int.Parse(args[2]);
        AppDomain.CurrentDomain.AssemblyResolve += (_, request) =>
        {
            string path = Path.Combine(directory, new AssemblyName(request.Name).Name + ".dll");
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        };
        ExecuteInputs();
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static void ExecuteInputs()
    {
        var api = typeof(XMRInstance).Assembly.GetType("OpenSim.Region.ScriptEngine.Yengine.XMRLSL_Api", true);
        var module = AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("FunctionExecutionReferenceContext"),
            AssemblyBuilderAccess.Run).DefineDynamicModule("Context");
        var builder = module.DefineType("FunctionExecutionReferenceInstance", TypeAttributes.Public,
            typeof(XMRInstAbstract));
        builder.DefineDefaultConstructor(MethodAttributes.Public);
        builder.DefineField("m_ApiManager_LSL", api, FieldAttributes.Public);
        // Environment methods are unused by these pure scripts. CheckRunWork is
        // intentionally a no-op; generated arithmetic, calls, and stack checks run unchanged.
        foreach (var method in typeof(XMRInstAbstract).GetMethods())
        {
            if (!method.IsAbstract) continue;
            var parameters = Array.ConvertAll(method.GetParameters(), p => p.ParameterType);
            var stub = builder.DefineMethod(method.Name, MethodAttributes.Public | MethodAttributes.Virtual,
                method.ReturnType, parameters);
            var il = stub.GetILGenerator();
            if (method.Name == "CheckRunWork")
            {
                il.Emit(OpCodes.Ldarg_0);
                il.Emit(OpCodes.Call, typeof(SwitchReference).GetMethod(nameof(RecordStack)));
            }
            if (method.ReturnType != typeof(void))
            {
                if (method.ReturnType.IsValueType)
                {
                    var local = il.DeclareLocal(method.ReturnType);
                    il.Emit(OpCodes.Ldloca, local);
                    il.Emit(OpCodes.Initobj, method.ReturnType);
                    il.Emit(OpCodes.Ldloc, local);
                }
                else il.Emit(OpCodes.Ldnull);
            }
            il.Emit(OpCodes.Ret);
            builder.DefineMethodOverride(stub, method);
        }
        Type instanceType = builder.CreateType();
        ScriptCodeGen.xmrInstSuperType = instanceType;
        ScriptObjWriter.DefineInternalType("xmrsuper", instanceType);
        ScriptObjWriter.DefineInternalType("apimanager_LSL", api);
        ScriptConst.AddInterfaceConstants(null, api.GetFields());
        TokenDeclInline.AddInterfaceMethods(null, api.GetMethods(), instanceType.GetField("m_ApiManager_LSL"));
        string line; int caseIndex=0;
        while ((line = Console.ReadLine()) != null)
        {
            string evidence=Path.Combine(Environment.GetEnvironmentVariable("FIXED_ARRAY_EVIDENCE"), (caseIndex++).ToString("D3"));
            Directory.CreateDirectory(evidence);
            var errors = new List<string>();
            var globals = new SortedDictionary<string, object>();
            bool accepted = false, executed = false;
            string exception = null; string exceptionType = null;
            int? finalStackLeft = null;
            StackSamples.Clear(); refs.Clear();
            try
            {
                string source = JsonSerializer.Deserialize<string>(line);
                File.WriteAllText(Path.Combine(evidence,"source.lsl"),source,new System.Text.UTF8Encoding(false));
                var tokens = TokenBegin.Construct("function-execution-reference", null,
                    (token, message) => errors.Add(message), source, out string hash);
                File.WriteAllText(Path.Combine(evidence,"source_hash.txt"),hash??"");
                var script = tokens == null ? null : ScriptReduce.Reduce(tokens);
                if (script != null)
                {
                    using var bytes = new MemoryStream();
                    using (var writer = new BinaryWriter(bytes, System.Text.Encoding.UTF8, true))
                        accepted = ScriptCodeGen.CodeGen(script, writer, hash);
                    if (accepted)
                    {
                        File.WriteAllBytes(Path.Combine(evidence,"original.yobj"),bytes.ToArray());
                        bytes.Position = 0;
                        var assemblyText=new StringWriter();
                        var code = new ScriptObjCode(new BinaryReader(bytes), assemblyText, null);
                        File.WriteAllText(Path.Combine(evidence,"original.il.txt"),assemblyText.ToString());
                        var instance = (XMRInstAbstract)Activator.CreateInstance(instanceType);
                        typeof(XMRInstAbstract).GetField("heapLimit", BindingFlags.Instance | BindingFlags.NonPublic)
                            .SetValue(instance, 100000000);
                        instance.stackLimit = stackLimit;
                        instance.m_StackLeft = stackSize;
                        instance.m_ObjCode = code;
                        instance.ehArgs = Array.Empty<object>();
                        instance.glblVars.AllocVarArrays(code.glblSizes);
                        instance.eventCode = ScriptEventCode.state_entry;
                        try { instance.CallSEH(); } catch(Exception error) { exception=error.Message;exceptionType=error.GetType().FullName; }
                        finally { finalStackLeft = instance.m_StackLeft; }
                        executed = true;
                        foreach (var arrayNames in code.globalVarNames)
                        {
                            var array = (Array)typeof(XMRInstArrays).GetField(arrayNames.Key).GetValue(instance.glblVars);
                            foreach (var entry in arrayNames.Value)
                            {
                                object value = array.GetValue(entry.Key);
                                globals[entry.Value] = Value(value);
                            }
                        }
                    }
                }
            }
            catch (Exception error) { exception = error.ToString(); exceptionType=error.GetType().FullName; }
            Console.WriteLine(JsonSerializer.Serialize(new { accepted, executed, globals, errors, exception, exceptionType,
                stackSize, stackLimit, finalStackLeft, stackSamples = StackSamples }));
        }
    }
}
