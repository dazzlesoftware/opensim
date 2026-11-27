using System;
using System.Collections.Generic;
using System.Linq;

static class ScriptBaseClass
{
    public const int LINK_SET = -1, LINK_ROOT = 1, LINK_ALL_OTHERS = -2, LINK_ALL_CHILDREN = -3, LINK_THIS = -4;
}
interface ISceneEntity { int Id { get; } }
class SceneObjectPart : ISceneEntity
{
    public int Id { get; set; }
    public int LinkNum;
    public SceneObjectGroup ParentGroup;
}
class ScenePresence : ISceneEntity { public int Id { get; set; } }
class PartCollection : List<SceneObjectPart>
{
    public SceneObjectPart[] GetArray() => ToArray();
}
class SceneObjectGroup
{
    public PartCollection m_parts = new();
    public List<ScenePresence> m_sittingAvatars = new();
    public SceneObjectPart RootPart;
    public bool IsDeleted = false;
    public SceneObjectPart[] Parts => m_parts.GetArray();
    public int PrimCount => m_parts.Count;
    public int GetPartCount() => m_parts.Count;
    public string Trace = "";
    public List<ScenePresence> GetSittingAvatars()
    {
        Trace += "S;";
        return new(m_sittingAvatars);
    }
    public ScenePresence GetLinkSitingAvatar(int linknumber)
    {
        Trace += "I;";
        return OriginalGetLinkSitingAvatar(linknumber);
    }
    /*PART_LOOKUP*/
    /*AVATAR_LOOKUP*/
}
class Api
{
    /*METHODS*/
}
class Program
{
    static string Observe(int parts, int avatars, int seed, int host, int link, int mode)
    {
        var group = new SceneObjectGroup();
        for (int i = 0; i < parts; ++i)
        {
            int rank = i == 0 ? 0 : (i - 1 + (seed >> 1)) % (parts - 1);
            if (i > 0 && (seed & 1) != 0) rank = parts - 2 - rank;
            var part = new SceneObjectPart { Id = i + 1, LinkNum = i == 0 ? (parts == 1 ? 0 : 1) : rank + 2, ParentGroup = group };
            group.m_parts.Add(part);
            if (i == 0) group.RootPart = part;
        }
        for (int i = 0; i < avatars; ++i)
            group.m_sittingAvatars.Add(new() { Id = 100 + ((seed & 4) != 0 ? avatars - i - 1 : i) });
        var selectedHost = host < parts ? group.m_parts[host] : null;
        try
        {
            var result = new List<ISceneEntity>();
            if (mode == 2) result = Api.GetLinkEntities(selectedHost, link);
            else
            {
                result.AddRange(Api.GetLinkParts(selectedHost, link));
                if (mode == 1) result.AddRange(Api.GetLinkAvatars(link, selectedHost?.ParentGroup));
            }
            return string.Concat(result.Select(entity => (entity?.Id ?? 0) + ",")) + "|" + group.Trace;
        }
        catch (Exception error)
        {
            return "!" + error.GetType().FullName + ":" + error.Message + "|" + group.Trace;
        }
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ').Select(int.Parse).ToArray();
            Console.WriteLine(Observe(p[0], p[1], p[2], p[3], p[4], p[5]));
        }
    }
}
