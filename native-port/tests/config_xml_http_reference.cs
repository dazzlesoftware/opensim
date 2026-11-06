using System;
using System.IO;
using System.Xml;
class Program
{
    static string H(string value)
    {
        var b = new System.Text.StringBuilder();
        foreach (char c in value)
            b.Append(((int)c).ToString("x4"));
        return b.Length == 0 ? "-" : b.ToString();
    }
    static string Error(Exception e)
    {
        var h = e as System.Net.Http.HttpRequestException;
        int code = h != null                                 ? (int)h.HttpRequestError
                   : e is System.Net.Http.HttpIOException io ? (int)io.HttpRequestError
                                                             : -1;
        var retry = e.GetType()
                        .GetProperty("AllowRetry", System.Reflection.BindingFlags.Instance |
                                                       System.Reflection.BindingFlags.NonPublic)
                        ?.GetValue(e)
                        ?.ToString() == "RetryOnConnectionFailure";
        return e.GetType().FullName + ":" + unchecked((uint)e.HResult).ToString("x8") + ":" + H(e.Message) +
               ":" + code + ":" +
               (h != null && h.StatusCode.HasValue ? ((int)h.StatusCode.Value).ToString() : "~") + ":" +
               (retry ? "1" : "0") + ":" + (e.InnerException == null ? "~" : Error(e.InnerException));
    }
    static void Meta(Exception e)
    {
        if (e is System.Net.Http.HttpRequestException)
            Console.Error.WriteLine(
                "RETRY:" + e.GetType()
                               .GetProperty("AllowRetry", System.Reflection.BindingFlags.Instance |
                                                              System.Reflection.BindingFlags.NonPublic)
                               ?.GetValue(e));
        if (e is System.Net.Sockets.SocketException s)
            Console.Error.WriteLine("SOCKET:" + s.NativeErrorCode + ":" + (int)s.SocketErrorCode + ":" +
                                    s.ErrorCode + ":" + s.HResult);
        if (e.InnerException != null)
            Meta(e.InnerException);
    }
    static Stream ActualStream(Uri uri)
    {
        var method = typeof(XmlReader)
                         .Assembly.GetType("System.Xml.XmlDownloadManager", true)
                         .GetMethod("GetStream", System.Reflection.BindingFlags.NonPublic |
                                                     System.Reflection.BindingFlags.Static);
        try
        {
            return (Stream)method.Invoke(null, new object[] { uri, null, null });
        }
        catch (System.Reflection.TargetInvocationException e) when (e.InnerException != null)
        {
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(e.InnerException).Throw();
            throw;
        }
    }
    static void Main()
    {
        System.Net.Http.HttpClient.DefaultProxy = new System.Net.WebProxy();
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            try
            {
                using var stream = ActualStream(new Uri(row));
                using var memory = new MemoryStream();
                stream.CopyTo(memory);
                Console.WriteLine("ok:" + Convert.ToHexString(memory.ToArray()).ToLowerInvariant());
            }
            catch (Exception e)
            {
                Meta(e);
                Console.Error.WriteLine(
                    row + " " +
                    e.GetType()
                        .GetProperty("AllowRetry", System.Reflection.BindingFlags.NonPublic |
                                                       System.Reflection.BindingFlags.Instance)
                        ?.GetValue(e));
                Console.WriteLine(Error(e));
            }
        }
    }
}
