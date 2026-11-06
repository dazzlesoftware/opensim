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
    static void Main()
    {
        System.Net.Http.HttpClient.DefaultProxy = new System.Net.WebProxy();
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            try
            {
                using var stream = XmlDownloadManager.GetStream(new Uri(row), null, null);
                using var memory = new MemoryStream();
                stream.CopyTo(memory);
                Console.WriteLine("ok:" + Convert.ToHexString(memory.ToArray()).ToLowerInvariant());
            }
            catch (Exception e)
            {
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
