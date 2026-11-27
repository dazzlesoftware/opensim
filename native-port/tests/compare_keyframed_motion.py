"""Compare the original keyframe playback class with controlled scene, clock and timer boundaries."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import subprocess
import xml.etree.ElementTree as ET
from compare_presence_endpoint import ROOT, extract
from compare_inventory_folder_decode import run


def build_reference(dotnet, work):
    source = (ROOT / 'OpenSim/Region/Framework/Scenes/KeyframeMotion.cs').read_text(encoding='utf-8-sig')
    core = extract(source, r'public class KeyframeMotion\b')
    core = '[Serializable]\n' + core
    wrapper = Path(__file__).with_name('keyframed_motion_reference.cs').read_text().replace('/*SOURCE_CLASS*/', core)
    (work / 'Program.cs').write_text(wrapper)
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for name, value in {'TargetFramework': 'net8.0', 'OutputType': 'Exe', 'Nullable': 'disable',
                        'EnableUnsafeBinaryFormatterSerialization': 'true', 'NoWarn':'SYSLIB0011'}.items():
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
    def frame(p=None, r=None, ms=200):
        return dict(p=p or [], r=r or [], ms=ms)
    def op(name, **kw):
        return dict(op=name, **kw)
    tick = op('tick')
    sequences = [
        [op('start')] + [tick] * 12,
        [op('start'), tick, op('pause'), tick, op('start')] + [tick] * 8,
        [op('start'), tick, op('stop'), op('start')] + [tick] * 6,
        [op('start'), tick, op('suspend'), tick, op('resume'), tick, op('resume'), tick],
        [op('start'), tick, op('selected', value=True), op('external', position=[20, 25, 40]), tick,
         op('selected', value=False)] + [tick] * 6,
        [op('start'), tick, op('cross'), tick, op('fail'), op('tick', repeat=1199), tick, tick, tick, op('stop')],
    ]
    shapes = [[], [frame([10, 0, 0], [0, 0, 0, 1])],
              [frame([.1, .2, .3], [0, 0, .6, .8], 173), frame([-3, 2, .25], [0, .3, 0, .9539392], 91)],
              [frame(ms=75)], [frame([1, 2, 3], ms=150)], [frame(r=[0, 0, 1, 0], ms=250)],
              [frame([1, 0, 0], ms=0)], [frame([1, 0, 0], ms=-10)],
              [frame([1, 0, 0], ms=-2147483648)], [frame([1, 0, 0], ms=2147483647)]]
    # Exercise List<T> growth beyond the default four slots, stale value slots
    # after RemoveAt/Clear, and exact-size backing storage in Copy.
    shapes.extend([[frame([i + .25, -i, .5], ms=75) for i in range(n)] for n in [5, 9]])
    out = []
    def add(keys, actions, mode=0, **extra):
        value = dict(position=[12, 20, 30], rotation=[0, 0, 0, 1], frames=keys, actions=actions,
                     mode=mode, boundary=False, nullFrames=False)
        value.update(extra)
        out.append(value)
    for mode in [0, 1, 2, 3, 7]:
        for keys in shapes:
            for sequence in sequences:
                add(keys, sequence, mode)
    for dt in [0, 1, 25, 50, 75, 149, 150, 151, 500]:
        for duration in [25, 50, 75, 0, -50]:
            add(shapes[2], [op('start')] + [op('tick', dt=dt, duration=duration)] * 10)
    add([], [op('start'), tick, op('stop'), op('pause'), op('resume'), op('erase')], nullFrames=True)
    for mode in range(4):
        add(shapes[2], [op('start'), tick, op('set', null=True)] + [tick] * 10, mode)
        add(shapes[2], [op('start'), tick, op('update', null=True), tick, op('update'), tick, op('erase'), tick], mode)
        add([frame([20, 0, 0], [0, 0, .6, .8], 300)],
            [op('start')] + [tick] * 6 + [op('fail'), op('tick', repeat=1200), op('boundary', value=False)] + [tick] * 8,
            mode, position=[250, 20, 30], boundary=True)
        add(shapes[2], [op('start'), tick, op('set', frames=shapes[4])] + [tick] * 12, mode)
    rng = random.Random(1175)
    for _ in range(80):
        keys = [frame([rng.uniform(-5, 5) for _ in range(3)],
                      rng.choice([[0, 0, 0, 1], [0, 0, .6, .8], [0, .3, 0, .9539392]]), rng.randint(40, 600))
                for _ in range(rng.randint(1, 3))]
        actions = [op('start')] + [op('tick', dt=rng.choice([25, 50, 75, 200])) for _ in range(12)]
        add(keys, actions, rng.randrange(4), rotation=[0, 0, .6, .8])
    phases = [[], [op('start')], [op('start'), tick], [op('start'), tick, op('pause')],
              [op('start'), tick, op('stop')],
              [op('start'), tick, op('selected', value=True), op('external', position=[30, 35, 40])],
              [op('start'), tick, op('cross')],
              [op('start'), tick, op('cross'), op('fail'), op('tick', repeat=5)]]
    for mode in [0, 1, 2, 3, 7]:
        for index, phase in enumerate(phases):
            actions = phase + [op('copy', position=[100, 50, 40], rotation=[0, 0, 1, 0]),
                op('tick', target='copy'), op('update', target='copy'), op('tick', target='copy'), tick,
                op('start', target='copy'), op('tick', target='copy'), tick, op('pause', target='copy'),
                op('set', frames=shapes[4]), tick, op('erase'), op('tick', target='copy'),
                op('start', target='copy'), op('tick', target='copy'),
                op('set', target='copy', frames=shapes[1]), op('stop', target='copy'),
                op('start', target='copy'), op('tick', target='copy')]
            for keys in [shapes[2], []]:
                add(keys, actions, mode, data=[0, 1, 2, 3, 7, -1][index % 6])
        add([], [op('copy', position=[100, 50, 40]), op('update', target='copy'),
                 op('start', target='copy'), op('tick', target='copy'), op('erase')], mode, nullFrames=True)
    for mode in [0, 1, 2, 3, 7]:
        for phase in phases:
            for selected in [False, True]:
                add(shapes[2], phase + [op('serialize'), op('external', position=[30, 35, 40]),
                    op('restore', position=[100, 50, 40], selected=selected),
                    op('tick', target='copy'), op('update', target='copy'),
                    op('tick', target='copy'), op('selected', target='copy', value=False),
                    op('tick', target='copy'), op('serialize', target='copy'),
                    op('stop', target='copy'), op('start', target='copy'),
                    op('tick', target='copy')], mode)
        for phase in phases:
            add(shapes[2], phase + [op('restore', position=[100, 50, 40], selected=False, alias=True),
                op('update', target='copy'), op('tick', target='copy'), op('stop', target='copy'),
                op('start', target='copy')] + [op('tick', target='copy')] * 12 +
                [op('serialize', target='copy'), op('set', target='copy', frames=shapes[4]),
                 op('stop', target='copy'), op('start', target='copy'), op('tick', target='copy')], mode)
        add(shapes[2], [op('start'), tick, op('positionError', value=True), op('serialize'),
            op('positionError', value=False), op('start'), tick, op('serialize'),
            op('restore', position=[100, 50, 40], selected=False),
            op('update', target='copy'), op('start', target='copy'), op('tick', target='copy')], mode)
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
    differences = []
    for case, old, new in zip(inputs, original, native):
        if old != new:
            index = next((i for i, pair in enumerate(zip(old, new)) if pair[0] != pair[1]), -1)
            differences.append(dict(case=case, action=index, original=old[index], native=new[index]))
    files = ['OpenSim/Region/Framework/Scenes/KeyframeMotion.cs', 'bin/OpenMetaverseTypes.dll']
    report = dict(passed=not differences, cases=len(inputs), observations=sum(map(len, original)),
                  differences=differences[:2],
                  sourceHashes={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in files},
                  scope='Original class including Serialize/FromData, controlled scene properties, timestamp and timer registration. Compares playback/copy/serialization lifecycle, list backing/version/aliases, float bits and callback order under serialized calls. Fixture uses distinct empty input arrays; alias cases explicitly share the list backing. Namespace-controlled BinaryFormatter roundtrips; actual compiled-class wire compatibility has a separate codec oracle. Real timer concurrency, root property projections, live region integration and region restart remain separate.')
    (work / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    assert not differences, json.dumps(differences[:1], indent=2)
    print(f'PASS: {len(inputs)} original keyframe playback scenarios, {report["observations"]} state observations')


if __name__ == '__main__':
    main()
