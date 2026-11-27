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

public static class SuspendedProgram {
    static int suspendAt = -1, checks;
    static string phase;
    static readonly List<object> samples = new();

    public static void RecordStack(XMRInstAbstract instance) {
        samples.Add(new { phase, stack_left = instance.m_StackLeft, stack_limit = instance.stackLimit,
                          call_mode = instance.callMode });
        if (instance.callMode == XMRInstAbstract.CallMode_RESTORE) {
            instance.callMode = XMRInstAbstract.CallMode_NORMAL;
            return;
        }
        if (++checks == suspendAt) {
            instance.callMode = XMRInstAbstract.CallMode_SAVE;
            throw new StackCaptureException();
        }
    }

    static string outputDirectory;
    static string generatorSourceFile;

    static void Main(string[] args) {
        string directory = Path.GetFullPath(args[0]);
        outputDirectory = Path.GetFullPath(args[1]);
        generatorSourceFile = Path.GetFullPath(args[2]);
        Directory.CreateDirectory(outputDirectory);
        AppDomain.CurrentDomain.AssemblyResolve += (_, request) => {
            string path = Path.Combine(directory, new AssemblyName(request.Name).Name + ".dll");
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        };
        ExecuteInputs();
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static void ExecuteInputs() {
        var api = typeof(XMRInstance).Assembly.GetType("OpenSim.Region.ScriptEngine.Yengine.XMRLSL_Api", true);
        var module =
            AssemblyBuilder
                .DefineDynamicAssembly(new AssemblyName("FunctionExecutionReferenceContext"), AssemblyBuilderAccess.Run)
                .DefineDynamicModule("Context");
        var builder =
            module.DefineType("FunctionExecutionReferenceInstance", TypeAttributes.Public, typeof(XMRInstAbstract));
        builder.DefineDefaultConstructor(MethodAttributes.Public);
        builder.DefineField("m_ApiManager_LSL", api, FieldAttributes.Public);
        // Environment methods are unused by these pure scripts. CheckRunWork is
        // intentionally a no-op; generated arithmetic, calls, and stack checks run unchanged.
        foreach (var method in typeof(XMRInstAbstract).GetMethods()) {
            if (!method.IsAbstract)
                continue;
            var parameters = Array.ConvertAll(method.GetParameters(), p => p.ParameterType);
            var stub = builder.DefineMethod(method.Name, MethodAttributes.Public | MethodAttributes.Virtual,
                                            method.ReturnType, parameters);
            var il = stub.GetILGenerator();
            if (method.Name == "CheckRunWork") {
                il.Emit(OpCodes.Ldarg_0);
                il.Emit(OpCodes.Call, typeof(SuspendedProgram).GetMethod(nameof(RecordStack)));
            }
            if (method.ReturnType != typeof(void)) {
                if (method.ReturnType.IsValueType) {
                    var local = il.DeclareLocal(method.ReturnType);
                    il.Emit(OpCodes.Ldloca, local);
                    il.Emit(OpCodes.Initobj, method.ReturnType);
                    il.Emit(OpCodes.Ldloc, local);
                } else
                    il.Emit(OpCodes.Ldnull);
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

        var cases = new[] { ("unwind_without_new_call", "return 7;"),
                            ("new_complex_call_after_resume", "return checkedLeaf();") };
        var results = new List<object>();
        foreach (var spec in cases) {
            var folder = Path.Combine(outputDirectory, spec.Item1);
            Directory.CreateDirectory(folder);
            var source = "integer entries;integer calls;integer result;" +
                         "integer checkedLeaf(){while(FALSE){}return 7;}" +
                         "integer recur(integer n){calls++;if(n>0)return recur(n-1);integer k=1;while(k>0){k--;}" +
                         spec.Item2 + "}default{state_entry(){entries++;result=recur(130);}}";
            File.WriteAllText(Path.Combine(folder, "source.lsl"), source);
            var errors = new List<string>();
            var tokens = TokenBegin.Construct("reduced-stack-reference", null, (token, message) => errors.Add(message),
                                              source, out string hash);
            var script = tokens == null ? null : ScriptReduce.Reduce(tokens);
            using var bytes = new MemoryStream();
            using (var writer = new BinaryWriter(bytes, System.Text.Encoding.UTF8,
                                                 true)) if (script == null ||
                                                            !ScriptCodeGen.CodeGen(
                                                                script, writer,
                                                                hash)) throw new Exception(string.Join("\n", errors));
            File.WriteAllBytes(Path.Combine(folder, "original.yobj"), bytes.ToArray());
            bytes.Position = 0;
            var asm = new StringWriter();
            var code = new ScriptObjCode(new BinaryReader(bytes), asm, null);
            File.WriteAllText(Path.Combine(folder, "original.il.txt"), asm.ToString());
            XMRInstAbstract NewInstance(int stackSize) {
                var inst = (XMRInstAbstract)Activator.CreateInstance(instanceType);
                typeof(XMRInstAbstract)
                    .GetField("heapLimit", BindingFlags.Instance | BindingFlags.NonPublic)
                    .SetValue(inst, 100000000);
                inst.stackLimit = stackSize / 2;
                inst.m_StackLeft = stackSize;
                inst.m_ObjCode = code;
                inst.ehArgs = Array.Empty<object>();
                inst.glblVars.AllocVarArrays(code.glblSizes);
                inst.eventCode = ScriptEventCode.state_entry;
                return inst;
            }
            object Globals(XMRInstAbstract inst) {
                var values = new SortedDictionary<string, object>();
                foreach (var names in code.globalVarNames) {
                    var array = (Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(inst.glblVars);
                    foreach (var name in names.Value)
                        values[name.Value] = array.GetValue(name.Key);
                }
                return values;
            }
            object[] Frames(XMRInstAbstract inst) {
                var frames = new List<object>();
                for (var frame = inst.stackFrames; frame != null; frame = frame.nextSF)
                    frames.Add(
                        new { function = frame.funcName, call_no = frame.callNo, slots = frame.objArray.Length });
                return frames.ToArray();
            }
            var originalInstance = NewInstance(65536);
            phase = "capture64K";
            samples.Clear();
            checks = 0;
            suspendAt = 133;
            bool captured = false;
            try {
                originalInstance.CallSEH();
            } catch (StackCaptureException) {
                captured = true;
            }
            if (!captured || originalInstance.stackFrames == null)
                throw new Exception("Expected original leaf suspension was not captured");
            var captureSamples = samples.ToArray();
            var capturedGlobals = Globals(originalInstance);
            var capturedFrames = Frames(originalInstance);
            using var snapshot = new MemoryStream();
            snapshot.WriteByte(XMRInstance.migrationVersion);
            snapshot.WriteByte(16);
            originalInstance.MigrateOut(new BinaryWriter(snapshot, System.Text.Encoding.UTF8, true));
            File.WriteAllBytes(Path.Combine(folder, "snapshot.bin"), snapshot.ToArray());
            var encoder =
                typeof(XMRInstance).GetMethod("ByteArrayToSixbitStr", BindingFlags.Static | BindingFlags.NonPublic);
            if (encoder == null)
                throw new Exception("Original cache encoder missing");
            var cacheKey = new System.Text.StringBuilder();
            encoder.Invoke(null, new object[] { cacheKey, System.Security.Cryptography.SHA256.HashData(
                                                              System.Text.Encoding.UTF8.GetBytes(source)) });
            File.WriteAllText(Path.Combine(folder, "cache_key.txt"), cacheKey + "\n",
                              new System.Text.UTF8Encoding(false));
            var resumes = new List<object>();
            foreach (int stackSize in new[] { 65536, 16384 }) {
                var restored = NewInstance(stackSize);
                bool loaded = false, completed = false;
                string loadError = null, resumeError = null;
                object loadedGlobals = null, loadedFrames = null;
                int stackAfterLoad = restored.m_StackLeft;
                samples.Clear();
                checks = 0;
                suspendAt = -1;
                phase = "resume" + stackSize;
                try {
                    snapshot.Position = 2;
                    restored.MigrateIn(new BinaryReader(snapshot, System.Text.Encoding.UTF8, true));
                    loaded = true;
                    loadedGlobals = Globals(restored);
                    loadedFrames = Frames(restored);
                    stackAfterLoad = restored.m_StackLeft;
                } catch (Exception error) {
                    loadError = error.ToString();
                }
                if (loaded) {
                    try {
                        restored.CallSEH();
                        completed = true;
                    } catch (Exception error) {
                        resumeError = error.ToString();
                    }
                }
                resumes.Add(new { stack_size = stackSize, stack_limit = stackSize / 2, loaded, loadError,
                                  stackAfterLoad, loadedGlobals, loadedFrames, completed, resumeError,
                                  finalGlobals = Globals(restored), finalStackLeft = restored.m_StackLeft,
                                  remainingFrames = Frames(restored), stackSamples = samples.ToArray() });
            }
            string Sha(string path) =>
                Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path)))
                    .ToLowerInvariant();
            var hashes = new Dictionary<string, string>();
            foreach (string file in new[] { "source.lsl", "original.yobj", "original.il.txt", "snapshot.bin",
                                            "cache_key.txt" })
                hashes[file] = Sha(Path.Combine(folder, file));
            hashes["original_yengine_assembly"] = Sha(typeof(XMRInstance).Assembly.Location);
            hashes["generator_assembly"] = Sha(typeof(SuspendedProgram).Assembly.Location);
            hashes["generator_source"] = Sha(generatorSourceFile);
            var evidence =
                new { scenario = spec.Item1,
                      source_hash = code.sourceHash,
                      global_names = code.globalVarNames,
                      cache_key = cacheKey.ToString(),
                      compiled_version = ScriptCodeGen.COMPILED_VERSION_VALUE,
                      migration_version = XMRInstance.migrationVersion,
                      original_source_unchanged = true,
                      environment_stubs = "Only abstract environment methods; CheckRunWork records actual generated " +
                                          "stack and scheduler-only suspension at controlled check133",
                      captureStackSize = 65536,
                      captureStackLimit = 32768,
                      captured_check = 133,
                      capturedGlobals,
                      capturedFrames,
                      captureAfterUnwindStackLeft = originalInstance.m_StackLeft,
                      captureSamples,
                      resumes,
                      sha256 = hashes };
            File.WriteAllText(Path.Combine(folder, "results.json"),
                              JsonSerializer.Serialize(evidence, new JsonSerializerOptions { WriteIndented = true }));
            File.WriteAllText(Path.Combine(folder, "metadata.json"),
                              JsonSerializer.Serialize(evidence, new JsonSerializerOptions { WriteIndented = true }));
            results.Add(evidence);
            Console.WriteLine(spec.Item1 + ": captured " + capturedFrames.Length +
                              " actual original frames; sourcehash " + code.sourceHash);
        }
        File.WriteAllText(Path.Combine(outputDirectory, "results.json"),
                          JsonSerializer.Serialize(results, new JsonSerializerOptions { WriteIndented = true }));
    }
}
