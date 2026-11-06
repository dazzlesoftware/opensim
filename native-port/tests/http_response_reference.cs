using System;
using System.IO;
using System.Text;
using System.Collections;
using System.Net;
using System.Reflection;
using System.Text.Json;
using OSHttpServer;
public static class Host
{
    public static JsonElement Input;
    public static string Trace = "";
    public static int Failed;
    public static bool Mutated;
    public static HttpResponse Response;
    public static void Event(string op, string detail = "")
    {
        Trace += op + (detail.Length == 0 ? "" : ":" + detail) + ";";
        if (Input.TryGetProperty("failure", out var f) && f.GetString() == op &&
            ++Failed == (Input.TryGetProperty("nth", out var n) ? n.GetInt32() : 1))
            throw new Exception("injected");
        if (!Mutated && Input.TryGetProperty("at", out var at) && at.GetString() == op)
        {
            Mutated = true;
            if (Response != null)
            {
                var action = Input.GetProperty("mutation").GetString();
                if (action == "clear")
                    Response.Clear();
                if (action == "continue")
                    Response.CheckSendNextAsyncContinue();
            }
        }
    }
    public static bool Bool(string key) => Input.TryGetProperty(key, out var v) && v.GetBoolean();
    public static string Hex(string s) =>
        s == null ? "N" : Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    public static string Bytes(byte[] b) => b == null ? "N" : Convert.ToHexString(b).ToLowerInvariant();
    public static string Name(JsonElement e) =>
        e.ValueKind == JsonValueKind.Null
            ? null
            : Encoding.UTF8.GetString(Convert.FromHexString(e.GetProperty("h").GetString()));
}
public static class ResponseClock
{
    public static DateTime LocalNow
    {
        get {
            Host.Event("local");
            return new DateTime(Host.Input.TryGetProperty("local", out var v) ? long.Parse(v.GetString())
                                                                              : 638397614450000000);
        }
    }
    public static DateTime UtcNow
    {
        get {
            Host.Event("utc");
            return new DateTime(Host.Input.TryGetProperty("utc", out var v) ? long.Parse(v.GetString())
                                                                            : 638397614450000000);
        }
    }
}
namespace OSHttpServer
{
    public interface IHttpClientContext
    {
        bool IsClosing { get; }
        int MaxRequests { get; }
        int TimeoutKeepAlive { get; set; }
        void StartSendResponse(HttpResponse response);
        bool SendAsyncStart(byte[] data, int start, int length);
        void EndSendResponse(uint id, ConnectionType connection);
        void ContinueSendResponse();
    }
    public interface IHttpRequest
    {
        string HttpVersion { get; }
        IHttpClientContext Context { get; }
        ConnectionType Connection { get; }
        uint ID { get; }
        double ArrivalTS { get; }
    }
    public class Context : IHttpClientContext
    {
        public int Maximum, Timeout;
        public Context()
        {
            Maximum = Host.Input.TryGetProperty("max", out var v) ? v.GetInt32() : 20;
            Timeout = Host.Input.TryGetProperty("timeout", out var t) ? t.GetInt32() : 30000;
        }
        public bool IsClosing
        {
            get {
                Host.Event("closing");
                return Host.Bool("closing");
            }
        }
        public int MaxRequests
        {
            get {
                Host.Event("max");
                return Maximum;
            }
        }
        public int TimeoutKeepAlive
        {
            get {
                Host.Event("get_timeout");
                return Timeout;
            }
            set {
                Host.Event("set_timeout", value.ToString());
                Timeout = Math.Min(value, 120000);
            }
        }
        public void StartSendResponse(HttpResponse r) => Host.Event("start");
        public bool SendAsyncStart(byte[] data, int start, int length)
        {
            var text = data != null && start >= 0 && length >= 0 && start <= data.Length &&
                               length <= data.Length - start
                           ? Convert.ToHexString(data.AsSpan(start, length)).ToLowerInvariant()
                           : "BAD";
            Host.Event("write", start + ":" + length + ":" + text);
            return !Host.Input.TryGetProperty("send_ok", out var v) || v.GetBoolean();
        }
        public void EndSendResponse(uint id, ConnectionType c) => Host.Event("end", id + ":" + (int)c);
        public void ContinueSendResponse() => Host.Event("continue");
    }
    public class Request : IHttpRequest
    {
        Context c;
        public Request(Context context)
        {
            c = context;
        }
        public string HttpVersion
        {
            get {
                Host.Event("version");
                return Host.Input.TryGetProperty("version", out var v) ? Host.Name(v) : "HTTP/1.1";
            }
        }
        public IHttpClientContext Context
        {
            get {
                Host.Event("context");
                return Host.Bool("null_context") ? null : c;
            }
        }
        public ConnectionType Connection
        {
            get {
                Host.Event("connection");
                return (ConnectionType)(Host.Input.TryGetProperty("connection", out var v) ? v.GetInt32()
                                                                                           : 1);
            }
        }
        public uint ID
        {
            get {
                Host.Event("id");
                return 123;
            }
        }
        public double ArrivalTS
        {
            get {
                Host.Event("arrival");
                return 1.25;
            }
        }
    }
}
public class Cookie : ResponseCookie
{
    string behavior;
    ResponseCookies collection;
    public Cookie(string how, ResponseCookies values) : base("custom", "value", DateTime.MinValue)
    {
        behavior = how;
        collection = values;
    }
    public override string ToString()
    {
        Host.Event("cookie");
        if (behavior == "throw")
            throw new Exception("cookie");
        if (behavior == "null")
            return null;
        if (behavior == "clear")
            collection.Clear();
        if (behavior == "add")
            collection.Add(new ResponseCookie("added", "v", DateTime.MinValue));
        if (behavior == "replace")
            collection["custom"] = null;
        return "custom";
    }
}
public class FakeBody : Stream
{
    long size;
    bool open;
    public FakeBody(long length, bool readable)
    {
        size = length;
        open = readable;
    }
    public override bool CanRead
    {
        get {
            Host.Event("body_read");
            return open;
        }
    }
    public override bool CanSeek => false;
    public override bool CanWrite => false;
    public override long Length
    {
        get {
            Host.Event("body_len");
            return size;
        }
    }
    public override long Position
    {
        get => throw new NotSupportedException();
        set => throw new NotSupportedException();
    }
    protected override void Dispose(bool d)
    {
        Host.Event("body_dispose");
        open = false;
    }
    public override void Flush()
    {
    }
    public override int Read(byte[] b, int o, int n) => throw new NotSupportedException();
    public override long Seek(long o, SeekOrigin s) => throw new NotSupportedException();
    public override void SetLength(long n) => throw new NotSupportedException();
    public override void Write(byte[] b, int o, int n) => throw new NotSupportedException();
}
class Program
{
    static FieldInfo bodyField =
        typeof(HttpResponse).GetField("m_body", BindingFlags.Instance | BindingFlags.NonPublic);
    static string Snapshot(HttpResponse r, Context c)
    {
        if (r == null)
            return "NULL";
        var encoding = r.Encoding;
        var data = bodyField.GetValue(r) as MemoryStream;
        string body = data == null
                          ? (bodyField.GetValue(r) == null ? "N" : "F")
                          : (data.CanRead ? data.Length.ToString() : "X") + ":" + data.GetBuffer().Length +
                                ":" + (data.CanRead ? data.Position.ToString() : "X");
        var header = r.m_headerBytes;
        string bytes =
            header == null
                ? "N"
                : Convert.ToHexString(header.GetArray().AsSpan(0, header.Length)).ToLowerInvariant();
        return (int)r.Status + "," + (int)r.Connection + "," + r.Priority + "," + r.KeepAlive + "," +
               (r.HeadersSent ? 1 : 0) + "," + (r.Sent ? 1 : 0) + "," + r.RawBufferStart + "," +
               r.RawBufferLen + "," + r.ContentLength + "," + r.requestID + "," +
               unchecked((ulong)BitConverter.DoubleToInt64Bits(r.RequestTS)) + "," + (r.Chunked ? 1 : 0) +
               "," + (encoding == null ? "N" : Host.Hex(encoding.WebName)) + "," +
               (Object.ReferenceEquals(encoding, Encoding.UTF8) ? 1 : 0) + "," + Host.Hex(r.ProtocolVersion) +
               "," + Host.Hex(r.Reason) + "," + Host.Hex(r.ContentType) + "," + c.Timeout + "," +
               r.Cookies.Count + "/" + Host.Bytes(r.RawBuffer) + "/" + bytes + "/" + body;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            Host.Input = JsonDocument.Parse(Convert.FromHexString(line)).RootElement.Clone();
            Host.Trace = "";
            Host.Failed = 0;
            Host.Mutated = false;
            Host.Response = null;
            var input = Host.Input;
            var context = new Context();
            HttpResponse r = null;
            string result = "";
            try
            {
                if (Host.Bool("internal"))
                    r = new HttpResponse(
                        Host.Bool("null_context") ? null : context,
                        input.TryGetProperty("version", out var v) ? Host.Name(v) : "HTTP/1.1",
                        (ConnectionType)(input.TryGetProperty("connection", out var con) ? con.GetInt32()
                                                                                         : 1));
                else
                    r = new HttpResponse(Host.Bool("null_request") ? null : new Request(context));
            }
            catch
            {
                result = "CTOR;";
            }
            Host.Response = r;
            foreach (var op in input.GetProperty("ops").EnumerateArray())
            {
                string action = op.GetProperty("op").GetString(), returned = "OK";
                var value = op.TryGetProperty("v", out var v) ? v : default;
                try
                {
                    if (r == null)
                        throw new Exception("null response");
                    switch (action)
                    {
                    case "status":
                        r.Status = (HttpStatusCode)value.GetInt32();
                        break;
                    case "priority":
                        r.Priority = value.GetInt32();
                        break;
                    case "keep":
                        r.KeepAlive = value.GetInt32();
                        break;
                    case "connection":
                        r.Connection = (ConnectionType)value.GetInt32();
                        break;
                    case "chunked":
                        r.Chunked = value.GetBoolean();
                        break;
                    case "protocol":
                        r.ProtocolVersion = Host.Name(value);
                        break;
                    case "reason":
                        r.Reason = Host.Name(value);
                        break;
                    case "type":
                        r.ContentType = Host.Name(value);
                        break;
                    case "declared":
                        r.ContentLength = long.Parse(value.GetString());
                        break;
                    case "encoding":
                        r.Encoding = value.GetString() switch { "null" => null, "utf8" => Encoding.UTF8,
                                                                "ascii" => Encoding.ASCII,
                                                                "unicode" => Encoding.Unicode,
                                                                _ => new UTF8Encoding(false) };
                        break;
                    case "raw":
                        var raw = Host.Name(value);
                        r.RawBuffer = raw == null ? null : Encoding.UTF8.GetBytes(raw);
                        break;
                    case "start":
                        r.RawBufferStart = value.GetInt32();
                        break;
                    case "length":
                        r.RawBufferLen = value.GetInt32();
                        break;
                    case "header":
                        r.AddHeader(Host.Name(op.GetProperty("key")), Host.Name(value));
                        break;
                    case "redirect":
                        r.Redirect(Host.Name(value),
                                   (HttpStatusCode)(op.TryGetProperty("code", out var code) ? code.GetInt32()
                                                                                            : 302));
                        break;
                    case "headers":
                        r.GetHeaders();
                        break;
                    case "send":
                        r.Send();
                        break;
                    case "next":
                        returned = r.SendNextAsync(value.GetInt32()) ? "1" : "0";
                        break;
                    case "continue":
                        r.CheckSendNextAsyncContinue();
                        break;
                    case "clear":
                        r.Clear();
                        break;
                    case "body_fake":
                        bodyField.SetValue(r, new FakeBody(long.Parse(value.GetString()),
                                                           !op.TryGetProperty("readable", out var readable) ||
                                                               readable.GetBoolean()));
                        break;
                    case "body_copy":
                        var copied = ((MemoryStream)r.Body).GetBuffer();
                        r.Body.Write(copied, op.GetProperty("start").GetInt32(),
                                     op.GetProperty("count").GetInt32());
                        break;
                    case "body_touch":
                        _ = r.Body;
                        break;
                    case "body_write":
                        var b = Encoding.UTF8.GetBytes(Host.Name(value));
                        r.Body.Write(b, 0, b.Length);
                        break;
                    case "body_read":
                        var read = new byte[value.GetInt32()];
                        var count = r.Body.Read(read, 0, read.Length);
                        returned =
                            count + ":" + Convert.ToHexString(read.AsSpan(0, count)).ToLowerInvariant();
                        break;
                    case "body_byte":
                        returned = r.Body.ReadByte().ToString();
                        break;
                    case "body_seek":
                        returned = r.Body
                                       .Seek(long.Parse(value.GetString()),
                                             (SeekOrigin)op.GetProperty("origin").GetInt32())
                                       .ToString();
                        break;
                    case "body_array":
                        returned = Convert.ToHexString(((MemoryStream)r.Body).ToArray()).ToLowerInvariant();
                        break;
                    case "body_write_to":
                        var destination = new MemoryStream();
                        ((MemoryStream)r.Body).WriteTo(destination);
                        returned = Convert.ToHexString(destination.ToArray()).ToLowerInvariant();
                        break;
                    case "body_position":
                        r.Body.Position = long.Parse(value.GetString());
                        break;
                    case "body_length":
                        r.Body.SetLength(long.Parse(value.GetString()));
                        break;
                    case "body_dispose":
                        r.Body.Dispose();
                        break;
                    case "cookie":
                        if (op.TryGetProperty("custom", out var custom))
                            r.Cookies.Add(new Cookie(custom.GetString(), r.Cookies));
                        else
                            r.Cookies.Add(new ResponseCookie(
                                Host.Name(op.GetProperty("key")), Host.Name(value),
                                op.TryGetProperty("expires", out var exp)
                                    ? new DateTime(long.Parse(exp.GetString()))
                                    : DateTime.MinValue,
                                op.TryGetProperty("path", out var path) ? Host.Name(path) : "/",
                                op.TryGetProperty("domain", out var domain) ? Host.Name(domain) : null));
                        break;
                    case "cookie_get":
                    case "cookie_render":
                    case "cookie_value":
                    case "cookie_path":
                    case "cookie_expiry":
                        var selected = r.Cookies[Host.Name(op.GetProperty("key"))];
                        if (action == "cookie_get")
                            returned = Host.Hex(selected.Name) + ":" + Host.Hex(selected.Value) + ":" +
                                       selected.Expires.Ticks + ":" + (int)selected.Expires.Kind + ":" +
                                       Host.Hex(selected.Path);
                        if (action == "cookie_render")
                            returned = Host.Hex(selected.ToString());
                        if (action == "cookie_value")
                            selected.Value = Host.Name(value);
                        if (action == "cookie_path")
                            selected.Path = Host.Name(value);
                        if (action == "cookie_expiry")
                            selected.Expires = new DateTime(
                                long.Parse(value.GetString()),
                                (DateTimeKind)(op.TryGetProperty("kind", out var kind) ? kind.GetInt32()
                                                                                       : 0));
                        break;
                    case "cookie_set":
                        ResponseCookie added = null;
                        if (!op.TryGetProperty("null", out var isnull) || !isnull.GetBoolean())
                            added = new ResponseCookie(Host.Name(op.GetProperty("name")), Host.Name(value),
                                                       DateTime.MinValue);
                        r.Cookies[Host.Name(op.GetProperty("key"))] = added;
                        break;
                    case "request_cookie":
                    case "cookie_copy":
                        var requestCookie =
                            new RequestCookie(Host.Name(op.GetProperty("key")), Host.Name(value));
                        if (op.TryGetProperty("after", out var after))
                            requestCookie.Value = Host.Name(after);
                        if (action == "request_cookie")
                            returned = Host.Hex(requestCookie.ToString());
                        else
                            r.Cookies.Add(requestCookie,
                                          new DateTime(op.TryGetProperty("expires", out var expires)
                                                           ? long.Parse(expires.GetString())
                                                           : 0,
                                                       (DateTimeKind)(op.TryGetProperty("kind", out var ck)
                                                                          ? ck.GetInt32()
                                                                          : 0)));
                        break;
                    case "cookie_clear":
                        r.Cookies.Clear();
                        break;
                    case "cookie_null":
                        r.Cookies[Host.Name(value)] = null;
                        break;
                    }
                }
                catch
                {
                    returned = "EX";
                }
                result += returned + ":" + Snapshot(r, context) + ";";
            }
            Console.WriteLine(result + "|" + Host.Hex(Host.Trace) + "|" + Snapshot(r, context));
        }
    }
}
