
import collections,hashlib,json,pathlib,subprocess
root=pathlib.Path('/audit')
calls=json.loads((root/'script-api-method-review.json').read_text())
exe=root/'probe/parity_compile_probe_linux'
control=subprocess.run([str(exe),str(root/'probe/control.lsl')],capture_output=True,text=True,check=True)
assert '\taccepted' in control.stdout
for offset in range(0,len(calls),50):
    batch=calls[offset:offset+50]
    paths=[str(root/'probe/calls'/pathlib.Path(c['probe']).name) for c in batch]
    result=subprocess.run([str(exe),*paths],capture_output=True,text=True,check=True)
    lines=result.stdout.splitlines()
    assert len(lines)==len(batch)
    for call,line in zip(batch,lines):
        fields=line.split('\t')
        call['linuxCompile']=fields[1]
        call['linuxDiagnostics']=fields[2:]
result={'overloads':len(calls),'linuxCompile':dict(collections.Counter(c['linuxCompile'] for c in calls)),
        'platformDisagreements':[c['id'] for c in calls if c['windowsCompile']!=c['linuxCompile']],
        'linuxProbeSha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
        'linuxLinkedArchiveSha256':hashlib.sha256(pathlib.Path('/build/libopensim_server_core.a').read_bytes()).hexdigest(),
        'linuxLslSourceSha256':hashlib.sha256(pathlib.Path('/src/native-port/src/lsl_runtime.cpp').read_bytes()).hexdigest()}
(root/'script-api-method-review.json').write_text(json.dumps(calls,indent=2)+'\n')
(root/'linux-method-review-summary.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
