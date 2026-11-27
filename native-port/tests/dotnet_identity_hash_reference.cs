// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Threading;
using LSL = OpenSim.Region.ScriptEngine.Shared.LSL_Types;

public static class DotNetIdentityHashReference {
    static string Sha(string file) =>
        Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(file))).ToLowerInvariant();

    static uint Next(ref uint state, uint multiplier) {
        uint hash;
        do {
            state = unchecked(state * multiplier + 1);
            hash = state >> 6;
        } while (hash == 0);
        return hash;
    }

    static object Capture() {
        var arrays = new object[64][];
        for (int n = 0; n < arrays.Length; n++)
            arrays[n] = new object[] { n };
        int threadId = Environment.CurrentManagedThreadId;
        var hashes = new int[64];
        // No logging/collections/hash calls intervene in this measured loop.
        for (int n = 0; n < arrays.Length; n++)
            hashes[n] = arrays[n].GetHashCode();
        uint multiplier = unchecked((uint)threadId * 4 + 5);
        var candidates = new List<uint>();
        for (uint low = 0; low < 64; low++) {
            uint first = unchecked(((uint)hashes[0] << 6) | low), state = first;
            bool matches = true;
            for (int n = 1; n < hashes.Length; n++)
                if (Next(ref state, multiplier) != (uint)hashes[n]) {
                    matches = false;
                    break;
                }
            if (matches)
                candidates.Add(first);
        }
        bool range = true;
        foreach (int hash in hashes)
            range &= hash > 0 && hash < (1 << 26);
        var predicted = new List<uint>();
        if (candidates.Count == 1) {
            uint state = candidates[0];
            for (int n = 1; n < 64; n++)
                predicted.Add(Next(ref state, multiplier));
        }
        int originalHash = arrays[0].GetHashCode();
        object[] alias = arrays[0];
        int aliasHash = alias.GetHashCode();
        arrays[0][0] = "mutated";
        int mutatedHash = arrays[0].GetHashCode();
        int runtimeHelpersHash = RuntimeHelpers.GetHashCode(arrays[0]);
        var data = new object[] { "first" };
        var list = new LSL.list(data);
        var listAlias = new LSL.list(data);
        int dataHash = data.GetHashCode(), listHash = list.GetHashCode(), listAliasHash = listAlias.GetHashCode();
        data[0] = "changed";
        int listAfterMutation = list.GetHashCode();
        return new {
            managed_thread_id = threadId,
            multiplier,
            hashes,
            nonzero_26bit = range,
            recovered_after_first_candidates = candidates,
            recovered_after_first_state_hex = candidates.Count == 1 ? candidates[0].ToString("x8") : null,
            predicted_remaining_63 = predicted,
            unique_recovery = candidates.Count == 1,
            array_relations = new { same_reference = object.ReferenceEquals(alias, arrays[0]),
                                    original_hash = originalHash, alias_hash = aliasHash,
                                    content_mutation_hash = mutatedHash, runtime_helpers_hash = runtimeHelpersHash,
                                    stable = originalHash == aliasHash && aliasHash == mutatedHash &&
                                             mutatedHash == runtimeHelpersHash },
            sdk_list_relations =
                new { first_backing_same = object.ReferenceEquals(list.Data, data),
                      alias_backing_same = object.ReferenceEquals(listAlias.Data, data), data_hash = dataHash,
                      list_hash = listHash, alias_hash = listAliasHash, after_mutation_hash = listAfterMutation,
                      stable = dataHash == listHash && listHash == listAliasHash && listAliasHash == listAfterMutation }
        };
    }

    static void Main(string[] args) {
        string bin = Path.GetFullPath(args[0]);
        AppDomain.CurrentDomain.AssemblyResolve += (_, r) => {
            string file = Path.Combine(bin, new AssemblyName(r.Name).Name + ".dll");
            return File.Exists(file) ? Assembly.LoadFrom(file) : null;
        };
        Run(Path.GetFullPath(args[1]), Path.GetFullPath(args[2]));
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Run(string output, string generator) {
        Directory.CreateDirectory(output);
        var rows = new List<object>();
        for (int n = 0; n < 4; n++) {
            object row = null;
            Exception failure = null;
            var worker = new Thread(() => {
                try {
                    row = Capture();
                } catch (Exception e) {
                    failure = e;
                }
            });
            worker.Start();
            worker.Join();
            if (failure != null)
                throw failure;
            rows.Add(row);
        }
        var report =
            new { runtime = RuntimeInformation.FrameworkDescription,
                  architecture = RuntimeInformation.ProcessArchitecture.ToString(),
                  os = RuntimeInformation.OSDescription,
                  process_id = Environment.ProcessId,
                  generator_sha256 = Sha(generator),
                  assembly_sha256 = Sha(typeof(DotNetIdentityHashReference).Assembly.Location),
                  corelib_sha256 = Sha(typeof(object).Assembly.Location),
                  original_sdk_assembly_sha256 = Sha(typeof(LSL.list).Assembly.Location),
                  algorithm = "Pinned CoreCLR v8.0.34: GetNewHashCode updates uint32 state = state*(threadId*4+5)+1; " +
                              "ComputeHashCode returns state>>6 and retries zero. Recover only the 64 possible " +
                              "after-first states then validate remaining 63.",
                  scope = "Actual BCL object[] identity hash calls and unchanged original SDK List.Data.GetHashCode. " +
                          "No portable fixed process sequence or unobserved thread/global hash consumption " +
                          "assumption. Array/list aliases and content mutations do not change assigned identity hash.",
                  threads = rows };
        File.WriteAllText(Path.Combine(output, "metadata.json"),
                          JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine("Captured 4 original CLR threads with 64 identity hashes each");
    }
}
