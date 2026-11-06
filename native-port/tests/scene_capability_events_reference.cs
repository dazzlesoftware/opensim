using System;
using OpenMetaverse;
using OpenSim.Region.Framework.Scenes;
using OpenSim.Framework.Capabilities;
namespace OpenSim.Region.Framework.Scenes
{
    public class Scene
    {
    }
}
namespace OpenSim.Framework.Capabilities
{
    public class Caps
    {
    }
}
public class EventLog
{
    public void ErrorFormat(string format, params object[] args)
    {
        Fixture.Current.Trace += "error:" + format.Split("{0}")[0] + args[0] + ";";
        if ((Fixture.Current.Profile & 64) != 0)
            throw new Exception("logger");
    }
}
public class Fixture
{
    public static Fixture Current;
    public int Profile, Depth;
    public bool Mutated;
    public char Selected = 'r';
    public string Trace = "";
    public EventManager Events = new();
    public Caps CapsPayload;
    public Scene ScenePayload;
    EventManager.RegisterCapsEvent[] register = new EventManager.RegisterCapsEvent[4];
    EventManager.DeregisterCapsEvent[] deregister = new EventManager.DeregisterCapsEvent[4];
    EventManager.ClientClosed[] closed = new EventManager.ClientClosed[4];
    public Fixture(int p)
    {
        Profile = p;
        if ((p & 8) != 0)
            CapsPayload = new();
        if ((p & 16) != 0)
            ScenePayload = new();
        for (int i = 1; i <= 3; i++)
        {
            int n = i;
            register[i] = (a, c) => Invoke(n, 'r', a, c != null);
            deregister[i] = (a, c) => Invoke(n, 'e', a, c != null);
            closed[i] = (a, s) => Invoke(n, 'h', a, s != null);
        }
    }
    void Invoke(int n, char channel, UUID agent, bool value)
    {
        Trace += $"{channel}{n}:{agent}:{(value?1:0)};";
        if (n == 1 && !Mutated && (Profile & 6) != 0)
        {
            Mutated = true;
            if ((Profile & 2) != 0)
                Add(channel, 3);
            if ((Profile & 4) != 0)
                Remove(channel, 2);
        }
        if (n == 1 && (Profile & 32) != 0 && Depth == 0)
        {
            ++Depth;
            Trigger('D');
            --Depth;
        }
        if ((n == 2 && (Profile & 1) != 0) || (n == 3 && (Profile & 128) != 0))
            throw new Exception("failure" + n);
    }
    public void Add(char c, int n)
    {
        if (c == 'r')
            Events.OnRegisterCaps += register[n];
        else if (c == 'e')
            Events.OnDeregisterCaps += deregister[n];
        else
            Events.OnClientClosed += closed[n];
    }
    public void Remove(char c, int n)
    {
        if (c == 'r')
            Events.OnRegisterCaps -= register[n];
        else if (c == 'e')
            Events.OnDeregisterCaps -= deregister[n];
        else
            Events.OnClientClosed -= closed[n];
    }
    public void Range(bool remove)
    {
        if (Selected == 'r')
        {
            var d = register[1] + register[2];
            if (remove)
                Events.OnRegisterCaps -= d;
            else
                Events.OnRegisterCaps += d;
        }
        else if (Selected == 'e')
        {
            var d = deregister[1] + deregister[2];
            if (remove)
                Events.OnDeregisterCaps -= d;
            else
                Events.OnDeregisterCaps += d;
        }
        else
        {
            var d = closed[1] + closed[2];
            if (remove)
                Events.OnClientClosed -= d;
            else
                Events.OnClientClosed += d;
        }
    }
    public void Trigger(char c)
    {
        UUID a = new("00000000-0000-0000-0000-000000000007");
        if (c == 'R')
            Events.TriggerOnRegisterCaps(a, CapsPayload);
        else if (c == 'D')
            Events.TriggerOnDeregisterCaps(a, CapsPayload);
        else
            Events.TriggerClientClosed(a, ScenePayload);
    }
    public static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            int split = line.IndexOf(':');
            var f = new Fixture(int.Parse(line[..split]));
            Current = f;
            foreach (char c in line[(split + 1)..])
            {
                try
                {
                    switch (c)
                    {
                    case 'r':
                    case 'e':
                    case 'h':
                        f.Selected = c;
                        break;
                    case 'a':
                    case 'd':
                        f.Add(f.Selected, 1);
                        break;
                    case 'b':
                        f.Add(f.Selected, 2);
                        break;
                    case 'c':
                        f.Add(f.Selected, 3);
                        break;
                    case 'x':
                        f.Remove(f.Selected, 1);
                        break;
                    case 'y':
                        f.Remove(f.Selected, 2);
                        break;
                    case 'm':
                        f.Range(false);
                        break;
                    case 'n':
                        f.Range(true);
                        break;
                    case '0':
                        f.Events.OnRegisterCaps += null;
                        break;
                    default:
                        f.Trigger(c);
                        break;
                    }
                }
                catch
                {
                    f.Trace += "EX;";
                }
            }
            Console.WriteLine(f.Trace);
        }
    }
}
