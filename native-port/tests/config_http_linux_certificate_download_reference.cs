using System;using System.IO;using System.Reflection;using System.Runtime.InteropServices;using System.Security.Cryptography;using System.Security.Cryptography.X509Certificates;
class Program {
 [DllImport("libcrypto.so.3",EntryPoint="i2d_X509_AUX")]static extern int Size(IntPtr cert,IntPtr output);
 [DllImport("libcrypto.so.3",EntryPoint="i2d_X509_AUX")]static extern int Encode(IntPtr cert,ref IntPtr output);
 static void Main(string[] args){
  if(args.Length>0&&args[0]=="--prepare"){using var key=RSA.Create(2048);var request=new CertificateRequest("CN=Download Fixture",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);using var cert=request.CreateSelfSigned(new DateTimeOffset(2020,1,1,0,0,0,TimeSpan.Zero),new DateTimeOffset(2040,1,1,0,0,0,TimeSpan.Zero));File.WriteAllBytes(args[1],cert.RawData);File.WriteAllBytes(args[2],cert.Export(X509ContentType.Pkcs12));return;}
  var type=typeof(X509Certificate2).Assembly.GetType("System.Security.Cryptography.X509Certificates.OpenSslCertificateAssetDownloader",true);var method=type.GetMethod("DownloadCertificate",BindingFlags.Static|BindingFlags.NonPublic);string line;
  while((line=Console.ReadLine())!=null){var fields=line.Split(' ');string uri="";for(int i=0;i<fields[1].Length;i+=4)uri+=(char)Convert.ToUInt16(fields[1].Substring(i,4),16);using var cert=(X509Certificate2)method.Invoke(null,new object[]{uri,TimeSpan.FromTicks(long.Parse(fields[0]))});if(cert==null){Console.WriteLine("_");continue;}int count=Size(cert.Handle,IntPtr.Zero);if(count<=0)throw new Exception("AUX size");var memory=Marshal.AllocHGlobal(count);try{var at=memory;if(Encode(cert.Handle,ref at)!=count)throw new Exception("AUX encode");byte[] aux=new byte[count];Marshal.Copy(memory,aux,0,count);Console.WriteLine(Convert.ToHexString(cert.RawData).ToLowerInvariant()+" "+Convert.ToHexString(aux).ToLowerInvariant());GC.KeepAlive(cert);}finally{Marshal.FreeHGlobal(memory);}}
 }
}
