// Test adapters only. No OpenSim algorithm is implemented here.
using System;
using System.Collections.Generic;
namespace log4net {
    public interface ILog {
        void WarnFormat(string format, params object[] args); void DebugFormat(string format, params object[] args);
        void Warn(string message); void Info(string message);
    }
    public sealed class TestLog : ILog {
        public static string LastMessage;
        public void WarnFormat(string format, params object[] args) { }
        public void DebugFormat(string format, params object[] args) { LastMessage = string.Format(format, args); }
        public void Warn(string message) { LastMessage = message; }
        public void Info(string message) { LastMessage = message; }
    }
    public static class LogManager { public static ILog GetLogger(Type type) => new TestLog(); }
}
namespace OpenSim.Framework {
    public static partial class Util {
        public static double Now; public static int TickNow;
        public static double GetTimeStamp() => Now;
        public static int EnvironmentTickCount() => TickNow & 0x3fffffff;
    }
}
// A manually driven scheduler lets the unchanged protector's actual Elapsed
// callback run deterministically. No rate-limit or cleanup logic lives here.
namespace System.Timers {
    public sealed class Timer {
        public event EventHandler Elapsed;
        public bool Enabled { get; set; }
        public double Interval { get; set; }
        public void Fire() { if (Enabled) Elapsed?.Invoke(this, EventArgs.Empty); }
    }
}
namespace Nini.Config {
    public interface IConfig { int GetInt(string key, int fallback); }
    public interface IConfigSource { Dictionary<string,IConfig> Configs { get; } }
    public sealed class TestConfig : IConfig {
        public int Mode, Client, Minimum;
        public int GetInt(string key, int fallback) {
            if (key == "client_throttle_max_bps") {
                if (Mode == 2) throw new FormatException();
                return Mode == 4 ? fallback : Client;
            }
            if (Mode == 3) throw new FormatException();
            return Mode == 4 ? fallback : Minimum;
        }
    }
    public sealed class TestConfigSource : IConfigSource {
        public Dictionary<string,IConfig> Configs { get; } = new();
    }
}
