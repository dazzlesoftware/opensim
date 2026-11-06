using System;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Text;
namespace log4net
{
    public interface ILog
    {
        void Debug(object s);
        void Info(object s);
    }
    public class Logger : ILog
    {
        public void Debug(object s)
        {
        }
        public void Info(object s)
        {
        }
    }
}
namespace OpenSim.Framework.ServiceAuth
{
    public interface IServiceAuth
    {
        void AddAuthorization(HttpRequestHeaders headers);
    }
}
namespace OpenSim.Framework
{
    public static class Util
    {
        public static int EnvironmentTickCount() => Environment.TickCount;
        public static int EnvironmentTickCountSubtract(int previous) => unchecked(Environment.TickCount -
                                                                                  previous);
        public static byte[] UTF8NBGetbytes(string value) => new UTF8Encoding(false).GetBytes(value);
    }
    public static partial class WebUtil
    {
        public static int RequestNumber = 0, DebugLevel = 0, LongCallTime = int.MaxValue;
        public static HttpClient GetNewGlobalHttpClient(int timeout) => new HttpClient(
            new SocketsHttpHandler { AllowAutoRedirect = true, MaxAutomaticRedirections = 10,
                                     AutomaticDecompression = System.Net.DecompressionMethods.None,
                                     UseCookies = false, ConnectTimeout = TimeSpan.FromSeconds(120) }) {
            Timeout = TimeSpan.FromSeconds(30)
        };
        public static void LogOutgoingDetail(string a, int b, string c)
        {
        }
        public static void LogResponseDetail(int a, string b)
        {
        }
    }
}
class RestFormsReference
{
    class Auth : OpenSim.Framework.ServiceAuth.IServiceAuth
    {
        public void AddAuthorization(HttpRequestHeaders headers) => headers.TryAddWithoutValidation(
            "Authorization",
            "Basic " + Convert.ToBase64String(Encoding.UTF8.GetBytes("service-user:service-password")));
    }
    static void Main(string[] args)
    {
        try
        {
            var result = OpenSim.Framework.SynchronousRestFormsRequester.MakeRequest(
                "POST", args[0], args.Length > 1 ? args[1] : "METHOD=probe",
                auth: args.Length > 2 ? new Auth() : null);
            Console.WriteLine("OK|" + Convert.ToHexString(Encoding.UTF8.GetBytes(result)).ToLowerInvariant());
        }
        catch
        {
            Console.WriteLine("EX");
        }
    }
}
