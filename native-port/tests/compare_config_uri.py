"""Actual System.Uri ConfigurationLoader.IsUri raw UTF16 predicate oracle."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):
 if s is None:return 'null'
 b=s.encode('utf-16-le','surrogatepass');return ''.join(f'{b[i]+256*b[i+1]:04x}' for i in range(0,len(b),2)) or '-'
def call(cmd,rows=None):
 p=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert p.returncode==0,(p.returncode,p.stdout[-1500:],p.stderr[-1500:]);return p.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);p.add_argument('--quick',action='store_true');a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',Nullable='disable').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/config_uri_reference.cs'));cs=w/'ConfigUriReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);call([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 texts=[None,'','http:','https:','http://','/http://x','http:///x','http://x','http://user:pass@x/?q#fragment','http://x\\path','http:\\\\x','http:/\\x','http:\\/x','ftp://x','file:///x','http://_a','http://-a','http://a..b','http://1','http://0x7f000001','http://99999999999999','http://a:','http://a:65536','http://a:0','http://a:00000080','http://a\x00/p','http://a/\x00','http://a/\ud800','http://\ud800/','http://\ufffe','http://\uffff','http://\u200e','http://_a.','http://a._b','http://a.-b','http://@a','http://a@','http://a@@b',' http://a ','\t\nHtTpS://a\r\n']
 for scheme,slashes,host,port,tail in itertools.product(['http','https','HTTP','Https','xhttp','ftp'],['//','\\\\','/\\','\\/','/','///'],['host','-host','_host','a..b','localhost.','a_b','a-b','1.2.3.4','user:pass@host','user@@host'],['',':80',':',':65536'],['','/path?query#fragment']):texts.append(f'{scheme}:{slashes}{host}{port}{tail}')
 addresses=['::','::1','2001:db8::1','1:2:3:4:5:6:7:8','1:2:3:4:5:6:7','1:2:3:4:5:6:7:8:9','::ffff:192.168.1.1','::ffff:192.168.01.1','::ffff:0x7f000001','fe80::1%scope','fe80::1%25eth0','fe80::1%','fe80::1%raw stuff','::/64','::/99','::/100','::/0','::/00','::1%scope/64',':::','1::2::3']
 for address,tail in itertools.product(addresses,['',':80','/p','?q','#f','\\p',':65536',':0',':80\\p']):texts.append(f'http://[{address}]{tail}')
 rng=random.Random(9542)
 for _ in range(2500):
  n=rng.randrange(12);address=':'.join(''.join(rng.choice('0123456789abcdefXYZ') for _ in range(rng.randrange(6))) for _ in range(n));texts.append('http://['+address+']'+rng.choice(['','%scope',':80','/p']))
 units=range(65536) if not a.quick else list(range(256))+[0x300,0x344,0x200e,0x202a,0x3002,0xd800,0xdc00,0xfffe,0xffff]
 for cp in units:
  c=chr(cp)
  for left,right in [('http://',''),('http://a','b/'),('http://_','/'),('http://a/',''),('http://a/?',''),('http://a/#',''),('http://','@a/'),('', 'http://a'),('http://a:', ''),('ht','tp://a'),('http://[fe80::1%',']'),('http://[::1]','')]:texts.append(left+c+right)
 for n in [1,2,15,28,29,30,31,58,59,60,63,64,127,255,256,257,7000,7200,8000,16000,32000,60000,65500,65508,65509,65510,65511,65512,65513,65514,65515,65516,65517,65518,65519,65520]:
  for c in ['a','é','\u0344','\u200e','\ue000','\ud800','%41','%80','%FF','%C3%A9','%EF%BF%BD']:
   if len(c)*n<70000:
    for left,right in [('http://',''),('http://a/',''),('http://a/?',''),('http://a/#',''),('http://','@a/')]:texts.append(left+c*n+right)
 for _ in range(20000):
  host=rng.choice(['a','_a','0','é','x'*62,'[::1]'])
  for _ in range(rng.randrange(5)):
   at=rng.randrange(len(host)+1);host=host[:at]+rng.choice(['a','-','_','.','。','é','\u0344','\u200e','\ud800','%41','@',':','\\','/'])+host[at:]
  texts.append(rng.choice(['http://','https://',' Http://','\tHTTPS://'])+host+rng.choice(['','/','?','#',':80',':65536'])+rng.choice(['é','\ud800','%41%FF%80','\x00','\ue000','\ud800\udc00','']))
 for n,suffix,repeats in itertools.product([7270,7275,7278,7279,7280,7281,7282],['%41','%FF%41','%C3%A9','%EF%BF%BD','%C0%AF%41','%E0%80%80%41','%F0%90%80%80','%C3%41%A9','%20','%7F','%00','%25','%G1'],[1,2,3,10]):
  texts.append('http://a/'+'\ud800'*n+suffix*repeats)
 for cp in [0x10000,0x1f600,0x1fffe,0xe0000,0xe0fff,0xe1000,0xeffff,0xf0000,0x10fffd,0x10fffe,0x10ffff]:
  c=chr(cp)
  for left,right in [('http://','/'),('http://a/',''),('http://a/?',''),('http://','@a/')]:texts.append(left+c+right)
 for c,n in itertools.product(['a','é','\u0344','\u200e','\ue000'],[28,29,30,31,58,59,60,63,64,100,255]):
  for repeats in [1,2,10,100,1000,1100,1129,1130,1149,1150,1200]:
   host=(c*n+'.')*repeats
   if len(host)<65500:texts.append('http://'+host+'/')
 rows=[enc(s) for s in texts];source=call([a.dotnet,str(w/'app/ConfigUriReference.dll')],rows);native=call([a.native,'--probe'],rows);assert len(source)==len(native)==len(rows)
 diff=[dict(index=i,text=text,source=s,native=n) for i,(text,s,n) in enumerate(zip(texts,source,native)) if s!=n];report=dict(passed=not diff,cases=len(rows),scope='Actual .NET8 Uri.TryCreate(raw UTF16,Absolute) HTTP(S) predicate; native selected-ICU NFC. No HTTP client policy and no DNS/IDN resolution. Input normalization modes other than default selected ICU require explicit provider.',differences=diff[:20]);(w/'config-uri-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diff,json.dumps(diff[:8]);print('PASS:',len(rows),'actual System.Uri configuration classifications')
if __name__=='__main__':main()
