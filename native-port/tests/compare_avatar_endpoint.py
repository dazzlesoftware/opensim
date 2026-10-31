"""Original AvatarServerPostHandler and XmlConvert comparisons, including request order."""
import argparse,hashlib,json,random,subprocess,urllib.parse
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_appearance_provisioner import reference,ROOT,extract

def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();w.mkdir(parents=True,exist_ok=True);base,paths=reference(a.dotnet,w/'base');project=ET.parse(base);ET.SubElement(project.getroot().find('PropertyGroup'),'StartupObject').text='AvatarHandler';group=project.getroot().find('ItemGroup')
    handler='OpenSim/Server/Handlers/Avatar/AvatarServerPostHandler.cs';utils='OpenSim/Server/Base/ServerUtils.cs';model='OpenSim/Services/Interfaces/IAvatarService.cs'
    source=(ROOT/handler).read_text(encoding='utf-8-sig');methods=[extract(source,r'(?:protected override|private|)\s*(?:byte\[\]|void) '+n+r'\(').replace('protected override','public') for n in ['ProcessRequest','GetAvatar','SetAvatar','ResetAvatar','RemoveRequestParamsNotForStorage','SetItems','RemoveItems','SuccessResult','FailureResult']]
    wrapper=w/'Handler.cs';wrapper.write_text('using System;using System.IO;using System.Xml;using System.Text;using System.Collections.Generic;using OpenMetaverse;using OpenSim.Services.Interfaces;using OpenSim.Server.Base;using Util=AvatarEndpointUtil;public partial class AvatarHandler {'+'\n'.join(methods)+'}',encoding='utf-8')
    source=(ROOT/utils).read_text(encoding='utf-8-sig');server=w/'ServerUtils.cs';server.write_text('using System;using System.Collections.Generic;using System.Xml;using SWH=System.Web.HttpUtility;namespace OpenSim.Server.Base{public static class ServerUtils{'+ '\n'.join(extract(source,r'(?:public|private) static [^\n{]+ '+n+r'\(') for n in ['ParseQueryString','BuildXmlResponse','BuildXmlData'])+'}}',encoding='utf-8')
    for f in [wrapper,server,ROOT/model,ROOT/'OpenSim/Framework/Culture.cs',ROOT/'native-port/tests/avatar_endpoint_reference.cs']:ET.SubElement(group,'Compile',Include=str(f))
    path=w/'AvatarEndpoint.csproj';project.write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    seven='00000000-0000-0000-0000-000000000007';bodies=['','METHOD=unknown','METHOD[]=getavatar']
    for method in ['getavatar','setavatar','resetavatar','setitems','removeitems']:
        for user in [None,'','bad',seven,seven[:-1]+'1',seven[:-1]+'8',seven[:-1]+'9',seven[:-1]+'0',seven.replace('-',''),seven+'tail']:
            prefix='METHOD='+method+('' if user is None else '&UserID='+user)
            for suffix in ['', '&Names=a&Values=b','&Names[]=a&Values[]=b','&Names[]=a&Values=b','&Names=a&Values[]=b','&Names[]=z&Names[]=a&Values[]=1&Values[]=2','&AvatarType=1&z=first&a=last&VERSIONMAX=9&VERSIONMIN=1','&AvatarType[]=1&field[]=one&field[]=two']:
                bodies.append(prefix+suffix)
    for suffix in ['&AvatarType=bad&AvatarType=7','&Names=scalar&Names[]=ignored','&field[]=list&field=ignored','&z=1&UserID='+seven+'&a=2','&%20=blank&%00=zero','&x=a=b&z=c','&field=%uD800','&field=%FF','&field=%u0061','&[]=ignored','&AvatarType=12%00','&AvatarType=2147483648']:
        bodies.append('METHOD=setavatar&UserID='+seven+suffix)
    bodies += ['\ufeffMETHOD=getavatar&UserID='+seven,'\u00a0METHOD=getavatar&UserID='+seven+'\u00a0']
    raw=[s.encode() for s in bodies]
    for encoding,bom in [('utf-8',b'\xef\xbb\xbf'),('utf-16-le',b'\xff\xfe'),('utf-16-be',b'\xfe\xff'),('utf-32-le',b'\xff\xfe\0\0'),('utf-32-be',b'\0\0\xfe\xff')]:
        for text in ['METHOD=getavatar&UserID='+seven,'METHOD=setavatar&UserID='+seven+'&field=試験 🦊','METHOD=setavatar&UserID='+seven+'&field=']:
            for extra in [b'',b'\xff',b'\0']:
                raw.append(bom+text.encode(encoding)+extra);bodies.append(encoding+':'+text+':'+extra.hex())
    for invalid in [b'\xff',b'\xc0\x80',b'\xed\xa0\x80',b'\xf0\x9f']:
        raw.append(('METHOD=setavatar&UserID='+seven+'&field=').encode()+invalid);bodies.append('invalid UTF8:'+invalid.hex())
    commands=[s.hex() for s in raw]
    # Every valid Unicode scalar in BMP, in first and subsequent name positions.
    names=[]
    for n in range(65536):
        if 0xd800<=n<=0xdfff:continue
        names.extend([chr(n),'A'+chr(n)])
    names += ['','Wearable 0:0','_x0041_','_X0041_','a_x0001F98A_','_x0041_x0042_','_x0041__x0042_','_x004_','_x00000_',':','a:b','a.b','a-b']
    rng=random.Random(1717)
    for n in [0x10000,0x1f98a,0xeffff,0x10ffff]+[rng.randrange(0x10000,0x110000) for _ in range(1000)]:names.extend([chr(n),'A'+chr(n)])
    commands += ['name '+s.encode().hex() for s in names]
    config=w/'server.ini';config.write_text('[AvatarService]\n',encoding='utf-8');rows=[];data='\n'.join(commands)+'\n'
    for cmd in [[a.dotnet,str(w/'app/AvatarEndpoint.dll')],[a.native,str(config)]]:
        r=subprocess.run(cmd,input=data,capture_output=True,text=True,encoding='utf-8',timeout=60)
        if r.returncode:raise RuntimeError(r.stdout[-500:]+r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0])==len(rows[1])==len(commands)
    differences=[{'request':bodies[i] if i<len(bodies) else names[i-len(bodies)],'source':x,'native':y} for i,(x,y) in enumerate(zip(*rows)) if x!=y]
    (w/'avatar-endpoint-parity-results.json').write_text(json.dumps({'passed':not differences,'handlerObservations':len(bodies),'xmlNameObservations':len(names),'sourceHashes':{s:hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in [handler,utils,model]},'differences':differences[:3]},indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:2],indent=2))
    print(f'PASS: {len(commands)} avatar endpoint source observations ({len(bodies)} requests, {len(names)} XML names)')
if __name__=='__main__':main()
