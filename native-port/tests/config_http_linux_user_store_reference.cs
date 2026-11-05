using System;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using System.Text.Json;
using System.Collections.Generic;
using System.Diagnostics;
class Program {
 [DllImport("libcrypto.so.3")]static extern int OPENSSL_sk_num(IntPtr stack);
 [DllImport("libcrypto.so.3")]static extern IntPtr OPENSSL_sk_value(IntPtr stack,int index);
 static void Main(){
  var home=Environment.GetEnvironmentVariable("HOME");
  var root=Path.Combine(home,".dotnet/corefx/cryptography/x509stores");
  using var key=RSA.Create(2048);
  var request=new CertificateRequest("CN=User store fixture",key,HashAlgorithmName.SHA256,RSASignaturePadding.Pkcs1);
  using var cert=request.CreateSelfSigned(DateTimeOffset.UtcNow.AddDays(-1),DateTimeOffset.UtcNow.AddDays(1));
  using var publicCert=new X509Certificate2(cert.RawData);
  var variants=new Dictionary<string,Dictionary<string,byte[]>> {
   ["empty"]=new(),
   ["keyed"]=new(){["cert.pfx"]=cert.Export(X509ContentType.Pfx)},
   ["public"]=new(){["cert.pfx"]=publicCert.Export(X509ContentType.Pfx)},
   ["case"]=new(){["cert.PfX"]=cert.Export(X509ContentType.Pfx,"")},
   ["duplicates"]=new(){["one.pfx"]=cert.Export(X509ContentType.Pfx),["two.PFX"]=publicCert.Export(X509ContentType.Pfx)},
   ["password"]=new(){["cert.pfx"]=cert.Export(X509ContentType.Pfx,"secret")},
   ["invalid"]=new(){["bad.pfx"]=new byte[]{1,2,3},["valid.pem"]=cert.Export(X509ContentType.Pfx),[".pfx"]=cert.Export(X509ContentType.Pfx)},
   ["mixed"]=new(){["bad.pfx"]=new byte[]{1,2,3},["one.pfx"]=cert.Export(X509ContentType.Pfx),["two.pfx"]=publicCert.Export(X509ContentType.Pfx),["encrypted.pfx"]=cert.Export(X509ContentType.Pfx,"secret")}
  };
  var provider=typeof(X509Certificate2).Assembly.GetType("System.Security.Cryptography.X509Certificates.OpenSslCachedDirectoryStoreProvider",true);
  var rows=new List<object>();
  foreach(var variant in variants){
   var directory=Path.Combine(root,variant.Key);Directory.CreateDirectory(directory);
   foreach(var existing in Directory.EnumerateFiles(directory))File.Delete(existing);
   foreach(var file in variant.Value)File.WriteAllBytes(Path.Combine(directory,file.Key),file.Value);
   var instance=Activator.CreateInstance(provider,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{variant.Key},null);
   using var stack=(SafeHandle)provider.GetMethod("GetNativeCollection",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(instance,null);
   var values=new List<string>();
   for(int i=0;i<OPENSSL_sk_num(stack.DangerousGetHandle());i++){
    using var loaded=new X509Certificate2(OPENSSL_sk_value(stack.DangerousGetHandle(),i));values.Add(Convert.ToHexString(loaded.RawData).ToLowerInvariant());
   }
   rows.Add(new {label=variant.Key,directory,expected=values});
  }
  var cacheDirectory=Path.Combine(root,"cache");Directory.CreateDirectory(cacheDirectory);
  foreach(var file in Directory.EnumerateFiles(cacheDirectory))File.Delete(file);
  var cache=Activator.CreateInstance(provider,BindingFlags.Instance|BindingFlags.NonPublic,null,new object[]{"cache"},null);
  var watch=(Stopwatch)provider.GetField("_recheckStopwatch",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(cache);
  var get=provider.GetMethod("GetNativeCollection",BindingFlags.Instance|BindingFlags.NonPublic);
  var handles=new List<SafeHandle>();int checks=0;
  SafeHandle Read(long milliseconds){
   watch.Stop();typeof(Stopwatch).GetField("_elapsed",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(watch,milliseconds*Stopwatch.Frequency/1000);
   var value=(SafeHandle)get.Invoke(cache,null);if(!handles.Contains(value))handles.Add(value);return value;
  }
  int Count(SafeHandle value)=>OPENSSL_sk_num(value.DangerousGetHandle());
  void Check(bool ok,string label){if(!ok)throw new Exception("Cache source assertion: "+label);checks++;}
  var initial=Read(0);Check(Count(initial)==0,"initial empty");
  File.WriteAllBytes(Path.Combine(cacheDirectory,"one.pfx"),cert.Export(X509ContentType.Pfx));
  Directory.SetLastWriteTimeUtc(cacheDirectory,new DateTime(2020,1,1,0,0,0,DateTimeKind.Utc));
  Check(ReferenceEquals(Read(999),initial),"999ms no stat");
  var first=Read(1000);Check(Count(first)==1 && !ReferenceEquals(first,initial),"1000ms reload changed");
  File.WriteAllBytes(Path.Combine(cacheDirectory,"two.pfx"),publicCert.Export(X509ContentType.Pfx));
  Directory.SetLastWriteTimeUtc(cacheDirectory,new DateTime(2020,1,2,0,0,0,DateTimeKind.Utc));
  var second=Read(1000);Check(Count(second)==2,"duplicate retained");
  File.Delete(Path.Combine(cacheDirectory,"two.pfx"));Directory.SetLastWriteTimeUtc(cacheDirectory,new DateTime(2020,1,2,0,0,0,DateTimeKind.Utc));
  Check(ReferenceEquals(Read(29999),second),"29999ms unchanged mtime");
  var expired=Read(30000);Check(Count(expired)==1 && !ReferenceEquals(expired,second),"30000ms unconditional reload");
  var moved=cacheDirectory+"-moved";Directory.Move(cacheDirectory,moved);
  Check(ReferenceEquals(Read(1000),expired),"deleted directory retains snapshot");
  provider.GetMethod("DoRefresh",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(cache,null);
  var absent=Read(0);Check(Count(absent)==0,"forced missing-directory refresh");
  Directory.Move(moved,cacheDirectory);
  provider.GetMethod("DoRefresh",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(cache,null);
  Check(Count(Read(0))==1,"forced restoration without elapsed time");
  Check(Count(initial)==0 && Count(first)==1 && Count(second)==2,"retained snapshots immutable");
  foreach(var handle in handles)handle.Dispose();
  Console.WriteLine(JsonSerializer.Serialize(new {rows,cacheAssertions=checks}));
 }
}
