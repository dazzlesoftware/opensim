"""Actual AddNewMovement, TargetVelocity and ignored-control methods with SDK math."""

import argparse, hashlib, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def extract(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if text[end] == '{':
            depth += 1
        if text[end] == '}':
            depth -= 1
        end += 1
    return text[start:end]


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', type=Path, required=True)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = ROOT / 'OpenSim/Region/Framework/Scenes/ScenePresence.cs'
    text = source.read_text(encoding='utf-8')
    body = '\n'.join(
        extract(text, s)
        for s in [
            'public void AddNewMovement(Vector3 vec, float thisAddSpeedModifier, bool breaking = false)',
            'public Vector3 TargetVelocity',
            'internal static ACFlags RemoveIgnoredControls(ACFlags flags, ScriptControlled ignored)',
        ]
    )
    fragment_start = text.index(
        'Vector3 agent_control_v3 = Vector3.Zero;', text.index('public void HandleAgentUpdate(')
    )
    fragment_end = text.index('bool newFlying;', fragment_start)
    directions = extract(text, 'private static readonly Vector3[] Dir_Vectors =') + ';'
    body += (
        '\n'
        + directions
        + '\nconst uint CONTROL_FLAG_NUDGE_MASK=0x3fu<<19; const float AgentControlNormalVel=1f*4.096f, AgentControlMidVel=.6f*4.096f;\nstatic string ControlVector(uint input,uint ignored,bool m_setAlwaysRun,uint previous){var flags=RemoveIgnoredControls((ACFlags)input,(ScriptControlled)ignored);uint MovementFlags=previous;bool update_movementflag=false,DCFlagKeyPressed=false,mvToTarget=true;\n'
        + text[fragment_start:fragment_end]
        + '\nreturn BitConverter.SingleToUInt32Bits(agent_control_v3.X).ToString("x")+","+BitConverter.SingleToUInt32Bits(agent_control_v3.Y).ToString("x")+","+BitConverter.SingleToUInt32Bits(agent_control_v3.Z).ToString("x")+","+BitConverter.SingleToUInt32Bits(agent_velocity).ToString("x")+","+((uint)flags).ToString("x")+","+MovementFlags.ToString("x")+","+(update_movementflag?1:0)+","+(DCFlagKeyPressed?1:0); }\n'
    )
    method = w / 'ExtractedMovement.cs'
    method.write_text(
        'using System;using OpenMetaverse;using ACFlags=OpenMetaverse.AgentManager.ControlFlags;\n'
        + extract(text, 'enum ScriptControlled : uint')
        + '\npartial class MovementReference {\n'
        + body
        + '\n}',
        encoding='utf-8',
    )
    project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = ET.SubElement(project, 'PropertyGroup')
    for k, v in dict(
        TargetFramework='net8.0', OutputType='Exe', EnableDefaultCompileItems='false'
    ).items():
        ET.SubElement(props, k).text = v
    group = ET.SubElement(project, 'ItemGroup')
    for f in [method, ROOT / 'native-port/tests/movement_reference.cs']:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverseTypes']:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    cs = w / 'MovementReference.csproj'
    ET.ElementTree(project).write(cs, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [a.dotnet, 'build', str(cs), '-o', str(w / 'app'), '--nologo', '-v', 'quiet'],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr
    commands = [f'{seed} {mode}' for seed in [-5, 0, 1, 11, 17, 31] for mode in range(4096)]
    rows = []
    for cmd in [[a.dotnet, str(w / 'app/MovementReference.dll')], [a.native, '--probe']]:
        r = subprocess.run(
            cmd, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=60
        )
        assert r.returncode == 0, r.stderr
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diff = [dict(command=c, source=s, native=n) for c, s, n in zip(commands, *rows) if s != n]
    files = [source, ROOT / 'bin/OpenMetaverseTypes.dll']
    report = dict(
        passed=not diff,
        cases=len(commands),
        sourceHashes={
            str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files
        },
        scope='Complete original AddNewMovement/TargetVelocity/RemoveIgnoredControls, actual SDK rotation and CPU-dispatched normalization, controlled actor/animator state plus original HandleAgentUpdate direction/speed fragment. Grounded/flying/NPC, collision/landing, move-target/normal scaling, falling/braking, no actor, zero vectors and ignored masks. Full HandleAgentUpdate/control/animation/physics lifecycle and actual Unix execution remain pending.',
        differences=diff[:5],
    )
    (w / 'movement-results.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    assert not diff, json.dumps(diff[:5], indent=2)
    print(f'PASS: {len(commands)} original movement and control projections')


if __name__ == '__main__':
    main()
