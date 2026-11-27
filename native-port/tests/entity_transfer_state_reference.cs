using System;
using System.Text;

namespace log4net {
    public interface ILog {
        void DebugFormat(string format, params object[] args);
        void WarnFormat(string format, params object[] args);
        void ErrorFormat(string format, params object[] args);
    }
    public static class LogManager {
        sealed class Logger : ILog {
            void Write(string level, string format, object[] args) {
                if (TransferFixture.FailLog) throw new Exception("log failed");
                TransferFixture.OnLog();
                TransferFixture.Logs += level + ":" + string.Format(format, args) + "\n";
            }
            public void DebugFormat(string f, params object[] a) => Write("Debug", f, a);
            public void WarnFormat(string f, params object[] a) => Write("Warn", f, a);
            public void ErrorFormat(string f, params object[] a) => Write("Error", f, a);
        }
        public static ILog GetLogger(Type type) => new Logger();
    }
}
namespace OpenSim.Region.CoreModules.Framework.EntityTransfer {
    public class EntityTransferModule {
        public SceneStub Scene = new();
        public bool WaitForAgentArrivedAtDestination => TransferFixture.Wait;
    }
    public class SceneStub { public RegionStub RegionInfo = new(); }
    public class RegionStub {
        public string RegionName {
            get {
                if (TransferFixture.FailRegion) throw new Exception("region failed");
                return "Test Region";
            }
        }
    }
}
public static class Constants { public const int RegionSize = 256; }
public static class TransferFixture {
    public static bool Wait, FailRegion, FailLog, ResetLog;
    public static string Logs;
    static int sleeps, at;
    static char action;
    static readonly Guid Agent = Guid.Parse("11111111-1111-4111-8111-111111111111");
    static OpenSim.Region.CoreModules.Framework.EntityTransfer.EntityTransferStateMachine machine;
    public static void OnLog() {
        if (ResetLog) machine.ResetFromTransit(Agent);
    }
    public static void Sleep(int milliseconds) {
        if (milliseconds != 100) throw new Exception("Wrong sleep duration");
        if (++sleeps != at) return;
        if (action == 'h') machine.SetAgentArrivedAtDestination(Agent);
        else if (action == 'x') machine.ResetFromTransit(Agent);
        else throw new Exception("sleep failed");
    }
    /*OUTSIDE_VIEW*/
    public static void Main(string[] args) {
        if (args.Length == 1 && args[0] == "--outside-view") {
            string value;
            while ((value = Console.ReadLine()) != null) {
                string[] p = value.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                Console.WriteLine(IsOutsideView(BitConverter.UInt32BitsToSingle(uint.Parse(p[0])),
                    uint.Parse(p[1]), uint.Parse(p[2]), uint.Parse(p[3]), uint.Parse(p[4]),
                    int.Parse(p[5]), int.Parse(p[6]), int.Parse(p[7]), int.Parse(p[8])) ? "1" : "0");
            }
            return;
        }
        string line;
        while ((line = Console.ReadLine()) != null) {
            Wait = true; FailRegion = false; FailLog = false; ResetLog = false;
            Logs = ""; sleeps = 0; at = 0; action = 'h';
            machine = new(new OpenSim.Region.CoreModules.Framework.EntityTransfer.EntityTransferModule());
            bool first = true;
            foreach (string command in line.Split(' ', StringSplitOptions.RemoveEmptyEntries)) {
                string result = "-";
                try {
                    char c = command[0];
                    if (c == 's') result = machine.SetInTransit(Agent) ? "T" : "F";
                    else if (c == 'u') result = machine.UpdateInTransit(Agent,
                        (OpenSim.Region.CoreModules.Framework.EntityTransfer.AgentTransferState)int.Parse(command.Substring(1))) ? "T" : "F";
                    else if (c == 'r') result = machine.ResetFromTransit(Agent) ? "T" : "F";
                    else if (c == 'a') machine.SetAgentArrivedAtDestination(Agent);
                    else if (c == 'w') result = machine.WaitForAgentArrivedAtDestination(Agent) ? "T" : "F";
                    else if (c == 'm') Wait = command[1] == '1';
                    else if (c == 'p') machine.EnableWaitForAgentArrivedAtDestination = command[1] == '1';
                    else if (c == 'b') FailRegion = command[1] == '1';
                    else if (c == 'l') FailLog = command[1] == '1';
                    else if (c == 'z') ResetLog = command[1] == '1';
                    else if (c == 'h' || c == 'x' || c == 'f') {
                        action = c; at = int.Parse(command.Substring(1)); sleeps = 0;
                    } else if (c != 'g') throw new Exception("Unknown command");
                } catch (Exception error) { result = "E:" + Convert.ToHexString(Encoding.UTF8.GetBytes(error.Message)); }
                var state = machine.GetAgentTransferState(Agent);
                if (!first) Console.Write(';');
                first = false;
                Console.Write(result + "|" + (state.HasValue ? ((int)state.Value).ToString() : "N")
                    + "|" + sleeps + "|" + Convert.ToHexString(Encoding.UTF8.GetBytes(Logs)));
            }
            Console.WriteLine();
        }
    }
}
