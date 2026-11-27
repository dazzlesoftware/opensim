using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Collections.Generic;
using System.Text.Json;

class Program {
    static void Main(string[] args) {
        var bin = Path.GetFullPath(args[0]);
        AppDomain.CurrentDomain.AssemblyResolve += (_, a) => {
            var p = Path.Combine(bin, new AssemblyName(a.Name).Name + ".dll");
            return File.Exists(p) ? Assembly.LoadFrom(p) : null;
        };
        var a = Assembly.LoadFrom(Path.Combine(bin, "OpenMetaverseTypes.dll"));
        var ops = typeof(OpCodes)
                      .GetFields()
                      .Where(f => f.FieldType == typeof(OpCode))
                      .Select(f => (OpCode)f.GetValue(null))
                      .ToDictionary(o => (ushort)o.Value);
        var rows = new List<object>();
        foreach (var t in a.GetTypes().Where(t => t.Name == "osUTF8" || t.FullName == "OpenMetaverse.Utils")) {
            foreach (var m in t
                         .GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance |
                                     BindingFlags.Static)
                         .Cast<MethodBase>()
                         .Concat(
                             t.GetConstructors(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance))
                         .Where(m => t.Name == "osUTF8" ||
                                     (m.Name.Contains("StringToBytesNoTerm") || m.Name.Contains("osUTF8")))) {
                var body = m.GetMethodBody();
                if (body == null)
                    continue;
                var b = body.GetILAsByteArray();
                var lines = new List<object>();
                for (int p = 0; p < b.Length;) {
                    int offset = p;
                    ushort op = b[p++];
                    if (op == 254)
                        op = (ushort)(0xfe00 | b[p++]);
                    var code = ops[op];
                    object operand = null;
                    int n = code.OperandType switch {
                        OperandType.InlineNone => 0,
                        OperandType.ShortInlineBrTarget or OperandType.ShortInlineI or OperandType.ShortInlineVar => 1,
                        OperandType.InlineVar => 2, OperandType.InlineI8 or OperandType.InlineR => 8,
                        _ => 4
                    };
                    if (code.OperandType == OperandType.InlineSwitch) {
                        int count = BitConverter.ToInt32(b, p);
                        n = 4 + 4 * count;
                    }
                    if (n > 0) {
                        var raw = Convert.ToHexString(b.AsSpan(p, n));
                        operand = raw;
                        if (n == 4) {
                            int token = BitConverter.ToInt32(b, p);
                            try {
                                if (code.OperandType == OperandType.InlineMethod ||
                                    code.OperandType == OperandType.InlineField ||
                                    code.OperandType == OperandType.InlineType ||
                                    code.OperandType == OperandType.InlineTok)
                                    operand = m.Module.ResolveMember(token).ToString();
                                else if (code.OperandType == OperandType.InlineString)
                                    operand = m.Module.ResolveString(token);
                                else if (code.OperandType == OperandType.InlineI)
                                    operand = token;
                                else if (code.OperandType == OperandType.InlineBrTarget)
                                    operand = p + 4 + token;
                            } catch {
                            }
                        } else if (n == 1 && code.OperandType == OperandType.ShortInlineBrTarget)
                            operand = p + 1 + (sbyte)b[p];
                        p += n;
                    }
                    lines.Add(new { offset, op = code.Name, operand });
                }
                rows.Add(new { type = t.FullName, method = m.ToString(),
                               locals = body.LocalVariables.Select(v => v.LocalType.ToString()), il = lines });
            }
        }
        File.WriteAllText(args[1], JsonSerializer.Serialize(rows, new JsonSerializerOptions { WriteIndented = true }));
    }
}
