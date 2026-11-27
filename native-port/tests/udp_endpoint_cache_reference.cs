using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Globalization;
using OpenSim.Framework;

static class Clock
{
    public static double Now;
    public static System.Threading.Timer Timer;
}

namespace System.Threading
{
    public sealed class Timer : IDisposable
    {
        readonly TimerCallback callback;
        readonly object state;
        public bool Disposed;
        public Timer(TimerCallback callback, object state, int due, int period)
        {
            if (due != 300000)
                throw new Exception("Wrong endpoint cache interval");
            this.callback = callback;
            this.state = state;
            Clock.Timer = this;
        }
        public bool Change(int due, int period)
        {
            if (due != 300000)
                throw new Exception("Wrong endpoint purge interval");
            return true;
        }
        public void Dispose() { Disposed = true; }
        public void Fire()
        {
            if (!Disposed)
                callback(state);
        }
    }
}

namespace OpenSim.Framework
{
    public static class Util
    {
        public static double GetTimeStampMS() { return Clock.Now; }
    }
}

class Address : SocketAddress
{
    readonly uint mask;
    public Address(int capacity, uint mask) : base(AddressFamily.InterNetwork, capacity) { this.mask = mask; }
    public override int GetHashCode() { return unchecked((int)((uint)base.GetHashCode() & mask)); }
}

class Api : IDisposable
{
    /*DUMMY_ENDPOINT*/
    static ExpiringCacheOS<SocketAddress, EndPoint> EndpointsCache;
    public Api() { EndpointsCache = new(/*CACHE_INTERVAL*/); }
    /*GET_ENDPOINT*/
    public int Count { get { return EndpointsCache.Count; } }
    public void Dispose() { EndpointsCache.Dispose(); }
}

class Program
{
    static void Observe(string line)
    {
        string[] commands = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
        uint mask = uint.Parse(commands[0]);
        Clock.Now = 0;
        Clock.Timer = null;
        using var api = new Api();
        var slots = new Dictionary<int, Address>();
        var identities = new Dictionary<EndPoint, int>(ReferenceEqualityComparer.Instance);
        var output = new List<string>();
        foreach (string command in commands[1..])
        {
            Address key = null;
            bool query = false;
            if (command.StartsWith('g') || command.StartsWith('a'))
            {
                string[] fields = command[1..].Split(',');
                int slot = int.Parse(fields[0]);
                byte[] bytes = Convert.FromHexString(fields[1]);
                if (!slots.TryGetValue(slot, out key))
                {
                    key = new Address(bytes.Length, mask);
                    slots.Add(slot, key);
                }
                if (key.Buffer.Length != bytes.Length)
                    throw new Exception("A socket address cannot change capacity");
                bytes.CopyTo(key.Buffer.Span);
                key.Size = int.Parse(fields[2]);
                query = command[0] == 'g';
            }
            else if (command.StartsWith('q'))
            {
                key = slots[int.Parse(command[1..])];
                query = true;
            }
            else if (command.StartsWith('t'))
                Clock.Now = double.Parse(command[1..], CultureInfo.InvariantCulture);
            else if (command == "f")
                Clock.Timer?.Fire();
            else
                throw new Exception("Unknown endpoint cache command");
            string observation = "-|-|-";
            if (query)
            {
                uint hash = unchecked((uint)key.GetHashCode());
                EndPoint endpoint = Api.GetEndPoint(key);
                if (!identities.TryGetValue(endpoint, out int identity))
                {
                    identity = identities.Count + 1;
                    identities.Add(endpoint, identity);
                }
                observation = $"{identity}|{endpoint}|{hash}";
            }
            bool active = Clock.Timer != null && !Clock.Timer.Disposed;
            output.Add($"{observation}|{api.Count}|{(active ? 1 : 0)}");
        }
        Console.WriteLine(string.Join(';', output));
    }

    static void Main()
    {
        uint seed = (uint)typeof(HashCode).GetField("s_seed", BindingFlags.NonPublic | BindingFlags.Static).GetValue(null);
        Console.WriteLine($"SEED {seed}");
        Console.WriteLine($"RUNTIME {Environment.Version}");
        Console.WriteLine($"SOCKET_ASSEMBLY {typeof(SocketAddress).Assembly.Location}");
        Console.WriteLine($"HASH_ASSEMBLY {typeof(HashCode).Assembly.Location}");
        string line;
        while ((line = Console.ReadLine()) != null)
            Observe(line);
    }
}
