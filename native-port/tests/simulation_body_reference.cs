using System;
using System.IO;
using System.Text;
using System.IO.Compression;
using System.Collections.Specialized;
using OpenMetaverse.StructuredData;
public class IOSHttpRequest
{
    public Stream InputStream;
    public string ContentType;
    public NameValueCollection Headers = new();
}
public static partial class Reference
{
    public static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int mode = int.Parse(p[0]);
            var request =
                new IOSHttpRequest { InputStream = new MemoryStream(
                                         p[1] == "." ? Array.Empty<byte>() : Convert.FromHexString(p[1])) };
            switch (mode)
            {
            case 1:
                request.ContentType = "application/x-gzip";
                break;
            case 2:
                request.Headers["Content-Encoding"] = "gzip";
                break;
            case 3:
                request.Headers["X-Content-Encoding"] = "gzip";
                break;
            case 4:
                request.Headers["X-Content-Encoding"] = "GZIP";
                break;
            case 5:
                request.ContentType = "application/x-gzip; charset=utf-8";
                break;
            case 6:
                request.Headers["content-encoding"] = "gzip";
                break;
            case 7:
                request.Headers["Content-Encoding"] = "gzip,gzip";
                break;
            }
            var value = DeserializeJSONOSMap(request);
            Console.WriteLine(
                value == null
                    ? "-"
                    : Convert.ToHexString(Encoding.UTF8.GetBytes(OSDParser.SerializeJsonString(value, true)))
                          .ToLowerInvariant());
        }
    }
}
