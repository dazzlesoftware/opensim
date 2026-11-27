"""Original SceneObjectGroup.AbsolutePosition callback ordering and state."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import random
import struct
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT, extract
from compare_inventory_folder_decode import run

p=argparse.ArgumentParser()
p.add_argument('--native',required=True);p.add_argument('--dotnet',required=True)
p.add_argument('--work',type=Path,required=True);p.add_argument('--reference',type=Path)
a=p.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
source=ROOT/'OpenSim/Region/Framework/Scenes/SceneObjectGroup.cs'
if not a.reference:
    prop=extract(source.read_text(encoding='utf-8-sig'),r'public override Vector3 AbsolutePosition\b')
    template=Path(__file__).with_name('group_position_reference.cs').read_text()
    (work/'Reference.cs').write_text(template.replace('/*SOURCE_PROPERTY*/',prop))
    project=ET.Element('Project',Sdk='Microsoft.NET.Sdk');props=ET.SubElement(project,'PropertyGroup')
    for k,v in dict(TargetFramework='net8.0',OutputType='Exe',EnableDefaultCompileItems='false').items():ET.SubElement(props,k).text=v
    items=ET.SubElement(project,'ItemGroup');ET.SubElement(items,'Compile',Include='Reference.cs')
    for name in ['OpenMetaverse','OpenMetaverseTypes']:
        ref=ET.SubElement(items,'Reference',Include=name);ET.SubElement(ref,'HintPath').text=str(ROOT/'bin'/(name+'.dll'))
    ET.ElementTree(project).write(work/'Reference.csproj')
    r=subprocess.run([a.dotnet,'build',str(work/'Reference.csproj'),'-o',str(work/'app')],capture_output=True,text=True)
    assert r.returncode==0,r.stdout+r.stderr
    a.reference=work/'app/Reference.dll'
def bits(values):return [str(struct.unpack('<I',struct.pack('<f',x))[0]) for x in values]
flags=['scene','inside','attached','loading','disabled','transit','sandbox','duplicate','deleted','changed']
base=dict.fromkeys(flags,False);base.update(scene=True,inside=True,changed=True)
def case(position=(0,0,0),target=(1,2,3),origin=(0,0,0),**kw):
    return dict(base,position=bits(position),target=bits(target),origin=bits(origin),**kw)
cases=[]
for values in itertools.product([False,True],repeat=len(flags)):
    c=case();c.update(zip(flags,values));cases.append(c)
for t in [(0,0,0),(.0005,.0005,.0005),(.0008,.0008,.0008),(.001,0,0),
          (.0010000002,0,0),(10,0,0),(10.000001,0,0),(6,8,0),(6,8.000001,0),
          (float('nan'),0,0),(float('inf'),0,0),(-float('inf'),0,0)]:
    for sandbox,changed,scene in itertools.product([False,True],repeat=3):
        c=case(target=t);c.update(sandbox=sandbox,changed=changed,scene=scene);cases.append(c)
rng=random.Random(4901)
for _ in range(500):
    c=case()
    for key in ['position','target','origin']:
        c[key]=[str(rng.getrandbits(32)) for _ in range(3)]
    c['sandbox']=rng.choice([False,True]);cases.append(c)
original=run([a.dotnet,str(a.reference)],cases);native=run([a.native],cases)
differences=[dict(case=c,original=o,native=n) for c,o,n in zip(cases,original,native) if o!=n]
report=dict(passed=not differences,observations=len(cases),differences=differences[:3],
    sourceHashes={str(f.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(f.read_bytes()).hexdigest()
                  for f in [source,ROOT/'bin/OpenMetaverseTypes.dll']},
    scope='Unchanged original AbsolutePosition property; controlled scene/part/sitter/async/script callbacks, actual SDK Vector3 methods and float bits. Actual region crossing, physics effects, concurrency and attachments require live integration.')
(work/'results.json').write_text(json.dumps(report,indent=2)+'\n')
assert not differences,json.dumps(differences[:1],indent=2)
print(f'PASS: {len(cases)} original group-position observations')
