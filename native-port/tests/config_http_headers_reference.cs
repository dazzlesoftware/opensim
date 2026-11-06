using System;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Text;
using System.Globalization;
class UnknownContent : HttpContent
{
    protected override bool TryComputeLength(out long length)
    {
        length = 0;
        return false;
    }
    protected override System.Threading.Tasks.Task SerializeToStreamAsync(
        Stream stream, TransportContext context) => System.Threading.Tasks.Task.CompletedTask;
}
class Program
{
    delegate bool Decoder(ReadOnlySpan<byte> bytes, out string value);
    static string H(string v)
    {
        if (v == null)
            return "~";
        var b = new StringBuilder();
        foreach (char c in v)
            b.Append(((int)c).ToString("x4"));
        return b.Length == 0 ? "-" : b.ToString();
    }
    static string Error(Exception e) => e.GetType().FullName + ":" +
                                        unchecked((uint)e.HResult).ToString("x8") + ":" + H(e.Message) + ":" +
                                        (e is HttpRequestException r ? (int)r.HttpRequestError : -1) + ":" +
                                        (e.InnerException == null ? "~" : Error(e.InnerException));
    static Func<object, byte[], Encoding, string> SelectedDecoder()
    {
        var type = typeof(HttpClient).Assembly.GetType("System.Net.Http.Headers.HeaderDescriptor");
        var method = new System.Reflection.Emit.DynamicMethod(
            "SourceHeaderValue", typeof(string), new[] { typeof(object), typeof(byte[]), typeof(Encoding) },
            typeof(Program).Module, true);
        var il = method.GetILGenerator();
        var local = il.DeclareLocal(type);
        il.Emit(System.Reflection.Emit.OpCodes.Ldarg_0);
        il.Emit(System.Reflection.Emit.OpCodes.Unbox_Any, type);
        il.Emit(System.Reflection.Emit.OpCodes.Stloc, local);
        il.Emit(System.Reflection.Emit.OpCodes.Ldloca, local);
        il.Emit(System.Reflection.Emit.OpCodes.Ldarg_1);
        il.Emit(System.Reflection.Emit.OpCodes.Newobj,
                typeof(ReadOnlySpan<byte>).GetConstructor(new[] { typeof(byte[]) }));
        il.Emit(System.Reflection.Emit.OpCodes.Ldarg_2);
        il.Emit(System.Reflection.Emit.OpCodes.Call,
                type.GetMethod("GetHeaderValue", System.Reflection.BindingFlags.Public |
                                                     System.Reflection.BindingFlags.Instance));
        il.Emit(System.Reflection.Emit.OpCodes.Ret);
        return method.CreateDelegate<Func<object, byte[], Encoding, string>>();
    }
    static string FullError(Exception e)
    {
        int code = e is HttpRequestException h ? (int)h.HttpRequestError
                   : e is HttpIOException io   ? (int)io.HttpRequestError
                                               : -1;
        bool retry = e.GetType()
                         .GetProperty("AllowRetry", System.Reflection.BindingFlags.Instance |
                                                        System.Reflection.BindingFlags.NonPublic)
                         ?.GetValue(e)
                         ?.ToString() == "RetryOnConnectionFailure";
        return e.GetType().FullName + ":" + unchecked((uint)e.HResult).ToString("x8") + ":" + H(e.Message) +
               ":" + code + ":" + (retry ? "1" : "0") + ":" +
               (e.InnerException == null ? "~" : FullError(e.InnerException));
    }
    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--map-eof")
        {
            var type = typeof(HttpClient).Assembly.GetType("System.Net.Http.HttpConnection");
            var field = type.GetField("_canRetry", System.Reflection.BindingFlags.Instance |
                                                       System.Reflection.BindingFlags.NonPublic);
            var method = type.GetMethod("MapSendException", System.Reflection.BindingFlags.Instance |
                                                                System.Reflection.BindingFlags.NonPublic);
            string mapRow;
            while ((mapRow = Console.ReadLine()) != null)
            {
                var instance = System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(type);
                GC.SuppressFinalize(instance);
                field.SetValue(instance, mapRow == "1");
                var parameters = new object[] { new HttpIOException(HttpRequestError.ResponseEnded,
                                                                    "The response ended prematurely."),
                                                System.Threading.CancellationToken.None, null };
                if (!(bool)method.Invoke(instance, parameters))
                    throw new Exception("Source mapping failed");
                Console.WriteLine(FullError((Exception)parameters[2]));
            }
            return;
        }
        if (args.Length > 0 && args[0] == "--selected-decode")
        {
            var type = typeof(HttpClient).Assembly.GetType("System.Net.Http.Headers.HeaderDescriptor");
            var get = type.GetMethod(
                "TryGet", System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.Static, null,
                new[] { typeof(string), type.MakeByRefType() }, null);
            var decode = SelectedDecoder();
            string selectedRow;
            while ((selectedRow = Console.ReadLine()) != null)
            {
                var a = selectedRow.Split(' ');
                var holder = new object[] { a[0] == "L" ? "Location" : "Set-Cookie", null };
                if (!(bool)get.Invoke(null, holder))
                    throw new Exception("Descriptor lookup failed");
                Encoding encoding = a[1] == "A"   ? Encoding.ASCII
                                    : a[1] == "L" ? Encoding.Latin1
                                                  : Encoding.UTF8;
                Console.WriteLine(H(decode(
                    holder[1], a[2] == "-" ? Array.Empty<byte>() : Convert.FromHexString(a[2]), encoding)));
            }
            return;
        }
        if (args.Length > 0 && args[0] == "--decode")
        {
            var type = typeof(HttpClient).Assembly.GetType("System.Net.Http.Headers.HeaderDescriptor");
            var decoder = type.GetMethod("TryDecodeUtf8", System.Reflection.BindingFlags.Static |
                                                              System.Reflection.BindingFlags.NonPublic)
                              .CreateDelegate<Decoder>();
            string decodeRow;
            while ((decodeRow = Console.ReadLine()) != null)
            {
                var bytes = decodeRow == "-" ? Array.Empty<byte>() : Convert.FromHexString(decodeRow);
                Console.WriteLine(
                    H(decoder(bytes, out string value) ? value : Encoding.Latin1.GetString(bytes)));
            }
            return;
        }
        if (args.Length > 0 && args[0] == "--framing")
        {
            string line;
            while ((line = Console.ReadLine()) != null)
            {
                using var r = new HttpResponseMessage();
                r.Content = new UnknownContent();
                foreach (var op in line.Split('|'))
                {
                    if (op.Length < 2)
                        continue;
                    var text = new StringBuilder();
                    var value = op.Substring(2);
                    if (value != "-")
                        for (int i = 0; i < value.Length; i += 4)
                            text.Append((char)Convert.ToInt32(value.Substring(i, 4), 16));
                    if (op[0] == 'C')
                        r.Content.Headers.TryAddWithoutValidation("Content-Length", text.ToString());
                    else
                        r.Headers.TryAddWithoutValidation("Transfer-Encoding", text.ToString());
                }
                Console.WriteLine((r.Headers.TransferEncodingChunked == true ? "chunked" : "other") + ":" +
                                  (r.Content.Headers.ContentLength?.ToString() ?? "~"));
            }
            return;
        }
        if (args.Length > 0 && args[0] == "--encode")
        {
            var type = typeof(HttpClient).Assembly.GetType("System.Net.Http.HttpConnection");
            var method = type.GetMethod("WriteString", System.Reflection.BindingFlags.Instance |
                                                           System.Reflection.BindingFlags.NonPublic);
            string value;
            while ((value = Console.ReadLine()) != null)
            {
                var text = new StringBuilder();
                if (value != "-")
                    for (int i = 0; i < value.Length; i += 4)
                        text.Append((char)Convert.ToInt32(value.Substring(i, 4), 16));
                try
                {
                    var instance =
                        System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(type);
                    GC.SuppressFinalize(instance);
                    var buffer = type.GetField("_writeBuffer", System.Reflection.BindingFlags.Instance |
                                                                   System.Reflection.BindingFlags.NonPublic);
                    buffer.SetValue(instance,
                                    Activator.CreateInstance(buffer.FieldType,
                                                             System.Reflection.BindingFlags.Instance |
                                                                 System.Reflection.BindingFlags.Public |
                                                                 System.Reflection.BindingFlags.NonPublic,
                                                             null, new object[] { 4096, false }, null));
                    method.Invoke(instance, new object[] { text.ToString(), null });
                    Console.WriteLine("ok");
                }
                catch (System.Reflection.TargetInvocationException e)
                {
                    Console.WriteLine(Error(e.InnerException));
                }
            }
            return;
        }
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            try
            {
                using var handler = new HttpClientHandler { UseProxy = false };
                using var client = new HttpClient(handler);
                client.Timeout = TimeSpan.FromSeconds(4);
                using var request = new HttpRequestMessage(HttpMethod.Get, row);
                using var response = client.Send(request, HttpCompletionOption.ResponseHeadersRead);
                var b = new StringBuilder();
                b.Append("ok:")
                    .Append((int)response.StatusCode)
                    .Append(':')
                    .Append(response.Version)
                    .Append(':')
                    .Append(H(response.ReasonPhrase));
                foreach (var h in response.Headers.NonValidated)
                {
                    b.Append("/H:").Append(H(h.Key));
                    foreach (var v in h.Value)
                        b.Append(':').Append(H(v));
                }
                foreach (var h in response.Content.Headers.NonValidated)
                {
                    b.Append("/C:").Append(H(h.Key));
                    foreach (var v in h.Value)
                        b.Append(':').Append(H(v));
                }
                Console.WriteLine(b);
            }
            catch (Exception e)
            {
                Console.WriteLine(Error(e));
            }
        }
    }
}
