"""Original full TerrainData/TerrainTaintsArray against native height/patch projection."""

import argparse, hashlib, json, random, struct, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def bits(v):
    return struct.pack('<f', v).hex()


def h(v):
    return f'{struct.unpack("<I",struct.pack("<f",v))[0]:x}'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    sources = ['OpenSim/Framework/TerrainData.cs', 'OpenSim/Framework/TerrainTaintsArray.cs']
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0',
        OutputType='Exe',
        AllowUnsafeBlocks='true',
        EnableDefaultCompileItems='false',
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [
        *[ROOT / s for s in sources],
        ROOT / 'native-port/tests/scene_world_terrain_reference.cs',
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    cs = w / 'SceneWorldTerrain.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = []
    rng = random.Random(29177)
    for x in [
        -100.0,
        -1.0,
        -0.0,
        0.0,
        0.25,
        0.5,
        0.75,
        1.0,
        15.25,
        30.75,
        31.0,
        31.25,
        32.0,
        100.0,
        float('inf'),
        float('-inf'),
        float('nan'),
    ]:
        for y in [
            -1.0,
            0.0,
            0.25,
            0.5,
            1.0,
            15.25,
            30.75,
            31.0,
            100.0,
            float('inf'),
            float('-inf'),
            float('nan'),
        ]:
            commands.append(f'h {h(x)} {h(y)}')
    for i in range(2000):
        commands.append(f'h {h(rng.uniform(-20,50))} {h(rng.uniform(-20,50))}')
    for px in range(2):
        for py in range(2):
            commands.append(f'm {px} {py}')
            for sub, mul in [(0.0, 1.0), (-3.5, 0.125), (17.0, -1.25)]:
                commands.append(f'p {px} {py} {h(sub)} {h(mul)}')
    commands += [
        'c',
        's 0 0 0',
        's 0 0 80000000',
        't 0 0 0',
        's 17 17 7fc00000',
        't 1 1 1',
        's 17 17 7fc00000',
        'n 0',
        'n 0',
        'a',
        'n -1',
        'n 4',
        'n 1',
        'n 2',
        'n 3',
        'n 0',
        'n 0',
        'c',
    ]
    for i in range(100):
        x = rng.randrange(32)
        y = rng.randrange(32)
        commands += [f's {x} {y} {h(rng.uniform(-10,10))}', f't {x//16} {y//16} {i%2}']
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/SceneWorldTerrain.dll')], [a.native, '--terrain-probe']]:
        r = subprocess.run(
            cmd,
            input='\n'.join(commands) + '\n',
            capture_output=True,
            text=True,
            encoding='utf-8',
            timeout=30,
        )
        assert r.returncode == 0, r.stdout[-1000:] + r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands), (
        len(rows[0]),
        len(rows[1]),
        len(commands),
    )
    diff = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    report = dict(
        passed=not diff,
        observations=len(commands),
        sourceHashes={s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest() for s in sources},
        scope='Whole original TerrainData/TerrainTaintsArray compile with controlled Constants/logger. Actual GetHeight triangular/edge/indexing behavior, patch block/minmax order and taints are compared. Storage codecs, terrain effects/modules, unsafe out-of-range patches, full runtime/concurrency and actual Unix execution remain pending.',
        differences=diff[:8],
    )
    (w / 'scene-world-terrain-results.json').write_text(
        json.dumps(report, indent=2) + '\n', encoding='utf-8'
    )
    assert not diff, json.dumps(diff[:3], indent=2)
    print(f'PASS: {len(commands)} original terrain height/patch/taint observations')


if __name__ == '__main__':
    main()
