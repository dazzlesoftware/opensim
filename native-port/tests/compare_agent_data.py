"""Complete original AgentData/CompleteAgentData with real appearance/SDK and controlled scene callbacks."""

import argparse, hashlib, itertools, json, subprocess, xml.etree.ElementTree as ET
from pathlib import Path
from compare_asset_model import ROOT, reference

MODEL_SOURCES = [
    "ChildAgentDataUpdate.cs",
    "Animation.cs",
    "AvatarAppearance.cs",
    "AvatarWearable.cs",
    "AvatarAttachment.cs",
    "WearableCacheItem.cs",
    "IAssetCache.cs",
    "EntityTransferContext.cs",
    "Culture.cs",
    "VersionInfo.cs",
]


def build_reference(dotnet, w, extra_files=(), startup="DataReference", references=()):
    w = Path(w).resolve()
    w.mkdir(parents=True, exist_ok=True)
    reference(dotnet, w / 'assets')
    project = ET.parse(w / 'assets/AssetModelReference.csproj')
    group = project.getroot().find('ItemGroup')
    for item in list(group):
        if item.tag == 'Compile' and Path(item.get('Include')).name == 'asset_model_reference.cs':
            group.remove(item)
    ET.SubElement(project.getroot().find('PropertyGroup'), 'StartupObject').text = startup
    appearance = w / 'AppearanceReference.cs'
    s = (ROOT / 'native-port/tests/avatar_appearance_reference.cs').read_text(encoding='utf-8')
    s = s.replace(
        'void ErrorFormat(string f,params object[] a);',
        'void ErrorFormat(string f,params object[] a);void WarnFormat(string f,params object[] a);',
    ).replace(
        'public void ErrorFormat(string f,params object[] a){}',
        'public void ErrorFormat(string f,params object[] a){}public void WarnFormat(string f,params object[] a){}',
    )
    s = s.replace(
        'void WarnFormat(string f,params object[] a);',
        'void WarnFormat(string f,params object[] a);void DebugFormat(string f,params object[] a);void Info(object o);',
    ).replace(
        'public void WarnFormat(string f,params object[] a){}',
        'public void WarnFormat(string f,params object[] a){}public void DebugFormat(string f,params object[] a){}public void Info(object o){}',
    )
    appearance.write_text(s, encoding='utf-8')
    names = [
        'ChildAgentDataUpdate.cs',
        'Animation.cs',
        'AvatarAppearance.cs',
        'AvatarWearable.cs',
        'AvatarAttachment.cs',
        'WearableCacheItem.cs',
        'IAssetCache.cs',
        'EntityTransferContext.cs',
        'Culture.cs',
        'VersionInfo.cs',
    ]
    for f in [
        appearance,
        ROOT / 'native-port/tests/agent_data_reference.cs',
        *[ROOT / 'OpenSim/Framework' / name for name in names],
    ]:
        ET.SubElement(group, 'Compile', Include=str(f))
    for name in ['OpenMetaverse', 'OpenMetaverse.StructuredData', *references]:
        ref = ET.SubElement(group, 'Reference', Include=name)
        ET.SubElement(ref, 'HintPath').text = str(ROOT / 'bin' / f'{name}.dll')
    for file in extra_files:
        ET.SubElement(group, 'Compile', Include=str(file))
    path = w / 'AgentData.csproj'
    project.write(path, encoding='utf-8', xml_declaration=True)
    r = subprocess.run(
        [
            dotnet,
            'build',
            str(path),
            '-c',
            'Release',
            '-o',
            str(w / 'app'),
            '--nologo',
            '-v',
            'quiet',
        ],
        capture_output=True,
        text=True,
    )
    if r.returncode:
        raise RuntimeError(r.stdout + r.stderr)
    return w / "app/AgentData.dll"


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    build_reference(a.dotnet, w)
    seven = '00000000-0000-0000-0000-000000000007'
    eight = seven[:-1] + '8'
    requests = [
        {},
        {
            'agent_uuid': seven,
            'region_id': eight,
            'circuit_code': '123456',
            'position': '<1, 2, 30>',
            'head_rotation': '<0, 0, 0, 1>',
            'body_rotation': '<0, 0, 0>',
            'wait_for_root': True,
            'far': 256.5,
            'packed_appearance': {},
            'god_data': {'ViewerUiIsGod': True},
        },
    ]
    numbers = [
        None,
        True,
        0,
        -1,
        1.5,
        'bad',
        '-0',
        '255',
        '256',
        '4294967295',
        '4294967296',
        '1\0',
        '1\0 ',
        {},
        [],
    ]
    for key in [
        'circuit_code',
        'locomotion_state',
        'control_flags',
        'agent_access',
        'crossingflags',
        'crossExtraFlags',
        'motion_state',
    ]:
        for value in numbers:
            requests.append({key: value})
    for key in [
        'agent_uuid',
        'session_uuid',
        'region_id',
        'prey_agent',
        'agent_cof',
        'active_group_id',
        'parent_part',
    ]:
        for value in [None, seven, seven.replace('-', ''), seven + 'tail', 'bad', True, {}, []]:
            requests.append({key: value})
    for key in [
        'position',
        'velocity',
        'center',
        'size',
        'at_axis',
        'left_axis',
        'up_axis',
        'sit_offset',
    ]:
        for value in [None, 'bad', '1,2,3', '<1.25, 2, 30>', 'prefix<1, 2, 3>tail']:
            requests.append({key: value})
    quaternions = [
        None,
        '',
        'bad',
        '<0,0,0>',
        '<1,2,3>',
        '<0.1, 0.2, 0.3>',
        '<0, 0, 0, 1>',
        '1,2,3,4',
        '<1,2,3,',
        '<1,2,3,>',
        '<NaN, 0, 0, 1>',
        'prefix<0.1, 0.2, 0.3, 0.4>tail',
    ]
    for key in ['head_rotation', 'body_rotation']:
        for value in quaternions:
            requests.append({key: value})
    for key in ['far', 'aspect', 'energy_level']:
        for value in [None, -1, 1.25, 'NaN', '∞', 'bad', {}, []]:
            requests.append({key: value})
    for key in ['wait_for_root', 'changed_grid', 'always_run']:
        for value in [None, True, False, 'false', '1', {}, []]:
            requests.append({key: value})
    for key in ['active_group_name', 'active_group_title', 'callback_uri', 'cb_uri']:
        for value in [None, '', '試験', 'uri/path', True, {}, []]:
            requests.append({key: value})
    for key, values in {
        'god_data': [None, {}, [], True, {'ViewerUiIsGod': True}, {'ViewerUiIsGod': 'false'}],
        'throttles': [None, [], [0, 1, 2, 255], True, 12.5, {}, 'AQID'],
        'children_seeds': [
            None,
            [],
            [3],
            [{}],
            [{'seed': 'missing'}],
            [{'handle': '3'}],
            [{'handle': '3', 'seed': 'first'}, {'handle': '3', 'seed': 'second'}],
            [{'handle': '3', 'seed': 'first'}, {'handle': '3'}],
            [{'handle': '18446744073709551615', 'seed': None}],
        ],
        'movementAO': [
            None,
            [],
            [3],
            [{}],
            [{'state': 'Walk', 'uuid': seven}],
            [{'state': 'Walk', 'uuid': seven}, {'state': 'Walk', 'uuid': eight}],
        ],
        'animations': [
            None,
            [],
            [3],
            [{}, 3, {}],
            [{'animation': seven, 'object_id': eight, 'seq_num': -1}],
        ],
        'default_animation': [None, {}, [], {'seq_num': 'bad'}],
        'animation_state': [None, {}, [], {'seq_num': 2147483648}],
        'controllers': [
            None,
            [],
            [3],
            [{}, 3, {}],
            [{'object': seven, 'item': eight, 'ignore': -1, 'event': 2147483648}],
        ],
        'packed_appearance': [None, {}, [], {'height': 0.5}, {'wearables': [3]}],
        'visual_params': [None, [], [1, 2, 3], {}],
        'texture_entry': [None, []],
        'wearables': [None, [], [[], []], [3, 3], [[], [], [], []]],
        'attachments': [None, [], [{}], [{'point': 3, 'item': seven, 'asset': eight}]],
        'cfonline': [None, [], [seven, 'bad', None, {}, []]],
    }.items():
        for value in values:
            requests.append({key: value})
    attachment = [
        {'sog': '<one/>', 'extra': 'extra-one', 'modified': True, 'state': 'state-one'},
        {'sog': '<two/>', 'extra': 'extra-two', 'modified': False, 'state': 'state-two'},
    ]
    requests += [{'attach_objects': value} for value in [None, [], [3], [{}], attachment]]
    cases = [
        {'profile': profile, 'data': value}
        for profile in [0, 1, 4, 5, 16, 17, 32, 33]
        for value in requests
    ]
    for profile, look in itertools.product(
        [0, 1, 32, 33],
        [
            '<0,0,0>',
            '<1,0,0>',
            '<3,4,5>',
            '<.01,.02,0>',
            '<1e-8,1e-8,0>',
            '<NaN, 1, 0>',
            '<∞, 1, 0>',
        ],
    ):
        cases.append({'profile': profile, 'data': {}, 'look': look})
    for profile, tag, value in itertools.product(
        [0, 1, 16, 32], ['null_map', 'null_context', 'null_overrides', 'skip_unpack'], [True]
    ):
        cases.append({'profile': profile, 'data': {}, tag: value})
    for profile in [0, 1, 16, 32]:
        cases.append({'profile': profile, 'data': {}, 'interface_id': seven})
    for failure, index in itertools.product(
        ['deserialize', 'extra-from', 'modified-set', 'xml', 'extra-to', 'modified-get'], [0, 1]
    ):
        cases.append(
            {
                'profile': 1,
                'data': {'attach_objects': attachment},
                'failure': failure,
                'fail_index': index,
            }
        )
    for tag in ['null_scene', 'scene_returns_null']:
        cases.append({'profile': 1, 'data': {'attach_objects': attachment}, tag: True})
    for mode in ['null', 'empty', 'null_object']:
        cases.append({'profile': 1, 'data': {}, 'states_mode': mode})
    for mutation in ['add', 'replace', 'set_same', 'agent']:
        cases.append({'profile': 1, 'data': {}, 'mutation': mutation})
    for version in [0.1, 0.3, 0.5, 0.8]:
        cases.append({'profile': 1, 'data': {}, 'version': version})
    commands = [
        json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode().hex()
        for value in cases
    ]
    rows = []
    for command in [[a.dotnet, str(w / 'app/AgentData.dll')], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=180
        )
        if r.returncode:
            raise RuntimeError(r.stdout[-2000:] + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [
        {'input': value, 'source': x, 'native': y} for value, x, y in zip(cases, *rows) if x != y
    ]
    (w / 'agent-data-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHashes': {
                    name: hashlib.sha256(
                        (ROOT / 'OpenSim/Framework' / name).read_bytes()
                    ).hexdigest()
                    for name in MODEL_SOURCES
                },
                'scope': 'Complete original AgentData/CompleteAgentData with actual appearance/animation/control/SDK; JSON pack bytes, raw state (appearance JSON MD5), dump, interface ID and controlled scene callback traces. Full scene serializers, CLR-null OSD slots, SDK collections/typed/runtime/math/platform semantics remain partial',
                'differences': diffs[:4],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        short = []
        for d in diffs[:4]:
            row = {'input': d['input']}
            for key in ['source', 'native']:
                parts = d[key].split('|')
                row[key] = (
                    [parts[0], parts[1]]
                    + [bytes.fromhex(v).decode('utf-8')[:600] for v in parts[2:6]]
                    + [parts[6]]
                )
            short.append(row)
        raise AssertionError(
            json.dumps(short, ensure_ascii=False, indent=2) + ' count=' + str(len(diffs))
        )
    print(f'PASS: {len(commands)} full agent-data source observations')


if __name__ == '__main__':
    main()
