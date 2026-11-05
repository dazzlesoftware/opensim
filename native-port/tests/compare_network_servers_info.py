"""Unchanged NetworkServersInfo/ConfigSettings with actual Nini typed getters."""
import argparse,hashlib,itertools,json,random,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def enc(s):
 if s is None:return 'null'
 b=s.encode('utf-16-le','surrogatepass');return ''.join(f'{b[i]+256*b[i+1]:04x}' for i in range(0,len(b),2)) or '-'
def call(cmd,rows=None):
 p=subprocess.run(cmd,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=180);assert p.returncode==0,(p.returncode,p.stdout[-2500:],p.stderr[-2500:]);return p.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',Nullable='disable').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in [ROOT/'native-port/tests/network_servers_info_reference.cs',ROOT/'OpenSim/Framework/NetworkServersInfo.cs',ROOT/'OpenSim/Framework/ConfigSettings.cs']:ET.SubElement(group,'Compile',Include=str(f))
 r=ET.SubElement(group,'Reference',Include='Nini');ET.SubElement(r,'HintPath').text=str(ROOT/'bin/Nini.dll');cs=w/'NetworkServersReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);call([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet'])
 rows=[]
 def add(doc,mode=0,seed=0,fault=0,mutation=0,at=0):rows.append(f'{mode} {seed} {fault} {mutation} {at} {enc(doc)}')
 ints=['','0','1','-1','+1','-0','000','2147483647','-2147483648','2147483648','-2147483649','4294967295','65535','65536','0x10','1,000','1_000','1.0','1e1',' 42 ','\t-42\t','42\x00','42\x00\x00','42\x00 ','\u008542','\u00a042','\ud800','\ufffe','not-an-int']
 bools=['\" true \"','\" false \"','\" True \"','\"Yes\"','\" \"','','True','False','true','false','TRUE','FALSE','On','Off','Yes','No','1','0',' true ','tru\u00ade','fal\x00se','\ud800','\ufffe']
 texts=['','plain','"quoted"','localhost','\ud800','\udc00','\ufffe','\uffff','\u00e9','\U0001f600','${other}','a=b','"a;b"','null']
 for mode,seed,doc in itertools.product([0,1,2,3,5],[-1,0,1,2,3,7],['','[Other]\nx=1\n','[Network]\n','[network]\nx=1\n','[Network]\nhttps_listener=true\n']):add(doc,mode,seed)
 for key,values in [('http_listener_port',ints),('http_listener_sslport',ints),('https_port',ints),('http_listener_ssl',bools),('https_listener',bools),('https_external',bools),('http_listener_cn',texts),('http_listener_cert_path',texts),('http_listener_cert_pass',texts),('cert_path',texts),('cert_pass',texts)]:
  for value,mode,seed in itertools.product(values,[0,1,2],[0,1,2,3,7]):add(f'[Network]\nhttps_listener=true\n{key}={value}\n',mode,seed)
 for number,key in itertools.product(range(-4096,4097),['http_listener_port','http_listener_sslport','https_port']):add(f'[Network]\nhttps_listener=true\n{key}={number}\n',1,1)
 docs=['[Network]\n','[Network]\nhttps_listener=true\n','[Network]\nhttps_listener=false\nhttp_listener_cert_path=explicit\n','[Network]\nhttp_listener_port=-1\nhttp_listener_sslport=-2147483648\nhttp_listener_ssl=true\nhttp_listener_cn=cn\nhttp_listener_cert_path=path\nhttp_listener_cert_pass=pass\nhttps_listener=true\nhttps_external=true\ncert_path=cert\ncert_pass=secret\nhttps_port=-2\n']
 for doc,seed,fault,mutation,at in itertools.product(docs,[0,1,2,3],range(-1,13),range(9),[1,5,6,7,8,9,10,11]):add(doc,0,seed,fault,mutation,at)
 for cp in range(65536):add('[Network]\nhttp_listener_cn='+chr(cp)+'\n',1,3)
 rng=random.Random(51324)
 keys=['http_listener_port','http_listener_sslport','http_listener_ssl','http_listener_cn','http_listener_cert_path','http_listener_cert_pass','https_listener','https_external','cert_path','cert_pass','https_port']
 for _ in range(5000):
  lines=['[Network]']
  for key in rng.sample(keys,rng.randrange(12)):
   values=ints if key.endswith('port') else bools if key in ['http_listener_ssl','https_listener','https_external'] else texts
   lines.append(key+'='+rng.choice(values))
  add('\n'.join(lines)+'\n',rng.randrange(3),rng.randrange(8),rng.randrange(13),rng.randrange(9),rng.randrange(12))
 source=call([a.dotnet,str(w/'app/NetworkServersReference.dll')],rows);native=call([a.native,'--probe'],rows);assert len(source)==len(native)==len(rows)
 diff=[dict(index=i,command=c,source=s,native=n) for i,(c,s,n) in enumerate(zip(rows,source,native)) if s!=n];report=dict(passed=not diff,cases=len(rows),sourceSha256=hashlib.sha256((ROOT/'OpenSim/Framework/NetworkServersInfo.cs').read_bytes()).hexdigest(),settingsSha256=hashlib.sha256((ROOT/'OpenSim/Framework/ConfigSettings.cs').read_bytes()).hexdigest(),scope='Unchanged original full public field/constructor/load class with actual Nini.dll; native concrete invariant Nini backend, foreign getter callback traces/faults/reentrancy, retained source reference and actual loader callback pair. Raw UTF16/default fallbacks/partial state/uint wrap preserved.',differences=diff[:10]);(w/'network-servers-info-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diff,json.dumps(diff[:3]);print('PASS:',len(rows),'actual original network configuration cases')
if __name__=='__main__':main()
