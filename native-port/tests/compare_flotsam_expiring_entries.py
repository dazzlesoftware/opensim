"""Full unchanged ExpiringKey/ExpiringCacheOS against adopted native entries."""
import argparse,hashlib,itertools,json,random,struct,subprocess,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def call(command,rows=None):
 result=subprocess.run(command,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=110)
 assert result.returncode==0,(result.returncode,result.stdout[-3000:],result.stderr[-3000:])
 return result.stdout.splitlines()
def bits(value):return format(struct.unpack('<Q',struct.pack('<d',value))[0],'016x')
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--native',required=True);parser.add_argument('--dotnet',required=True);parser.add_argument('--work',required=True,type=Path);parser.add_argument('--prepare-only',action='store_true');args=parser.parse_args();work=args.work.resolve();work.mkdir(parents=True,exist_ok=True)
 slutil=ROOT/'OpenSim/Framework/SLUtil.cs';source=slutil.read_text(encoding='utf-8-sig');conversion=work/'SLUtil.cs';conversion.write_text(source[:source.index('static readonly char[] seps')]+'\n}}\n',encoding='utf-8')
 originals=[ROOT/'OpenSim/Framework'/name for name in ['AssetBase.cs','NrbfReader.cs','ExpiringKey.cs','ExpiringCacheOS.cs']]
 commands=[]
 for real in [False,True]:
  project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
  settings=dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',AllowUnsafeBlocks='true',NoWarn='0436;0649;0169;0414')
  if real:settings['DefineConstants']='REAL_TIMERS'
  for key,value in settings.items():ET.SubElement(props,key).text=value
  group=ET.SubElement(project,'ItemGroup')
  for file in originals+[conversion,ROOT/'native-port/tests/flotsam_expiring_entries_reference.cs']:ET.SubElement(group,'Compile',Include=str(file))
  ref=ET.SubElement(group,'Reference',Include='OpenMetaverseTypes');ET.SubElement(ref,'HintPath').text=str(ROOT/'bin/OpenMetaverseTypes.dll')
  name='ExpiringRealReference' if real else 'ExpiringReference';cs=work/(name+'.csproj');ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);app=work/('real-app' if real else 'app');call([args.dotnet,'build',str(cs),'-o',str(app),'--nologo','-v','quiet']);commands.append([args.dotnet,str(app/(name+'.dll'))])
 if args.prepare_only:print('Prepared unchanged original manual and real expiring container oracles');return
 rows=[];periods=[-2147483648,-1,0,499,500,501,2000,300000,2147483647]
 for key_check,asset_check,epoch,step in itertools.product(periods,periods,[0.,-1000.,2147483600.,9007199254740992.],[0.,.25]):
  rows += [f'init {key_check} {asset_check} {bits(epoch)} {bits(step)}','keyadd 0 120','assetadd 0 120 0','drop 0','keyget 0','assetget 0','jump 500','keyget 0','assetget 0','pump','advance 1','keyget 0','assetget 0','keyadd 1 0','assetadd 1 -1 -1','assetget 1','keyclear','assetclear','keyadd 2 1','assetadd 2 1 2','drop 2',f'newk {key_check}',f'newa {asset_check}','alive 2',f'advance {max(500,key_check,asset_check)*2+1}','alive 2','keyadd 4 1000','assetadd 4 1000 4','drop 4','keydispose','assetdispose','keyget 4','assetget 4','keyadd 5 1','assetadd 5 1 5','keyremove 4','assetremove 4','keyclear','assetclear','keydispose','assetdispose','alive 4','pump']
 for check,ttl in itertools.product(periods,[-2147483648,-1,0,1,499,500,501,1999,2000,2001,2147483647]):
  rows += [f'init {check} {check} {bits(0.)} {bits(0.)}',f'keyadd 1 {ttl}',f'assetadd 1 {ttl} 1','drop 1','jump 500','keyget 1','assetget 1','pump','advance 1','keyget 1','assetget 1','keyadd 1 2147483647','assetadd 1 2147483647 2','advance 2000','keyremove 1','assetremove 1','alive 1']
 rng=random.Random(614820)
 for epoch,check in itertools.product([0.,2147483600.,2147483648.,-2147483650.],[500,2000,300000]):
  rows.append(f'init {check} {check} {bits(epoch)} {bits(0.)}')
  for _ in range(700):
   op=rng.choice(['keyadd','assetadd','keyget','assetget','keyremove','assetremove','keyclear','assetclear','drop','alive','mutate','advance','jump','pump','newk','newa']);key=rng.randrange(4)
   if op=='keyadd':row=f'{op} {key} {rng.choice([-2147483648,-1,0,1,500,2000,2147483647])}'
   elif op=='assetadd':row=f'{op} {key} {rng.choice([-2147483648,-1,0,1,500,2000,2147483647])} {rng.randrange(-1,4)}'
   elif op in ['advance','jump']:row=f'{op} {rng.choice([-1,0,1,499,500,501,1999,2000,2001,2147483647])}'
   elif op in ['newk','newa']:row=f'{op} {rng.choice(periods)}'
   elif op in ['keyclear','assetclear','pump']:row=op
   else:row=f'{op} {key}'
   rows.append(row)
 outputs=[call(commands[0],rows),call([args.native,'--probe'],rows)];assert len(outputs[0])==len(outputs[1])==len(rows),(len(outputs[0]),len(outputs[1]),len(rows))
 differences=[dict(index=i,command=row,source=original,native=native,context=rows[max(0,i-6):i]) for i,(row,original,native) in enumerate(zip(rows,*outputs)) if original!=native]
 retained=[call(commands[1]),call([args.native,'--retained'])];assert retained==[['1:1:1:1'],['1:1:1:1']],retained
 files=originals+[slutil,ROOT/'bin/OpenMetaverseTypes.dll'];report=dict(passed=not differences,cases=len(rows),realRetained=retained,sourceHashes={str(file.relative_to(ROOT)):hashlib.sha256(file.read_bytes()).hexdigest() for file in files},scope='Full unchanged original ExpiringKey/ExpiringCacheOS classes with actual AssetBase/SDK. Used int-ms add/get/contains/remove/clear/dispose/count methods, constructor clamp and separately captured epoch/check fields, manual one-shot timer due state, strict expiration, no read-time purge, no refresh on updates, null asset values, strong shared mutation, signed Int32 casts/wrap and long uptime are compared. Replacement retains armed old containers during manual scheduling. Separate actual BCL and native timers prove armed asset retention after wrapper loss, eventual release, and disposed value/count preservation plus failed-get output preservation. API accepts non-null string keys; nullable keys, arbitrary throwing clock callbacks, allocation failures, lock-recursion and all concurrent/GC schedules remain unverified.',differences=differences[:6]);(work/'flotsam-expiring-entries-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not differences,json.dumps(differences[:3]);print(f'PASS: {len(rows)} original expiring entry state/epoch/timer/lifetime observations')
if __name__=='__main__':main()
