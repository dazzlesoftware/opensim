# Copyright (c) Contributors, http://opensimulator.org/
# BSD license: native-port/LICENSE.txt.
"""Unchanged RemoteConsole bodies with controlled clock and HTTP registrations."""
from pathlib import Path
import argparse,subprocess,json,hashlib,random,xml.etree.ElementTree as ET
from compare_inventory_asset_caps import extract
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parents[2];work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
remote=root/'OpenSim/Framework/Console/RemoteConsole.cs';commands=root/'OpenSim/Framework/Console/CommandConsole.cs'
source=commands.read_bytes().decode('utf-8-sig');template=(root/'native-port/tests/remote_console_reference.cs').read_text(encoding='utf-8')
code=template.replace('PARSER',extract(source,'public class Parser')).replace('COMMANDS',extract(source,'public class Commands')).replace('REMOTE',extract(remote.read_bytes().decode('utf-8-sig'),'public class RemoteConsole'))
(work/'Reference.cs').write_text(code,encoding='utf-8',newline='')
project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for key,value in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable',NoWarn='0436').items():ET.SubElement(props,key).text=value
items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include=str(work/'Reference.cs'))
for name in ['Nini','OpenMetaverse','OpenMetaverseTypes']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/str(name+'.dll'))
ET.ElementTree(project).write(work/'Reference.csproj')
build=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app'),'-v:q'],capture_output=True,text=True);(work/'build.txt').write_text(build.stdout+build.stderr,encoding='utf-8');assert build.returncode==0,build.stdout+build.stderr
h=lambda value:value.encode('utf-16-be','surrogatepass').hex()
def action(op,*values):return ','.join([op]+[str(value) for value in values])
def form(op,value):return action(op,h(value))
def output(value,level=None,flags=0):return action('O','-' if value is None else h(value),'-' if level is None else h(level),flags)
start=form('S','USER=user&PASS=pass')
cases=[]
cases.append(';'.join([start,'H,0','G,0',output(' hello <&>é\r\n ','WARN'), 'H,0','G,0','G,0','N',form('I','ID=@0&COMMAND=echo+%22hello+world%22'),action('L',h('Prompt> '),1,0),'G,0',form('C','ID=@0'),'H,0','G,0','Z,0']))
for body in ['','USER=user','PASS=pass','user=user&PASS=pass','USER=user&PASS=bad','USER=x&USER=user&PASS=pass','USER=user&PASS=pass=ignored','USER=%75ser&PASS=pass','USER=user%00&PASS=pass','USER=user&PASS=pa%73s']:
 cases.append(';'.join([form('S',body),'H,0','G,0',form('C','ID=@0')]))
for value in [None,'',' <&>" é中😀\n','\ud800','\udfff','a\0b','\u2003x\u00a0']:
 for flags in [0,1,2,4,7]:cases.append(';'.join([output(value,None,flags),start,'G,0',output('later','INFO',flags),'G,0','G,0']))
for ticks in [499999,500000,500001,2147483647,-2147483648,-2146983648]:cases.append(';'.join(['T,2147483640',start,'T,'+str(ticks),'E','H,0','G,0']))
cases += [';'.join([start,'T,500000','E','T,500001','E','H,0','G,0']),';'.join([start,'T,499999','H,0','T,500001','E','G,0'])]
for count in [999,1000,1001]:cases.append(';'.join([action('W',count),start,'G,0',output('last'), 'G,0']))
for body in ['ID=@0','ID=bad&COMMAND=x','ID=00000000-0000-0000-0000-000000000000&COMMAND=x','ID=@0&COMMAND','ID=@0&COMMAND=unknown','ID=@0&COMMAND=echo+text','ID=@0&COMMAND=echo+one&COMMAND=echo+two']:
 cases.append(';'.join([start,form('I',body),action('L',h('Input: '),0,0),'G,0']))
cases.append(';'.join([action('A',h('user'),h('pass'),h('https://client.example')),start,form('S','USER=user&PASS=bad'),'N','M',start,form('C','ID=00000000-0000-0000-0000-000000000000')]))
# Decoder is raw UTF16, including malformed percent data and lone surrogates.
for value in ['','&','a=b=c','x=1&x=2','%uD800=%uDFFF','%U0041=%c0%af','a=%ED%A0%80','a=%C3%u00A9%A9','USER=a+b&COMMAND=%26%3D','é=中','x=%','x=%u','x=%u0000']:
 cases.append(form('D',value))
for begin in range(0,65536,1024):
 cases.append(';'.join(form('D',chr(c)+'=%u'+format(c,'04x')+'x') for c in range(begin,begin+1024)))
for value in ['%E0%80%80','%F0%80%80%80','%F4%90%80%80','%C2%41','%E2%82','%ED%9F%BF','%F4%8F%BF%BF','%uD800%uD800%uDC00','%uD83D%uDE00']:
 cases.append(form('D','x='+value))
for value in ['echo+%uD800','echo+%uDFFF','echo+%uD83D%uDE00','echo+%ED%A0%80']:
 cases.append(';'.join([start,form('I','ID=@0&COMMAND='+value),action('L',h('Command> '),1,0),'G,0']))
rng=random.Random(6259)
for _ in range(700):
 text=''.join(rng.choice('abUSERPASS0123456789%u=+&ABCDEFé中\ud800') for _ in range(rng.randrange(0,100)));cases.append(form('D',text))
for _ in range(120):
 steps=[start]
 for _ in range(12):
  choice=rng.randrange(7)
  if choice==0:steps.append(start)
  elif choice==1:steps.append(output(rng.choice(['','text',' <&>é\n','\u2003value ']),rng.choice([None,'','INFO']),rng.randrange(8)))
  elif choice==2:steps.append('H,'+str(rng.randrange(-1,4)))
  elif choice==3:steps.append('G,'+str(rng.randrange(-1,4)))
  elif choice==4:steps.append(form('C','ID=@'+str(rng.randrange(3))))
  elif choice==5:steps.append('T,'+str(rng.choice([0,500000,500001,1000001,-2147483648,2147483647])));steps.append('E')
  else:steps.append('N')
 cases.append(';'.join(steps))
payload='\n'.join(cases)+'\n';(work/'input.txt').write_text(payload,encoding='utf-8');outputs={}
for name,command in [('reference',[a.dotnet,str(work/'app/Reference.dll')]),('native',[str(Path(a.native).resolve())])]:
 run=subprocess.run(command,input=payload,capture_output=True,text=True,encoding='utf-8',timeout=240);assert run.returncode==0,(name,run.stderr)
 (work/(name+'.txt')).write_text(run.stdout,encoding='utf-8');outputs[name]=run.stdout.splitlines();assert len(outputs[name])==len(cases),(name,len(outputs[name]),len(cases))
differences=[dict(index=i,input=cases[i],reference=x,native=y) for i,(x,y) in enumerate(zip(outputs['reference'],outputs['native'])) if x!=y]
report=dict(passed=not differences,scenarios=len(cases),operations=sum(case.count(';')+1 for case in cases),differenceCount=len(differences),differences=differences[:4],applicationParityVerified=False,scope='Original RemoteConsole methods and actual Commands/Parser with real Nini/UUID/XML. Controlled TickCount/disabled constructor timer, recording HTTP hooks and deterministic test command callbacks; random session IDs normalized. All BMP raw/percent-u form units with original surrogate validation, session/poll replies, scrollback, expiration and prebuffered reads. Real HTTP polling/wait timing, arbitrary protected-queue mutation and full simulator parity remain unproved.',sourceSHA256={str(path.relative_to(root)):hashlib.sha256(path.read_bytes()).hexdigest() for path in [remote,commands]})
(work/'results.json').write_text(json.dumps(report,indent=2),encoding='utf-8');assert report['passed'],f"{report['differenceCount']} remote console differences; see results.json"
print(f'PASS: {len(cases)} original remote console scenarios')
