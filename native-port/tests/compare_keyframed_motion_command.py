"""Compare original llSetKeyframedMotion list parsing and command side-effect order."""
import argparse
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT, extract
from compare_inventory_folder_decode import run


def build_reference(dotnet, work):
    api = ROOT / 'OpenSim/Region/ScriptEngine/Shared/Api/Implementation/LSL_Api.cs'
    types = ROOT / 'OpenSim/Region/ScriptEngine/Shared/LSL_Types.cs'
    wrapper = Path(__file__).with_name('keyframed_motion_command_reference.cs').read_text()
    replacements = {
        'API': extract(api.read_text(encoding='utf-8-sig'), r'public void llSetKeyframedMotion\('),
        'INTEGER': extract(types.read_text(encoding='utf-8-sig'), r'public int GetIntegerItem\('),
        'FLOAT': extract(types.read_text(encoding='utf-8-sig'), r'public float GetStrictFloatItem\('),
        'VECTOR': extract(types.read_text(encoding='utf-8-sig'), r'public LSL_Types.Vector3 GetVector3Item\('),
        'ROTATION': extract(types.read_text(encoding='utf-8-sig'), r'public LSL_Types.Quaternion GetQuaternionItem\('),
        'NORMALIZE': extract(types.read_text(encoding='utf-8-sig'), r'public Quaternion Normalize\('),
    }
    for name, method in replacements.items():
        wrapper = wrapper.replace('/*' + name + '*/', method)
    (work / 'Program.cs').write_text(wrapper)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for name, value in {'TargetFramework': 'net8.0', 'OutputType': 'Exe', 'Nullable': 'disable'}.items():
        ET.SubElement(props, name).text = value
    refs = ET.SubElement(project, 'ItemGroup')
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(refs, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / (name + '.dll'))
    ET.ElementTree(project).write(work / 'Reference.csproj', encoding='utf-8', xml_declaration=True)
    result = subprocess.run([dotnet, 'build', str(work / 'Reference.csproj'), '-o', str(work / 'app'),
                             '--nologo', '-v', 'quiet'], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    return work / 'app/Reference.dll'


def cases():
    i = lambda value: ['i', value]
    f = lambda value: ['f', value]
    s = lambda value: ['s', value]
    v = ['v', [1.25, -2.5, 3.75]]
    q = ['q', [0, 0, 3, 4]]
    base = [v, q, f(.15)]
    frame_lists = [[], base, base + [i(9)], base * 2, [v, f(.15)], [q, f(.15)],
                   [f(.15), f(.25)], [v, q], [v], [q], [i(1), q, f(.1)], [v, i(1), f(.1)]]
    options = [[], [i(1)], [i(2)], [i(19)], [i(19), i(0), i(2)],
               [i(0), i(0)], [i(0), i(1)], [i(0), i(2)], [i(0)],
               [i(0), i(0), i(0), i(2), i(0), i(1), i(0), i(0)]]
    options += [[i(1), i(mode)] for mode in [-1, 0, 1, 2, 3, 4, 99]]
    options += [[i(2), i(data)] for data in [-1, 0, 1, 2, 3, 4, 5, 7]]
    out = []
    def add(frames, opts, **extra):
        case = dict(physical=False, attachment=False, existing=True, frames=frames, options=opts, throws='')
        case.update(extra)
        out.append(case)
    for frames, opts, existing, physical, attachment in itertools.product(frame_lists, options, [False, True],
                                                                        [False, True], [False, True]):
        add(frames, opts, existing=existing, physical=physical, attachment=attachment)
    values = [i(0), i(2), f(1.75), f(1e300), s('1'), s('1.0'), s('bad'), v, q, ['l', []]]
    for first, second in itertools.product(values, repeat=2):
        add(base, [first, second])
        add([], [first, second])
    for duration in [i(1), f(0), f(.0019), f(-.5), f(2147484), f(-2147484), f(1e300),
                     s('0.125'), s('1,250.25'), s('bad'), v, q, ['l', []]]:
        for data, frames in [(3, [v, q, duration]), (2, [v, duration]), (1, [q, duration]),
                             (0, [duration, duration, duration])]:
            add(frames, [i(2), i(data)])
    for rotation in [[0, 0, 0, 0], [1, 2, 3, 4], [0, 0, 1e-30, 1e-30], [1e300, 0, 0, 0],
                     [.123456789123, -.987654321, .0000001, -.25]]:
        add([v, ['q', rotation], f(.1)], [])
    for throw in ['delete', 'clear', 'create', 'set', 'start', 'stop', 'pause']:
        add(base, [], throws=throw)
        add([], [i(0), i(0), i(0), i(2), i(0), i(1)], throws=throw)
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', required=True)
    parser.add_argument('--dotnet', required=True)
    parser.add_argument('--reference', type=Path)
    parser.add_argument('--work', type=Path, required=True)
    args = parser.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    reference = args.reference.resolve() if args.reference else build_reference(args.dotnet, work)
    inputs = cases()
    original = run([args.dotnet, str(reference)], inputs)
    native = run([args.native], inputs)
    differences = [dict(case=case, original=old, native=new)
                   for case, old, new in zip(inputs, original, native) if old != new]
    files = ['OpenSim/Region/ScriptEngine/Shared/Api/Implementation/LSL_Api.cs',
             'OpenSim/Region/ScriptEngine/Shared/LSL_Types.cs', 'bin/OpenMetaverseTypes.dll']
    report = dict(passed=not differences, cases=len(inputs), differences=differences[:3],
                  sourceHashes={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in files},
                  scope='Unchanged original API and list getter/LSL quaternion normalization methods, actual SDK float vectors/quaternions. Native LSL boxed values, invariant culture, controlled motion callbacks; timer/movement, scene binding, CLR raw/null items and serialization are not covered.')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, json.dumps(differences[:2], indent=2)
    print(f'PASS: {len(inputs)} original keyframed-motion command/list observations')


if __name__ == '__main__':
    main()
