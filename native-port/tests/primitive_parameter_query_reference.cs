using System;
using System.Collections.Generic;
using System.Globalization;
using OpenMetaverse;
using LSL_Key = System.String;
using LSL_List = LSL_Types.list;

namespace LSL_Types
{
    public struct LSLInteger { public int value; }
    public struct LSLFloat { public double value; }
    public struct LSLString { public string m_string; }
    public struct Vector3 {}
    public struct Quaternion {}
    public class list
    {
        public object[] Data;
        public int Length => Data.Length;
        public list(params object[] items) { Data = items; }
        public list GetSublist(int start, int end)
        {
            if (end < 0) end += Length;
            if (start > end || start >= Length) return new list();
            return new list(Data[start..(end + 1)]);
        }
        /*INTEGER_METHOD*/
    }
}
enum PermissionMask : uint { Modify = 1u << 14 }
enum ThreatLevel { High }
interface ISceneEntity { UUID ID { get; } }
class SceneObjectPart : ISceneEntity
{
    public UUID ID { get; set; }
    public UUID OwnerID;
    public uint OwnerMask = (uint)PermissionMask.Modify;
    public SceneObjectGroup ParentGroup;
}
class ScenePresence : ISceneEntity { public UUID ID { get; set; } }
class SceneObjectGroup
{
    public bool IsDeleted;
    public SceneObjectPart RootPart;
}
class World
{
    public SceneObjectPart Part;
    public SceneObjectPart GetSceneObjectPart(UUID id) => Part != null && Part.ID == id ? Part : null;
}
class Api
{
    public SceneObjectPart m_host;
    public World World = new();
    public string Trace = "";
    public int Scenario, Kind;
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    SceneObjectPart Part(int n) => new() { ID = Id(n), ParentGroup = m_host.ParentGroup };
    ScenePresence Avatar(int n) => new() { ID = Id(n) };
    public List<SceneObjectPart> GetLinkParts(int link)
    {
        Trace += "P" + link + ";";
        return link switch
        {
            1 => new() { Part(1) }, 2 => new() { Part(2), Part(3) },
            3 => new() { Part(3) }, _ => new()
        };
    }
    public List<ScenePresence> GetLinkAvatars(int link)
    {
        Trace += "A" + link + ";";
        return link is 2 or 3 ? new() { Avatar(101 + link - 2) } : new();
    }
    public List<ISceneEntity> GetLinkEntities(SceneObjectPart part, int link)
    {
        Trace += "E" + link + ";";
        return link switch
        {
            1 => new() { Part(1) },
            2 => new() { Part(2), Part(3), Avatar(101) },
            3 => new() { Part(3), Avatar(102) }, _ => new()
        };
    }
    object Target() => Kind switch
    {
        1 => 2.9, 2 => new LSL_Types.LSLString { m_string = " 2 " },
        3 => new LSL_Types.LSLString { m_string = "2.5" },
        4 => new LSL_Types.Vector3(), 5 => new LSL_Types.Quaternion(),
        6 => new LSL_List(), 7 => 1.0e100,
        8 => new LSL_Types.LSLString { m_string = "2147483648" }, _ => 2
    };
    LSL_List Read(ISceneEntity entity, LSL_List rules, ref LSL_List result)
    {
        int n = Convert.ToInt32(entity.ID.ToString()[^12..], 16);
        int stage = rules.Length == 0 ? 0 : rules.GetIntegerItem(0);
        Trace += "R" + n + "," + rules.Length + ";";
        var values = new List<object>(result.Data);
        values.Add(n * 10 + stage);
        result = new LSL_List(values.ToArray());
        if (stage == 1)
            return Scenario == 2 ? new LSL_List(Target()) :
                   Scenario == 3 ? new LSL_List(Target(), 2, 99) : new LSL_List(Target(), 2);
        return stage == 2 ? new LSL_List(Scenario == 1 && n == 3 ? 4 : 3, 3) : new LSL_List();
    }
    public LSL_List GetPrimParams(SceneObjectPart part, LSL_List rules, ref LSL_List result) =>
        Read(part, rules, ref result);
    public LSL_List GetPrimParams(ScenePresence avatar, LSL_List rules, ref LSL_List result) =>
        Read(avatar, rules, ref result);
    void Error(string function, string message) { Trace += "ERR:" + message + ";"; }
    /*LSL_METHODS*/
}
class Ossl
{
    public Api m_LSL_Api;
    void CheckThreatLevel(ThreatLevel level, string function) {}
    void InitLSL() {}
    /*OSSL_METHOD*/
}
class Program
{
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var fields = Array.ConvertAll(line.Split(' '), int.Parse);
            var owner = new UUID("00000000-0000-0000-0000-000000000001");
            var root = new SceneObjectPart { ID = owner, OwnerID = owner };
            var group = new SceneObjectGroup { RootPart = root };
            root.ParentGroup = group;
            var api = new Api { m_host = root, Scenario = fields[3], Kind = fields[4] };
            api.World.Part = root;
            if (fields[2] == 1) group.IsDeleted = true;
            if (fields[2] == 2) root.OwnerMask = 0;
            if (fields[2] == 3) api.World.Part = null;
            var rules = fields[5] == 0 ? new LSL_List(1) : new LSL_List();
            try
            {
                var result = fields[0] switch
                {
                    0 => api.llGetPrimitiveParams(rules),
                    1 => api.llGetLinkPrimitiveParams(fields[1], rules),
                    2 => api.GetPrimitiveParamsEx(owner.ToString(), rules),
                    _ => new Ossl { m_LSL_Api = api }.osGetLinkPrimitiveParams(fields[1], rules)
                };
                Console.WriteLine(api.Trace + "|" +
                    string.Concat(Array.ConvertAll(result.Data, x => x.ToString() + ",")));
            }
            catch (Exception error) { Console.WriteLine(api.Trace + "|!" + error.GetType().Name); }
        }
    }
}
