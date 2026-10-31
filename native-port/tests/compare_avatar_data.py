"""Compare actual AvatarData constructors, dictionary export and appearance reload."""
import argparse,hashlib,json,random,struct,subprocess
from pathlib import Path
import xml.etree.ElementTree as ET
from compare_appearance_provisioner import reference,ROOT,extract
def main():
    p=argparse.ArgumentParser();p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True);p.add_argument('--work',type=Path,required=True);a=p.parse_args();w=a.work.resolve();base,_=reference(a.dotnet,w/'base');project=ET.parse(base);props=project.getroot().find('PropertyGroup');ET.SubElement(props,'StartupObject').text='AvatarDataReference';group=project.getroot().find('ItemGroup')
    interface='OpenSim/Services/Interfaces/IAvatarService.cs';snapshot=extract((ROOT/'native-port/tests/avatar_appearance_reference.cs').read_text(encoding='utf-8'),r'static string Snapshot\(');helpers=w/'Snapshot.cs';helpers.write_text('using System;using OpenSim.Framework;public partial class AvatarDataReference{'+snapshot+'}',encoding='utf-8')
    for f in [ROOT/interface,ROOT/'OpenSim/Framework/Culture.cs',ROOT/'native-port/tests/avatar_data_reference.cs',helpers]:ET.SubElement(group,'Compile',Include=str(f))
    path=w/'AvatarData.csproj';project.write(path,encoding='utf-8',xml_declaration=True);r=subprocess.run([a.dotnet,'build',str(path),'-c','Release','-o',str(w/'app'),'--nologo','-v','quiet'],capture_output=True,text=True)
    if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    def token(s):return '-' if s is None else s.encode().hex() or '.'
    commands=['default','empty','type 7','export','round'];seven='00000000-0000-0000-0000-000000000007';eight=seven[:-1]+'8'
    values=[None,'','bad','0','-1','255','256','-257','2147483648','1,2','1,234.5','0.5','NaN','Infinity','-Infinity','∞','+NaN','1e100',' 1.9 ','\u00a01.9\u00a0','12\0','12\0 ','1,2,3',seven,seven+':'+eight,seven+'tail:'+eight,'bad:'+eight,seven+':'+eight+':extra']
    for key in ['Serial','AvatarHeight','VisualParams','Wearable 0:0','Wearable 16:0','Wearable 17:0','Wearable -1:0','Wearable 3:any','Wearable 0','_ap_5','_ap_129','AvatarType','ignored']:
        for v in values:commands+=['empty','set '+token(key)+' '+token(v),'export','round']
    for name in ['Body','Skin','Hair','Eyes','Shirt','Pants','Shoes','Socks','Jacket','Gloves','UnderShirt','UnderPants','Skirt']:
        commands+=['empty','set '+token(name+'Item')+' '+token(seven),'set '+token(name+'Asset')+' '+token(eight),'round']
    for key in ['Wearable 0:0','Wearable 18:0','Wear\0able 0:0','Wearable \u03010:0','_a\0p_5','_ap_\u034f5']:
        commands+=['empty','set '+token(key)+' '+token(seven+':'+eight)]
    for key in ['AvatarType','field']:
        for kind,value in [('null','.'),('int','-7'),('bool','1'),('string',token('bad')),('float','7f800000')]:commands+=['kv '+token(key)+' '+kind+' '+value,'export']
    for mode in range(12):commands+=['ap '+str(mode)+' 3ff33333','round']
    for number in [0.,-0.,.1,1.9,32.,float('nan'),float('inf'),-float('inf'),1e-6,1e20]:commands+=['ap 1 '+struct.pack('>f',number).hex(),'round']
    for value in ['1,','1,,2','1,.2',',1','1.2,3','1e2,3','+Infinity','infinity','\u00a0Infinity\u00a0','NaN\0','1 \0','1\0 ','1\0\0','1e-1000','-0','+1.5','  +1.5  ','1\t2','1_2','\u22121','1,234,567','1e999999','-NaN']:
        commands+=['empty','set '+token('AvatarHeight')+' '+token(value)]
    for i in [-2,-1,0,1,4,14,15,16,17,18,100,2147483647]:
        commands+=['empty','set '+token(f'Wearable {i}:ignored')+' '+token(seven+':'+eight)]
    for value in [seven+'tail',seven.replace('-','')+'tail',' '+seven+' ','{'+seven+'}',seven.upper(),'g'+seven[1:]]:
        commands+=['empty','set '+token('BodyItem')+' '+token(value),'set '+token('BodyAsset')+' '+token(eight)]
    commands+=['default','remove '+token('missing'),'empty','set '+token('z')+' '+token('1'),'set '+token('a')+' '+token('2'),'remove '+token('z'),'set '+token('Serial')+' '+token('3'),'export']
    rng=random.Random(39111)
    for _ in range(200):
        i=rng.randrange(-2,23);v=rng.choice(values);commands+=['empty','set '+token(f'Wearable {i}:0')+' '+token(v)]
    rows=[]
    for cmd in [[a.dotnet,str(w/'app/AvatarData.dll')],[a.native]]:
        r=subprocess.run(cmd,input='\n'.join(commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=45)
        if r.returncode:raise RuntimeError(r.stdout[-500:]+r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0])==len(rows[1])==len(commands)
    differences=[{'input':c,'source':x,'native':y} for c,x,y in zip(commands,*rows) if x!=y]
    (w/'avatar-data-parity-results.json').write_text(json.dumps({'passed':not differences,'observations':len(commands),'sourceHash':hashlib.sha256((ROOT/interface).read_bytes()).hexdigest(),'differences':differences[:4]},indent=2)+'\n',encoding='utf-8')
    if differences:raise AssertionError(json.dumps(differences[:1],indent=2)[:4000])
    print(f'PASS: {len(commands)} avatar data source observations')
if __name__=='__main__':main()
