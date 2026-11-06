using System;
using System.Text;
using OpenMetaverse.StructuredData;
using OpenSim.Region.Framework.Scenes;
public class StateContext
{
    public OSDMap command;
    public string trace = "";
    internal ScenePresenceStateMachine machine;
    public ScenePresence owner;
    public bool mutated;
    public void Log(string op)
    {
        trace += op + ";";
        string mutation = command["mutation"].AsString();
        if (!mutated && mutation.StartsWith(op + "_"))
        {
            mutated = true;
            if (mutation.EndsWith("release"))
                owner = null;
            else
                machine.SetState(mutation.EndsWith("pre")       ? ScenePresenceState.PreRemove
                                 : mutation.EndsWith("removed") ? ScenePresenceState.Removed
                                                                : ScenePresenceState.Removing);
        }
        if (command["failure"].AsString() == op)
            throw new Exception(op);
    }
}
namespace OpenSim.Region.Framework.Scenes
{
    public class Scene
    {
        StateContext c;
        public Scene(StateContext ctx)
        {
            c = ctx;
        }
        public string Name
        {
            get {
                c.Log("scene");
                return c.command["null_scene_name"].AsBoolean() ? null : "Region";
            }
        }
    }
    public class ScenePresence
    {
        StateContext c;
        public ScenePresence(StateContext ctx)
        {
            c = ctx;
        }
        public string Name
        {
            get {
                c.Log("name");
                return c.command["null_name"].AsBoolean() ? null : "Avatar 試験";
            }
        }
        public Scene Scene => c.command["null_scene"].AsBoolean() ? null : new Scene(c);
    }
}
class StateReference
{
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var c = new StateContext { command = (OSDMap)OSDParser.DeserializeJson(
                                           Encoding.UTF8.GetString(Convert.FromHexString(line))) };
            c.owner = c.command["null_owner"].AsBoolean() ? null : new ScenePresence(c);
            var weak = new WeakReference(c.owner);
            c.machine = new(c.owner);
            string results = "";
            foreach (var step in (OSDArray)c.command["steps"])
            {
                try
                {
                    c.machine.SetState((ScenePresenceState)step.AsInteger());
                    results += "OK";
                }
                catch (Exception e)
                {
                    results += "EX:" + H(e.Message);
                }
                results += ":" + (int)c.machine.GetState() + ";";
            }
            GC.Collect();
            GC.WaitForPendingFinalizers();
            Console.WriteLine(results + "|" + H(c.trace) + "|" + (weak.IsAlive ? "1" : "0"));
            GC.KeepAlive(c.machine);
        }
    }
}
