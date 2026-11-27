using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Xml;
using System.Collections.Generic;

class ObjectTextUtf8Reference {
    static object
    Describe(string s) => new { units = s.Select(c => (int)c).ToArray(),
                                utf8 = Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant() };

    static object Backing(object part, string name) {
        for (var type = part.GetType(); type != null; type = type.BaseType) {
            var field = type.GetField(name, BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
            if (field == null)
                continue;
            var value = field.GetValue(part);
            if (value == null)
                return null;
            return Convert.ToHexString((byte[])value.GetType().GetMethod("ToArray").Invoke(value, null))
                .ToLowerInvariant();
        }
        return "FIELD_NOT_FOUND";
    }

    static void Main(string[] args) {
        string bin = Path.GetFullPath(args[0]);
        AppDomain.CurrentDomain.AssemblyResolve += (_, a) => {
            var p = Path.Combine(bin, new AssemblyName(a.Name).Name + ".dll");
            return File.Exists(p) ? Assembly.LoadFrom(p) : null;
        };
        var scene = Assembly.LoadFrom(Path.Combine(bin, "OpenSim.Region.Framework.dll"));
        var partType = scene.GetType("OpenSim.Region.Framework.Scenes.SceneObjectPart", true);
        var apiAsm = Assembly.LoadFrom(Path.Combine(bin, "OpenSim.Region.ScriptEngine.Shared.Api.dll"));
        var apiType = apiAsm.GetType("OpenSim.Region.ScriptEngine.Shared.Api.LSL_Api", true);
        var serializer = scene.GetType("OpenSim.Region.Framework.Scenes.Serialization.SceneObjectSerializer", true);
        var cases = new List<(string, string)>();
        void Add(string name, string value) => cases.Add((name, value));
        foreach (int length in new[] { 0, 1, 2, 62, 63, 64, 125, 126, 127, 128, 200 })
            Add("ascii_" + length, new string('a', length));
        foreach (char unit in new[] { (char)0, (char)1, (char)0x7f, (char)0x80, (char)0x7ff, (char)0x800, (char)0xffff,
                                      (char)0xd800, (char)0xdbff, (char)0xdc00, (char)0xdfff }) {
            Add("unit_" + ((int)unit).ToString("x4"), new string(unit, 1));
            Add("mixed_" + ((int)unit).ToString("x4"), "a" + unit + "b");
            foreach (int prefix in new[] { 61, 62, 63, 125, 126, 127 })
                Add("edge_" + ((int)unit).ToString("x4") + "_" + prefix, new string('a', prefix) + unit + "z");
        }
        var emoji = char.ConvertFromUtf32(0x1f600);
        foreach (int prefix in new[] { 0, 59, 60, 61, 62, 63, 64, 123, 124, 125, 126, 127, 128 })
            Add("emoji_" + prefix, new string('a', prefix) + emoji + "z");
        foreach (var pair in new[] { "\ud800\ud800", "\udc00\ud800", "\udfff\udfff", "\ud800x", "x\ud800", "\ud800\0z",
                                     "\0z", "x\0z", "\0\0z", "x\0\0z", "\udbff\udfff" })
            Add("special_" + cases.Count, pair);
        var records = new List<object>();
        foreach (var c in cases) {
            var part = Activator.CreateInstance(partType);
            var shape = partType.GetProperty("Shape");
            shape.SetValue(part, shape.PropertyType.GetProperty("Default").GetValue(null));
            var groupType = scene.GetType("OpenSim.Region.Framework.Scenes.SceneObjectGroup", true);
            var group = Activator.CreateInstance(groupType, new object[] { part });
            var api = Activator.CreateInstance(apiType);
            apiType.GetField("m_host", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(api, part);
            var row = new Dictionary<string, object> { { "name", c.Item1 }, { "input", Describe(c.Item2) } };
            foreach (var field in new[] { "Name", "Desc" }) {
                try {
                    apiType.GetMethod("llSetObject" + field).Invoke(api, new object[] { c.Item2 });
                    var result = apiType.GetMethod("llGetObject" + field).Invoke(api, null).ToString();
                    row[field] = Describe(result);
                } catch (Exception e) {
                    row[field + "Exception"] = e.GetBaseException().ToString();
                }
            }
            row["name_large_bytes"] = Backing(part, "osUTF8LargeName");
            row["name_wire_bytes"] = Backing(part, "osUTF8Name");
            row["description_bytes"] = Backing(part, "osUTF8Description");
            try {
                var writer = new StringWriter();
                using (var xml = new XmlTextWriter(writer)) {
                    serializer.GetMethod("SOPToXml2")
                        .Invoke(null, new object[] { xml, part, new Dictionary<string, object>() });
                }
                var text = writer.ToString();
                try {
                    var restored = serializer.GetMethod("FromXml2Format")
                                       .Invoke(null, new object[] { "<SceneObjectGroup>" + text +
                                                                    "<OtherParts /></SceneObjectGroup>" });
                    if (restored == null)
                        row["xml_restore"] = new { success = false, error = "Original FromXml2Format returned null" };
                    else {
                        var root = restored.GetType().GetProperty("RootPart").GetValue(restored);
                        row["xml_restore"] =
                            new { success = true, name = Describe((string)partType.GetProperty("Name").GetValue(root)),
                                  description = Describe((string)partType.GetProperty("Description").GetValue(root)) };
                    }
                } catch (Exception ex) {
                    row["xml_restore"] = new { success = false, error = ex.GetBaseException().ToString() };
                }
                row["xml"] =
                    new { success = true, utf8 = Convert.ToHexString(Encoding.UTF8.GetBytes(text)).ToLowerInvariant() };
            } catch (Exception e) {
                row["xml"] = new { success = false, error = e.GetBaseException().ToString() };
            }
            records.Add(row);
        }
        File.WriteAllText(args[1],
                          JsonSerializer.Serialize(records, new JsonSerializerOptions { WriteIndented = true }));
    }
}
