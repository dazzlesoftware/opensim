"""Whole original local connector with actual Nini/agent DTOs and projected scene/object interfaces."""

import argparse, hashlib, itertools, json, subprocess
from pathlib import Path
from compare_agent_data import ROOT, build_reference


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--native', required=True)
    p.add_argument('--dotnet', required=True)
    p.add_argument('--work', required=True, type=Path)
    a = p.parse_args()
    w = a.work.resolve()
    w.mkdir(parents=True, exist_ok=True)
    source = (
        ROOT
        / 'OpenSim/Region/CoreModules/ServiceConnectorsOut/Simulation/LocalSimulationConnector.cs'
    )
    text = source.read_text(encoding='utf-8-sig')
    text = text.replace(
        'using GridRegion = OpenSim.Services.Interfaces.GridRegion;',
        'using GridRegion = OpenSim.Services.Interfaces.GridRegion;\nusing ISceneObject = global::LocalObject;',
    )
    wrapper = w / 'LocalModule.cs'
    wrapper.write_text(text, encoding='utf-8')
    assembly = build_reference(
        a.dotnet,
        w,
        [
            wrapper,
            ROOT / 'native-port/tests/local_simulation_reference.cs',
            ROOT / 'OpenSim/Framework/AgentCircuitData.cs',
        ],
        'LocalProbe',
        references=['Nini'],
    )
    init = lambda s: {'op': 'init', 'scene': s}
    remove = lambda s: {'op': 'remove', 'scene': s}
    activate = {'op': 'initialise', 'name': 'LocalSimulationConnectorModule'}
    inspect = [
        {'op': 'inner'},
        {'op': 'local_id', 'id': 1},
        {'op': 'local_id', 'id': 2},
        {'op': 'local_id', 'id': 3},
        {'op': 'get', 'id': 5},
    ]
    starts = [
        [],
        [init('A')],
        [init('A'), init('B')],
        [init('B'), init('A')],
        [init('A'), init('B'), remove('A'), init('C')],
        [activate, {'op': 'add', 'scene': 'A'}, {'op': 'add', 'scene': 'B'}],
    ]
    actions = []
    for method in [
        'create',
        'data',
        'position',
        'query',
        'release',
        'close_agent',
        'object',
        'get',
        'local_id',
    ]:
        for n in [0, 1, 2, 5]:
            actions.append({'op': method, 'id': n})
    for method in ['create', 'data', 'position', 'query', 'close_agent', 'object']:
        actions.append({'op': method, 'null_destination': True})
    for method in ['create', 'data', 'position']:
        actions.append({'op': method, 'null_data': True})
    actions += [
        {'op': 'create', 'source': True, 'null_context': True},
        {'op': 'query', 'null_context': True},
        {'op': 'query', 'null_features': True, 'home': 'https://home/', 'cross': True},
        {'op': 'close_agent', 'auth': 'token/試験'},
        {'op': 'object', 'local': True},
        {'op': 'object', 'local': True, 'null_object': True},
        {'op': 'object', 'null_object': True},
    ]
    actions += [{'op': 'local_handle', 'handle': h} for h in [0, 256, 512, 768, 999]]
    cases = [
        {'ops': start + [action] + inspect, 'deny': deny}
        for start, action, deny in itertools.product(starts, actions, [False, True])
    ]
    for scene, version, x, y in itertools.product(
        ['A', 'B'], [0, 0.299, 0.3, 0.8], [0, 256, 512], [256, 512]
    ):
        cases.append(
            {
                'ops': [
                    init('A'),
                    init('B'),
                    {'op': 'resize', 'scene': scene, 'x': x, 'y': y},
                    {'op': 'query', 'id': 1 if scene == 'A' else 2, 'version': version},
                ]
                + inspect
            }
        )
    for failure, method, extra in [
        ('register', 'add', {}),
        ('unregister', 'remove_region', {}),
        ('connect', 'create', {}),
        ('data', 'data', {}),
        ('position', 'position', {}),
        ('query', 'query', {}),
        ('entity', 'release', {}),
        ('arrival', 'release', {}),
        ('close', 'close_agent', {}),
        ('clone', 'object', {'local': True}),
        ('snapshot', 'object', {'local': True}),
        ('set-state', 'object', {'local': True}),
        ('object', 'object', {}),
    ]:
        for label in ['A', 'B']:
            cases.append(
                {
                    'ops': [
                        activate,
                        {'op': 'add', 'scene': 'A'},
                        {'op': 'add', 'scene': 'B'},
                        {'op': method, 'scene': label, 'id': 1 if label == 'A' else 2, **extra},
                    ]
                    + inspect,
                    'failure': failure,
                    'fail_scene': label,
                }
            )
    for mutation in ['add', 'remove_self', 'remove_other', 'duplicate', 'remove_all']:
        cases.append(
            {
                'ops': [init('A'), init('B'), {'op': 'position'}, {'op': 'position'}] + inspect,
                'mutation': mutation,
            }
        )
    for flag, action in [
        ('mutate_data', {'op': 'position'}),
        ('reset_data', {'op': 'position'}),
        ('null_transfer', {'op': 'release'}),
        ('null_clone', {'op': 'object', 'local': True}),
        ('null_snapshot', {'op': 'object', 'local': True}),
    ]:
        cases.append({'ops': [init('A'), init('B'), action] + inspect, flag: True})
    lifecycle = [
        {'op': 'add', 'scene': 'A'},
        {'op': 'remove_region', 'scene': 'A'},
        activate,
        {'op': 'add', 'scene': 'A'},
        {'op': 'add', 'scene': 'D'},
        {'op': 'close'},
        {'op': 'initialise', 'name': 'other'},
        {'op': 'remove_region', 'scene': 'D'},
        {'op': 'add', 'scene': 'B'},
        {'op': 'post'},
        {'op': 'loaded'},
        {'op': 'service_init'},
    ]
    cases += [
        {'ops': lifecycle + inspect},
        {
            'ops': [
                {'op': 'initialise', 'null_config': True},
                {'op': 'init'},
                {'op': 'remove'},
                {'op': 'add'},
                {'op': 'remove_region'},
                activate,
                {'op': 'add'},
                {'op': 'remove_region'},
            ]
            + inspect
        },
        {
            'ops': [
                init('A'),
                init('B'),
                {'op': 'rekey', 'scene': 'A', 'id': 2},
                remove('A'),
                {'op': 'position'},
            ]
            + inspect
        },
    ]
    for name in [
        '',
        None,
        'other',
        'localsimulationconnectormodule',
        ' LocalSimulationConnectorModule ',
    ]:
        cases.append(
            {'ops': [{'op': 'initialise', 'name': name}, {'op': 'add', 'scene': 'A'}] + inspect}
        )
    commands = [
        json.dumps(value, separators=(',', ':'), ensure_ascii=False).encode().hex()
        for value in cases
    ]
    rows = []
    for command in [[a.dotnet, str(assembly)], [a.native]]:
        r = subprocess.run(
            command, input='\n'.join(commands) + '\n', capture_output=True, text=True, timeout=180
        )
        if r.returncode:
            raise RuntimeError(r.stdout[-1000:] + r.stderr)
        rows.append(r.stdout.splitlines())
    assert len(rows[0]) == len(rows[1]) == len(commands)
    diffs = [{'input': c, 'source': x, 'native': y} for c, x, y in zip(cases, *rows) if x != y]
    (w / 'local-simulation-parity-results.json').write_text(
        json.dumps(
            {
                'passed': not diffs,
                'observations': len(commands),
                'sourceHash': hashlib.sha256(source.read_bytes()).hexdigest(),
                'scope': 'Entire original LocalSimulationConnectorModule with actual Nini and DTO dependencies; projected scene/module/logger/object interfaces (object parameter alias); callbacks, registry ordering/mutation, lifecycle and config state. Full concrete scene/object/plugin/logging/runtime/platform semantics remain pending',
                'differences': diffs[:5],
            },
            indent=2,
        ),
        encoding='utf-8',
    )
    if diffs:
        for d in diffs[:3]:
            for key in ['source', 'native']:
                parts = d[key].split('|')
                parts[1] = bytes.fromhex(parts[1]).decode()
                parts[2] = bytes.fromhex(parts[2]).decode()
                d[key] = parts
        raise AssertionError(
            json.dumps(diffs[:3], ensure_ascii=False, indent=2) + ' count=' + str(len(diffs))
        )
    print(f'PASS: {len(commands)} local simulation connector source observations')


if __name__ == '__main__':
    main()
