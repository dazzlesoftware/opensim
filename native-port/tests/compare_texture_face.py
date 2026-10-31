"""Compare mutable texture faces with the bundled SDK, including parent aliasing."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]

def main(model='texture_face', supplied_cases=None, multiplier=4):
    parser = argparse.ArgumentParser()
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for key, value in {'TargetFramework':'net8.0', 'OutputType':'Exe', 'EnableDefaultCompileItems':'false'}.items():
        ET.SubElement(props, key).text = value
    items = ET.SubElement(project, 'ItemGroup')
    ET.SubElement(items, 'Compile', Include=str(ROOT / f'native-port/tests/{model}_reference.cs'))
    libraries = ['OpenMetaverse', 'OpenMetaverseTypes', 'OpenMetaverse.StructuredData']
    for name in libraries:
        node = ET.SubElement(items, 'Reference', Include=name)
        ET.SubElement(node, 'HintPath').text = str(ROOT / 'bin' / (name + '.dll'))
    path = work / 'TextureFaceReference.csproj'
    ET.ElementTree(project).write(path, encoding='utf-8', xml_declaration=True)
    built = subprocess.run([args.dotnet, 'build', str(path), '-c', 'Release', '-o', str(work / 'app'), '--nologo', '-v', 'quiet'], capture_output=True, text=True)
    if built.returncode:
        raise RuntimeError(built.stdout + built.stderr)
    cases = []
    # Equal inherited writes, explicit overrides, raw parent vs recursive parent,
    # copy independence, invalid enum bits and every quantization boundary.
    floats = [0., -0., .5, -.5, 1., -1., 2., -2., 3.1415927410125732, 6.2831854820251465, 1/32767, -1/32767, .5/32767, -.5/32767, 1/255, 100000., float('nan'), float('inf'), -float('inf')]
    bits = [struct.unpack('>I', struct.pack('>f', n))[0] for n in floats]
    for op in range(16):
        for b in (bits if op < 6 or op == 11 else [0,1,2,31,32,64,128,192,255,256,0xffffffff]):
            for index in [0,1,2,3]:
                cases.append(f'{index} {op} {b:x}')
    rng = random.Random(80132)
    for _ in range(2048):
        cases.append(f'{rng.randrange(4)} {rng.randrange(16)} {rng.getrandbits(32):x}')
    if supplied_cases is not None:
        cases = supplied_cases
    results = []
    for command in [[args.dotnet, str(work / 'app/TextureFaceReference.dll')], [args.native]]:
        run = subprocess.run(command, input='\n'.join(cases)+'\n', capture_output=True, text=True, encoding='utf-8', timeout=60)
        if run.returncode:
            raise RuntimeError(run.stdout[-1000:] + run.stderr)
        results.append(run.stdout.splitlines())
    if any(len(rows) != len(cases) for rows in results):
        raise AssertionError([len(cases), *map(len, results)])
    differences = [{'index':i, 'input':case, 'source':a, 'native':b} for i,(case,a,b) in enumerate(zip(cases,*results)) if a != b]
    report = {'passed':not differences, 'operations':len(cases), 'observations':len(cases)*multiplier, 'libraryHashes':{name:hashlib.sha256((ROOT/'bin'/(name+'.dll')).read_bytes()).hexdigest() for name in libraries}, 'differences':differences[:20]}
    (work/(model.replace('_','-')+'-parity-results.json')).write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    if differences:
        raise AssertionError(json.dumps(differences[:2],indent=2))
    print(f'PASS: {len(cases)*multiplier} {model.replace("_"," ")} observations')

if __name__ == '__main__':
    main()
