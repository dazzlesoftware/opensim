"""Actual .NET status-file enumeration/local timestamps compared to native I/O."""
import argparse,ctypes,datetime,hashlib,json,os,stat,subprocess,tempfile,xml.etree.ElementTree as ET
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def invoke(command,rows=None):
 r=subprocess.run(command,input=None if rows is None else '\n'.join(rows)+'\n',capture_output=True,text=True,encoding='utf-8',cwd=ROOT,timeout=110)
 assert r.returncode==0,(r.returncode,r.stdout[-3000:],r.stderr[-3000:]);return r.stdout.splitlines()
def main():
 p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',required=True,type=Path);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
 project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
 for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultItems='false',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
 group=ET.SubElement(project,'ItemGroup');ET.SubElement(group,'Compile',Include=str(ROOT/'native-port/tests/flotsam_console_files_reference.cs'));cs=w/'ConsoleFilesReference.csproj';ET.ElementTree(project).write(cs,encoding='utf-8',xml_declaration=True);invoke([a.dotnet,'build',str(cs),'-o',str(w/'app'),'--nologo','-v','quiet']);managed=[a.dotnet,str(w/'app/ConsoleFilesReference.dll')]
 actual=Path(tempfile.mkdtemp(prefix='actual-',dir=w));assert actual.resolve().parent==w
 root=actual/'cache';root.mkdir();unicode_root=actual/'é漢😀';unicode_root.mkdir();empty=actual/'empty';empty.mkdir();sub=root/'dir.fac';sub.mkdir();(sub/'nested.fac').write_bytes(b'nested')
 names=['a.fac','B.FAC','c.Fac','no.txt','abc.fac.txt','faç.fac','漢.fac','.fac','file..fac','long name prefix fac document.facx','hidden.fac','readonly.fac']
 for folder in [root,unicode_root]:
  for name in names:(folder/name).write_bytes(name.encode())
 (root/'readonly.fac').chmod(stat.S_IREAD)
 capabilities={};timestamps=[];extra=[]
 if os.name=='nt':
  from ctypes import wintypes as T
  k=ctypes.WinDLL('kernel32',use_last_error=True);k.CreateFileW.argtypes=[T.LPCWSTR,T.DWORD,T.DWORD,T.LPVOID,T.DWORD,T.DWORD,T.HANDLE];k.CreateFileW.restype=T.HANDLE;k.CloseHandle.argtypes=[T.HANDLE];k.SetFileTime.argtypes=[T.HANDLE,T.LPVOID,T.LPVOID,T.LPVOID];k.SetFileAttributesW.argtypes=[T.LPCWSTR,T.DWORD];k.GetShortPathNameW.argtypes=[T.LPCWSTR,T.LPWSTR,T.DWORD]
  def extended(path):return '\\\\?\\'+str(path)
  for name in ['trailing.fac.','trailing.fac ']:
   path=root/name;handle=k.CreateFileW(extended(path),0x40000000,7,None,1,0x80,None);assert handle not in [None,ctypes.c_void_p(-1).value],ctypes.get_last_error();k.CloseHandle(handle);extra.append(extended(path))
  assert k.SetFileAttributesW(str(root/'hidden.fac'),2|4),ctypes.get_last_error()
  def stamp(path,ticks):
   handle=k.CreateFileW(extended(path),0x100,7,None,3,0x80,None);assert handle not in [None,ctypes.c_void_p(-1).value],ctypes.get_last_error();value=ctypes.c_uint64(ticks);ok=k.SetFileTime(handle,None,None,ctypes.byref(value));code=ctypes.get_last_error();k.CloseHandle(handle);return bool(ok),code
  values=[0,1]
  for date in [(1900,1,1,0,0),(2024,7,1,12,0),(2024,11,3,7,30),(2024,11,3,8,30),(2025,1,1,0,0),(9999,12,31,12,0)]:
   d=datetime.datetime(*date,tzinfo=datetime.timezone.utc);delta=d-datetime.datetime(1601,1,1,tzinfo=datetime.timezone.utc);values.append((delta.days*86400+delta.seconds)*10000000)
  values+=[0x7fffffffffffffff]
  for i,value in enumerate(values):
   path=root/f'time-{i}.fac';path.write_bytes(b'time');ok,code=stamp(path,value);capabilities['timestamp-'+str(i)]=dict(set=ok,error=None if ok else code,ticks=value)
   if ok:timestamps.append(str(path))
  short=ctypes.create_unicode_buffer(32768);length=k.GetShortPathNameW(str(root/names[9]),short,len(short));capabilities['shortAlias']=short.value if length else None
 else:
  (root/'hidden.fac').chmod(stat.S_IRUSR|stat.S_IWUSR)
  for i,seconds in enumerate([-2208988800,1719835200,1730619000,1730622600,1735689600]):
   path=root/f'time-{i}.fac';path.write_bytes(b'time');os.utime(path,ns=(seconds*10**9,seconds*10**9));timestamps.append(str(path))
 for name,target,directory in [('link.fac',root/'a.fac',False),('directory-link.fac',sub,True),('broken.fac',root/'absent',False)]:
  try:os.symlink(target,root/name,target_is_directory=directory);capabilities[name]=True
  except OSError as error:capabilities[name]=dict(skipped=True,error=str(error))
 if os.name=='nt':
  for name,target in [('directory-junction.fac',sub),('broken-junction.fac',root/'absent-dir')]:
   result=subprocess.run(['cmd','/d','/c','mklink','/J',str(root/name),str(target)],capture_output=True,text=True,encoding='utf-8');capabilities[name]=dict(created=result.returncode==0,output=result.stdout+result.stderr)
 try:os.link(root/'a.fac',root/'hardlink.fac');capabilities['hardlink.fac']=True
 except OSError as error:capabilities['hardlink.fac']=dict(skipped=True,error=str(error))
 wide=root/'wide';wide.mkdir()
 for i in range(180):
  (wide/(f'{i:04d}-'+('name'*8)+('.fac' if i%2 else '.FAC'))).write_bytes(b'wide')
  if i%6==0:(wide/(f'{i:04d}.txt')).write_bytes(b'ignored')
 long_file=root/(('n'*251)+'.fac');long_file.write_bytes(b'long component');extra.append(str(long_file))
 deep=actual/'long'
 for i in range(8):deep=deep/('long-'+str(i)+'-'+'x'*30)
 deep.mkdir(parents=True);(deep/'deep.fac').write_bytes(b'deep')
 paths=[str(wide),os.path.relpath(wide,ROOT),str(root),str(unicode_root),str(empty),str(sub),str(actual/'missing'),str(actual/'missing'/'child'),str(root/'a.fac'),str(root/'a.fac'/'child'),str(deep),str(root)+os.sep,str(root)+'/',str(root).replace('\\','/'),os.path.relpath(root,ROOT),'.'+os.sep+os.path.relpath(root,ROOT),str(root/os.curdir),str(root.parent/'cache'/'..'/'cache'),'','   ',None,str(root)+'\x00tail',str(root/'bad*'),str(root/'bad?'),str(root/'bad:stream'),str(root/'NUL')]
 if os.name=='nt':paths += ['\\\\?\\'+str(root),'\\\\?\\'+str(deep),str(root)+'.',str(root)+'   ',str(root)+'\\'+('x'*32768),str(root).replace('\\','/')+'/'+('x'*32768),os.path.relpath(root,ROOT)+'\\'+('x'*32768),str(root)+'\\..\\cache\\'+('x'*32768),'x'*32750]
 paths += [str(root/name) for name in ['directory-junction.fac','broken-junction.fac'] if isinstance(capabilities.get(name),dict) and capabilities[name].get('created')]
 rows=[];cases=[]
 def add(op,path):rows.append(op+' '+('~' if path is None else str(path).encode().hex()));cases.append(dict(operation=op,path=None if path is None else str(path)))
 for path in paths:
  add('enum',path);add('time',path)
 for path in timestamps+extra+[str(root/name) for name in names]+[str(root/name) for name in ['link.fac','directory-link.fac','broken.fac','directory-junction.fac','broken-junction.fac','hardlink.fac']]:add('time',path)
 if os.name=='nt':
  volume=ctypes.create_unicode_buffer(1024);k.GetVolumeNameForVolumeMountPointW.argtypes=[T.LPCWSTR,T.LPWSTR,T.DWORD];ok=k.GetVolumeNameForVolumeMountPointW(root.anchor,volume,len(volume));capabilities['volumeRoot']=volume.value if ok else None
  if ok:add('time',volume.value)
  add('time','\\\\.\\'+root.anchor)
 if os.name=='nt':add('enum-lock',root);add('time-lock',root/'a.fac');add('time-lock',root)
 outputs=[invoke(managed,rows),invoke([a.native,'--probe'],rows)];assert len(outputs[0])==len(outputs[1])==len(rows)
 diffs=[dict(case=c,source=s,native=n) for c,s,n in zip(cases,*outputs) if s!=n]
 source=Path(a.dotnet).parent/'shared/Microsoft.NETCore.App/8.0.31/System.Private.CoreLib.dll';report=dict(passed=not diffs,cases=len(rows),actualRoot=str(actual),multiBufferFiles=180,platform=os.name,timezone=invoke(managed+['--zone'])[0],capabilities=capabilities,observations=[dict(case=c,source=s,native=n) for c,s,n in zip(cases,*outputs)],referenceSha256=hashlib.sha256((ROOT/'native-port/tests/flotsam_console_files_reference.cs').read_bytes()).hexdigest(),runtimeSha256=hashlib.sha256(source.read_bytes()).hexdigest() if source.exists() else None,scope='Actual Directory.GetFiles(root,"*.fac") long-name matching, native order and specified-root spelling plus File.GetLastWriteTime local ticks/kind/raw DateTime data and typed errors. Existing/missing/file/directory/read-only/hidden/system/Unicode/case/trailing-literal/relative/extended/long/wildcard/null/invalid/locked paths and available symlinks are compared. Formatting and arbitrary culture/timezone/platform/race behavior remain separate; no date parser or file-cache host is installed.',differences=diffs[:8]);(w/'flotsam-console-files-results.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');assert not diffs,json.dumps([dict(case=str(d['case'])[:180],source=d['source'][:300],native=d['native'][:300]) for d in diffs[:3]],indent=2);print(f'PASS: {len(rows)} actual .NET status-file API observations')
if __name__=='__main__':main()
