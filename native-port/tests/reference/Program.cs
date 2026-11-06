// Executes the original C# implementations linked by compare.py.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using OpenSim.Framework;
using OpenSim.Region.ClientStack.LindenUDP;
using Nini.Config;

sealed class ProbeCrc : Crc32
{
    public ProbeCrc(uint polynomial, uint seed) : base(polynomial, seed)
    {
    }
    public void Core(byte[] bytes, int start, int length) => HashCore(bytes, start, length);
    public byte[] Final() => HashFinal();
}
static class Program
{
    static readonly FieldInfo Tokens =
        typeof(TokenBucket).GetField("m_tokenCount", BindingFlags.Instance | BindingFlags.NonPublic);
    static void Emit(params object[] values)
    {
        Console.WriteLine(string.Join(
            "|",
            Array.ConvertAll(values, v => v is float f ? f.ToString("R", CultureInfo.InvariantCulture)
                                                       : Convert.ToString(v, CultureInfo.InvariantCulture))));
    }
    static void State(TokenBucket b, int result) => Emit("bucket", result, b.RequestedDripRate,
                                                         b.TotalDripRequest, b.DripRate, b.MaxDripRate,
                                                         b.RequestedBurst, b.Burst, Tokens.GetValue(b));
    static void Main(string[] args)
    {
        if (args.Length == 1 && args[0] == "--runtime")
        {
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(
                new { framework = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
                      architecture =
                          System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
                      os = System.Runtime.InteropServices.RuntimeInformation.OSDescription }));
            return;
        }
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        var buckets = new Dictionary<int, TokenBucket>();
        var buffers = new Dictionary<int, CircularBuffer<int>>();
        var protectors = new Dictionary<int, BasicDOSProtector>();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            var op = p[0];
            if (op == "ticktime")
            {
                Util.TickNow = int.Parse(p[1]);
                Emit("ticktime");
            }
            else if (op == "ticks")
            {
                Util.TickNow = int.Parse(p[1]);
                Emit("ticks", Util.EnvironmentTickCountSubtract(int.Parse(p[2]), int.Parse(p[3])),
                     Util.EnvironmentTickCountCompare(int.Parse(p[2]), int.Parse(p[3])));
            }
            else if (op.StartsWith("dos_"))
            {
                int id = int.Parse(p[1]);
                string result = "-";
                try
                {
                    if (op == "dos_new")
                        protectors[id] = new BasicDOSProtector(new BasicDosProtectorOptions {
                            MaxRequestsInTimeframe = int.Parse(p[2]),
                            RequestTimeSpan = TimeSpan.FromMilliseconds(double.Parse(p[3])),
                            ForgetTimeSpan = TimeSpan.FromMilliseconds(double.Parse(p[4])),
                            MaxConcurrentSessions = int.Parse(p[5]),
                            ThrottledAction = (BasicDOSProtector.ThrottleAction) int.Parse(p[6])
                        });
                    else
                    {
                        var protector = protectors[id];
                        if (op == "dos_process")
                            result = protector.Process(p[2], "/test") ? "1" : "0";
                        else if (op == "dos_end")
                            protector.ProcessEnd(p[2], "/test");
                        else if (op == "dos_cleanup")
                            ((System.Timers.Timer) typeof(BasicDOSProtector)
                                 .GetField("_forgetTimer", BindingFlags.Instance | BindingFlags.NonPublic)
                                 .GetValue(protector))
                                .Fire();
                    }
                }
                catch (System.Security.SecurityException)
                {
                    result = "throttled";
                }
                catch (Exception)
                {
                    result = "error";
                }
                Console.Write("dos|" + result);
                if (protectors.TryGetValue(id, out var current))
                {
                    var timer = (System.Timers.Timer) typeof(BasicDOSProtector)
                                    .GetField("_forgetTimer", BindingFlags.Instance | BindingFlags.NonPublic)
                                    .GetValue(current);
                    Console.Write("|" + (timer.Enabled ? 1 : 0) + "|" + (current.IsBlocked("A") ? 1 : 0) +
                                  "|" + (current.IsBlocked("B") ? 1 : 0) + "|" +
                                  (current.IsBlocked("C") ? 1 : 0));
                }
                Console.WriteLine();
            }
            else if (op.StartsWith("buffer_"))
            {
                int id = int.Parse(p[1]);
                string result = "-";
                try
                {
                    if (op == "buffer_new")
                        buffers[id] = new CircularBuffer<int>(int.Parse(p[2]), p[3] != "0");
                    else
                    {
                        var b = buffers[id];
                        if (op == "buffer_single")
                            b.Put(int.Parse(p[2]));
                        else if (op == "buffer_resize")
                            b.Capacity = int.Parse(p[2]);
                        else if (op == "buffer_skip")
                            b.Skip(int.Parse(p[2]));
                        else if (op == "buffer_clear")
                            b.Clear();
                        else if (op == "buffer_overflow")
                            b.AllowOverflow = p[2] != "0";
                        else if (op == "buffer_get")
                            result = b.Get().ToString();
                        else if (op == "buffer_remove")
                            result = ((ICollection<int>)b).Remove(int.Parse(p[2])) ? "1" : "0";
                        else if (op == "buffer_contains")
                            result = b.Contains(int.Parse(p[2])) ? "1" : "0";
                        else if (op == "buffer_raw")
                            b.GetBuffer()[int.Parse(p[2])] = int.Parse(p[3]);
                        else if (op == "buffer_bulk")
                        {
                            int[] values = Array.ConvertAll(p[4..], int.Parse);
                            result = b.Put(values, int.Parse(p[2]), int.Parse(p[3])).ToString();
                        }
                        else if (op == "buffer_take" || op == "buffer_read" || op == "buffer_copy")
                        {
                            int[] values;
                            if (op == "buffer_take")
                                values = b.Get(int.Parse(p[2]));
                            else if (op == "buffer_read")
                            {
                                values = new int[int.Parse(p[4])];
                                Array.Fill(values, -99);
                                result = b.Get(values, int.Parse(p[2]), int.Parse(p[3])).ToString() + ":";
                            }
                            else
                            {
                                values = new int[int.Parse(p[5])];
                                Array.Fill(values, -99);
                                b.CopyTo(int.Parse(p[2]), values, int.Parse(p[3]), int.Parse(p[4]));
                            }
                            if (result == "-")
                                result = "";
                            foreach (int value in values)
                                result += value.ToString() + ",";
                        }
                    }
                }
                catch (Exception)
                {
                    result = "error";
                }
                Console.Write("buffer|" + result);
                if (buffers.TryGetValue(id, out var buffer))
                {
                    Console.Write("|" + buffer.Capacity + "|" + buffer.Size + "|");
                    foreach (int value in buffer.GetBuffer())
                        Console.Write(value.ToString() + ",");
                    Console.Write("|");
                    try
                    {
                        foreach (int value in buffer)
                            Console.Write(value.ToString() + ",");
                    }
                    catch (Exception)
                    {
                        Console.Write("error");
                    }
                }
                Console.WriteLine();
            }
            else if (op == "prime")
            {
                int n = int.Parse(p[1]);
                Emit("prime", PrimeNumberHelper.IsPrime(n) ? 1 : 0, PrimeNumberHelper.GetPrime(n));
            }
            else if (op == "perm")
            {
                uint source = uint.Parse(p[1]), target = uint.Parse(p[2]), folded = target, nomod = target;
                PermissionsUtil.ApplyFoldedPermissions(source, ref folded);
                PermissionsUtil.ApplyNoModFoldedPermissions(source, ref nomod);
                var display =
                    typeof(PermissionsUtil)
                        .GetMethod("PermissionsToString", BindingFlags.Static | BindingFlags.NonPublic);
                Emit("perm", folded, nomod, PermissionsUtil.FixAndFoldPermissions(target),
                     display.Invoke(null, new object[] { target }));
            }
            else if (op == "log")
            {
                PermissionsUtil.LogPermissions("object", "transfer", uint.Parse(p[1]), uint.Parse(p[2]),
                                               uint.Parse(p[3]));
                Emit("log", log4net.TestLog.LastMessage);
            }
            else if (op == "crc")
            {
                uint poly = uint.Parse(p[1]), seed = uint.Parse(p[2]);
                byte[] bytes = p[3] == "-" ? Array.Empty<byte>() : Convert.FromHexString(p[3]);
                var crc = new ProbeCrc(poly, seed);
                crc.Core(bytes, int.Parse(p[4]), int.Parse(p[5]));
                var digest = crc.Final();
                uint value =
                    ((uint)digest[0] << 24) | ((uint)digest[1] << 16) | ((uint)digest[2] << 8) | digest[3];
                crc.Initialize();
                int mid = bytes.Length / 2;
                crc.Core(bytes[..mid], 0, mid);
                crc.Core(bytes[mid..], 0, bytes.Length - mid);
                var incremental = crc.Final();
                uint segmented = ((uint)incremental[0] << 24) | ((uint)incremental[1] << 16) |
                                 ((uint)incremental[2] << 8) | incremental[3];
                Emit("crc", Crc32.Compute(poly, seed, bytes), value, digest[0], digest[1], digest[2],
                     digest[3], segmented);
            }
            else if (op == "throttle")
            {
                int mode = int.Parse(p[1]);
                var config = new TestConfigSource();
                config.Configs["ClientStack.LindenUDP"] =
                    mode == 0
                        ? null
                        : new TestConfig { Mode = mode, Client = int.Parse(p[2]), Minimum = int.Parse(p[3]) };
                var rates = new ThrottleRates(config);
                var values = new List<object> { "throttle",
                                                rates.ClientMaxRate,
                                                rates.MinimumAdaptiveThrottleRate,
                                                rates.AdaptiveThrottlesEnabled ? 1 : 0,
                                                rates.Total,
                                                rates.BurstTime };
                for (int i = -2; i <= 8; ++i)
                    values.Add(rates.GetRate((ThrottleOutPacketType)i));
                Emit(values.ToArray());
            }
            else if (op == "time")
            {
                Util.Now = double.Parse(p[1]);
                Emit("time");
            }
            else if (op == "new")
            {
                int id = int.Parse(p[1]), parent = int.Parse(p[2]), adaptive = int.Parse(p[5]);
                TokenBucket ancestor = parent < 0 ? null : buckets[parent];
                float rate = float.Parse(p[3]), burst = float.Parse(p[4]);
                buckets[id] = adaptive < 0 ? new TokenBucket(ancestor, rate, burst)
                                           : new AdaptiveTokenBucket(ancestor, rate, burst, adaptive != 0);
                State(buckets[id], -1);
            }
            else
            {
                var b = buckets[int.Parse(p[1])];
                int result = -1;
                if (op == "remove")
                    result = b.RemoveTokens(int.Parse(p[2])) ? 1 : 0;
                else if (op == "check")
                    result = b.CheckTokens(int.Parse(p[2])) ? 1 : 0;
                else if (op == "bytes")
                    result = b.GetCatBytesCanSend(int.Parse(p[2]));
                else if (op == "unregister")
                    b.UnregisterRequest(buckets[int.Parse(p[2])]);
                else if (op != "state")
                {
                    float value = float.Parse(p[2]);
                    if (op == "rate")
                        b.RequestedDripRate = value;
                    else if (op == "max")
                        b.MaxDripRate = value;
                    else if (op == "burst")
                        b.RequestedBurst = value;
                    else if (op == "total")
                        b.TotalDripRequest = value;
                    else
                    {
                        var a = (AdaptiveTokenBucket)b;
                        if (op == "adjust")
                            a.AdjustedDripRate = value;
                        else if (op == "ack")
                            a.AcknowledgePackets((int)value);
                        else if (op == "expire")
                            a.ExpirePackets((int)value);
                        else if (op == "enabled")
                            a.AdaptiveEnabled = value != 0;
                        else
                            throw new ArgumentException(op);
                    }
                }
                State(b, result);
            }
        }
    }
}
