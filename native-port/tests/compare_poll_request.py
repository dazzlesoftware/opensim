"""Compile whole unchanged polling request/args classes with controlled HTTP and log facades."""
import argparse,hashlib,itertools,json,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 sources=[ROOT/'OpenSim/Framework/Servers/HttpServer'/n for n in ['PollServiceHttpRequest.cs','PollServiceEventArgs.cs']]
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in {'TargetFramework':'net8.0','OutputType':'Exe','EnableDefaultCompileItems':'false'}.items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup')
 for f in sources+[ROOT/'native-port/tests/poll_request_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
 ref=ET.SubElement(group,'Reference',Include='OpenMetaverseTypes');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll')
 cs=w/'Poll.csproj';ET.ElementTree(project).write(cs,encoding='utf-8');r=subprocess.run([a.dotnet,'build',str(cs),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
 cases=[]
 def add(mode='text',ctype='text/plain',text='hello',start=0,length=-1,code=200,fail='',wrong='',counter=0):cases.append('|'.join(map(str,[mode,'N' if ctype is None else ctype.encode().hex(),text.encode().hex(),start,length,0,code,fail,wrong,counter])))
 for ctype,text in itertools.product([None,'','text/plain','image/png','Image/png','application/vnd.ll.mesh','x-shockwave-flash','application/x-oar'],['','hello','aGVsbG8=','é世界','YW Jj\r\n']):add(ctype=ctype,text=text)
 for start,length in itertools.product([-4,0,1,6,7,2147483647],[-4,0,1,5,6,7,2147483647]):add(mode='binary',start=start,length=length)
 for mode,code,fail in itertools.product(['text','binary','prefab','nullprefab','badprefab','null','stop'],[200,301],['','body','read','dispose','factory','status:200','header:Content-Type:746578742f706c61696e','utf8','length:5','start:0','count:6','buffer:000102fdfeff','redirect:2f6e657874','keep:0','send','warn']):add(mode=mode,code=code,fail=fail)
 for mode,counter in itertools.product(['text','binary','prefab','nullprefab','stop'],[2147483647,-2147483648]):add(mode=mode,counter=counter)
 for mode in ['text','binary']:add(mode=mode,wrong='long')
 rows=[]
 for command in [[a.dotnet,str(w/'app/Poll.dll')],[a.native]]:
  r=subprocess.run(command,input='\n'.join(cases)+'\n',capture_output=True,text=True,timeout=90);assert r.returncode==0,r.stderr;rows.append(r.stdout.splitlines())
 assert len(rows[0])==len(rows[1])==len(cases)
 diffs=[{'case':c,'source':s,'native':n} for c,s,n in zip(cases,*rows) if s!=n]
 (w/'poll-request-results.json').write_text(json.dumps({'passed':not diffs,'observations':len(cases),'sources':{s.name:hashlib.sha256(s.read_bytes()).hexdigest() for s in sources},'scope':'Whole original request and event-args classes; actual .NET byte/base64/string behavior; controlled HTTP response, body, and logging. Async transport, scheduling, OS socket behavior, and concurrent callbacks remain pending.','differences':diffs[:10]},indent=2),encoding='utf-8')
 assert not diffs,json.dumps(diffs[:3]);print(f'PASS: {len(cases)} polling request source observations')
if __name__=='__main__':main()
