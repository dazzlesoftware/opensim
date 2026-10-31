"""Execute the original Util.TryParseHttpRange body against the native port."""
import argparse, hashlib, itertools, json, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 source=ROOT/'OpenSim/Framework/Util.cs';text=source.read_text(encoding='utf-8-sig');start=text.index('public static bool TryParseHttpRange(');opening=text.index('{',start);depth=1;end=opening+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 body=text[start:end]
 (w/'Reference.cs').write_text('using System; using System.Text; using System.Globalization; class Reference { '+body+''' static void Main(){CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;string line;while((line=Console.ReadLine())!=null){try{string value=line=="N"?null:Encoding.UTF8.GetString(Convert.FromHexString(line));int start,end;bool ok=TryParseHttpRange(value,out start,out end);Console.WriteLine((ok?"1":"0")+"|"+start+"|"+end);}catch{Console.WriteLine("EX");}}}}''',encoding='utf-8')
 (w/'Reference.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net8.0</TargetFramework></PropertyGroup></Project>',encoding='utf-8')
 r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'--nologo'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 numbers=['','0','1','2','10','-1','+1','+0','2147483647','2147483648','-2147483648','0003',' 3 ','\t3\r','3\0','3\0\0','3\0 ','٣','３','3.0','3,000','x','\u00a03\u00a0','\u20033\u2003']
 prefixes=['bytes=','Bytes=',' bytes=','bytes =','bytes=\u200b','bytes\u00ad=','ｂｙｔｅｓ=','bytes=\0']
 cases=[None,'',' ','\t\r\n','bytes=1-2-3','bytes=1-2,4-5']+[prefix+left+'-'+right for prefix,left,right in itertools.product(prefixes,numbers,numbers)]
 commands='\n'.join('N' if c is None else c.encode().hex() for c in cases)+'\n';rows=[]
 for cmd in [[a.dotnet,str(w/'app/Reference.dll')],[a.native]]:
  r=subprocess.run(cmd,input=commands,capture_output=True,text=True,timeout=120);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 differences=[{'header':c,'source':s,'native':n} for c,s,n in zip(cases,*rows) if s!=n]
 (w/'http-range-results.json').write_text(json.dumps({'passed':not differences,'observations':len(cases),'sourceSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'scope':'Original method body; invariant culture; UTF8-representable headers. Other cultures/platforms and invalid UTF16 remain pending.','differences':differences[:10]},indent=2),encoding='utf-8')
 assert not differences,differences[:3]
 print(f'PASS: {len(cases)} HTTP range source observations')
if __name__=='__main__':main()
