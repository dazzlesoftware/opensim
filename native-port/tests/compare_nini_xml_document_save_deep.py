import argparse, pathlib, subprocess, json, hashlib

p = argparse.ArgumentParser()
p.add_argument('--native')
p.add_argument('--dotnet')
p.add_argument('--reference')
p.add_argument('--work', type=pathlib.Path)
a = p.parse_args()
a.work.mkdir(parents=True, exist_ok=True)
rows = [
    f'{mode} 1 0 ~ ' + (('<r>' * depth + 'v' + '</r>' * depth).encode('utf-16-be').hex())
    for depth in [1024, 16384, 65536]
    for mode in [0, 1]
]
payload = '\n'.join(rows) + '\n'
(a.work / 'inputs.txt').write_text(payload)
outputs = {}
for name, cmd in {'native': [a.native, '--probe'], 'managed': [a.dotnet, a.reference]}.items():
    r = subprocess.run(cmd, input=payload, text=True, capture_output=True)
    assert r.returncode == 0, (name, r.returncode, r.stderr)
    outputs[name] = r.stdout
    (a.work / (name + '.txt')).write_text(r.stdout)
assert outputs['native'] == outputs['managed']
report = {
    'cases': len(rows),
    'differences': 0,
    'depths': [1024, 16384, 65536],
    'outputSHA256': hashlib.sha256(outputs['native'].encode()).hexdigest(),
}
(a.work / 'results.json').write_text(json.dumps(report, indent=2))
print(report)
