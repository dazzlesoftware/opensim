"""Compare outgoing profile methods extracted unchanged from LLClientView."""
from pathlib import Path
import argparse,subprocess,json,hashlib,xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True)
sources=['native-port/tests/profile_packets_reference.cs','OpenSim/Region/ClientStack/Linden/UDP/LLClientView.cs','OpenSim/Framework/Util.cs','OpenSim/Framework/Culture.cs','OpenSim/Framework/Constants.cs','OpenSim/Region/ClientStack/Linden/UDP/LLUDPServer.cs','OpenSim/Framework/GroupData.cs','OpenSim/Region/ClientStack/Linden/Caps/EventQueue/EventQueueGetHandlers.cs']
def extract(path,signature):
 text=path.read_bytes().decode('utf-8-sig');start=text.index(signature);brace=text.index('{',start);end=brace+1;depth=1
 while depth:
  if text[end]=='{':depth+=1
  elif text[end]=='}':depth-=1
  end+=1
 return text[start:end]
methods=[extract(root/sources[1],signature) for signature in ['public void SendAvatarProperties(','public void SendAvatarInterestsReply(','public void SendAvatarNotesReply(','public void SendUserInfoReply(','public void SendClassifiedInfoReply(','public void SendPickInfoReply(','public void SendAvatarPicksReply(','public void SendAvatarClassifiedReply(UUID targetID, Dictionary<UUID, string> classifieds)','public void SendAvatarClassifiedReply(UUID targetID, UUID[] classifiedID, string[] name)','public void SendAvatarGroupsReply(']]
helpers=[extract(root/sources[2],signature) for signature in ['public static byte[] StringToBytes256(ReadOnlySpan<char> str)','public static byte[] StringToBytes1024(ReadOnlySpan<char> str)']]
event_methods=[extract(root/sources[-1],sig) for sig in ['public osUTF8 StartEvent(string eventName)','public byte[] EndEventToBytes(']]
group_model=extract(root/sources[-2],'public class GroupMembershipData')
(w/'Extracted.cs').write_text('using System;using System.Collections.Generic;using System.Linq;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenMetaverse.Packets;using OpenSim.Framework;partial class ProfilePacketsReference{'+''.join(methods)+'}partial class ProfileEventsReference{'+''.join(event_methods)+'}namespace OpenSim.Framework{'+group_model+'static class Util{'+''.join(helpers)+'}}')

project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false',RuntimeFrameworkVersion='8.0.31',RollForward='Disable').items():ET.SubElement(props,k).text=v
items=ET.SubElement(project,'ItemGroup')
ET.SubElement(items,'Compile',Include=str(root/sources[0]));ET.SubElement(items,'Compile',Include=str(w/'Extracted.cs'))
for name in ['OpenMetaverse','OpenMetaverseTypes','OpenMetaverse.StructuredData','Nini']:
 ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(root/'bin'/str(name+'.dll'))
ET.ElementTree(project).write(w/'Reference.csproj');r=subprocess.run([a.dotnet,'build',str(w/'Reference.csproj'),'-o',str(w/'app'),'-v:q'],capture_output=True,text=True);assert r.returncode==0,r.stdout+r.stderr
def text(value):return '-' if value is None else value.encode('utf-16-le','surrogatepass').hex() or '.'
values=[None,'','hello','x\x00y','é😀','x\ud800q','\udc00','\ud800','abc\x00','A'*254,'B'*255,'C'*256,'é'*200,'😀'*70,'a'*1023,'é'*600]
commands=[f'{mode} {text(first)} {text(last)} {length} {flags}' for mode in [0,1] for first in values for last in values[::2] for length in [-1,0,1,254,255,256,257,520] for flags in [0,4294967295]]
commands += [f'{mode} {text(value)} . 0 {flags}' for mode in [2,3] for value in values+[('x'*n) for n in [1020,1021,1022,1023,65534,65535,65536,65537]] for flags in [0,1,2,3]]
commands += [f'{mode} {text(first)} {text(last)} {position} {flags}' for mode in [4,5] for first in values for last in values[::2] for position in range(4) for flags in [0,1,2,3,4,78,128,4294967295]]
commands += [f'4 {text("hello")} . 0 {flags}' for flags in range(256)]
commands += [f'{mode} {text("x"*n)} . 0 0' for mode in [4,5] for n in [65534,65535,65536,65537]]
list_values=values+['a','A','ä','z','å','Ω','a\u0308','I','ı','İ','i','ß','ss']
commands += [f'{mode} {text(first)} {text(last)} {count} {flags}' for mode in [6,7] for first in list_values for last in list_values[::6] for count in [-1,0,1,2,3,15,16,17,19,20,21,23,31,32,33] for flags in [0,1,3]]
commands += [f'{mode} {text(first)} {text(last)} {count} {flags}' for mode in [6,7] for first in [None,'','a','ä','😀','x\x00y'] for last in [None,'A','ä'] for count in [64,128,254,255,256,257,520] for flags in [0,1,2,3]]
commands += [f'8 . . {count} {names&4294967295}' for count in [-1,0,1,2,20] for names in [-1,0,1,2,20]]
group_values=values+['<&>\"\'','a\ud800<&z','\ud800\ud800\udc00','\x01\x1f\x7f']
commands += [f'9 {text(first)} {text(last)} {count} {flags}' for first in group_values for last in group_values[::4] for count in [-1,0,1,3,20] for flags in [0,1,2,3,4,16,17,32,33,64,128,256,512,1024,2048]]
commands += [f'9 {text(chr(unit))} . 1 0' for unit in range(65536)]
outputs={}
for name,cmd in [('reference',[a.dotnet,str(w/'app/Reference.dll')]),('native',[a.native])]:
 with (w/(name+'.txt')).open('w',encoding='utf-8') as output:r=subprocess.run(cmd,input='\n'.join(commands)+'\n',stdout=output,stderr=subprocess.PIPE,text=True,timeout=180)
 assert r.returncode==0,r.stderr;outputs[name]=(w/(name+'.txt')).read_text(encoding='utf-8').splitlines();assert len(outputs[name])==len(commands),(name,len(outputs[name]),len(commands))
diffs=[dict(command=c,source=x,native=y) for c,x,y in zip(commands,outputs['reference'],outputs['native']) if x!=y]
report=dict(passed=not diffs,scenarios=len(commands),differenceCount=len(diffs),differences=diffs[:12],applicationParityVerified=False,scope='Complete original LLClientView SendAvatarProperties/SendAvatarInterestsReply/SendAvatarNotesReply/SendUserInfoReply/SendClassifiedInfoReply/SendPickInfoReply and original Util bounded string helper bodies, actual SDK packet serializers and zero coder. Controlled PacketPool/OutPacket only. Exact raw and zero-coded bytes across null/empty/NUL/malformed UTF16, byte boundaries and overlong charter/interest/email/classified/pick fields, complete classified maturity flags, signed/unsigned limits and float-to-double coordinates; SDK wraps length prefixes while retaining full payload. Complete SendAvatarGroupsReply with original group model, EventQueue StartEvent/EndEventToBytes and actual SDK LLSDxmlEncode2/osUTF8 includes self/other filtering, queue absence/failures/partial mutation, ulong binary powers and all BMP code units. Picks invariant-culture source worker collation, original .NET List.Sort and cap20/MTU length, dictionary slot reuse enumeration and classified array-overload null/length/uninitialized-block behavior included. Source socket/queue/session/runtime and whole application/macOS integration remain incomplete.',sourceHashes={f:hashlib.sha256((root/f).read_bytes()).hexdigest() for f in sources})
(w/'results.json').write_text(json.dumps(report,indent=2));assert not diffs,str(len(diffs))+' profile packet differences; see results.json';print('PASS:',len(commands),'original profile packet scenarios')
