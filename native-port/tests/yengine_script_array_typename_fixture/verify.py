"""Validate unchanged original generated-array observations and migration wire."""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent
REPO = ROOT.parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


class Reader:
    def __init__(self, data):
        self.data = data
        self.position = 0
        self.references = []
        self.classes = []

    def take(self, size):
        assert 0 <= size <= len(self.data) - self.position
        value = self.data[self.position:self.position + size]
        self.position += size
        return value

    def number(self):
        return struct.unpack('<i', self.take(4))[0]

    def text(self):
        size = shift = 0
        while True:
            byte = self.take(1)[0]
            size |= (byte & 127) << shift
            if byte < 128:
                return self.take(size).decode('utf-8')
            shift += 7
            assert shift < 35

    def value(self):
        tag = self.take(1)[0]
        if tag == 0:
            return None
        if tag == 15:
            index = self.number()
            assert 0 <= index < len(self.references)
            return self.references[index]
        if tag in (3, 12):
            return self.number()
        if tag in (2, 10):
            return struct.unpack('<d', self.take(8))[0]
        if tag == 11:
            return struct.unpack('<f', self.take(4))[0]
        if tag == 16:
            return bool(self.take(1)[0])
        if tag == 17:
            return {'kind': 'instance'}
        if tag in (4, 7):
            return {'kind': 'lsl_string', 'value': self.value()}
        if tag in (6, 8):
            return struct.unpack('<' + 'd' * (4 if tag == 6 else 3), self.take(32 if tag == 6 else 24))
        if tag == 13:
            value = self.text()
            self.references.append(value)
            return value
        if tag == 9:
            node = {'kind': 'array', 'reference': len(self.references)}
            self.references.append(node)
            node['element_type'] = self.text()
            size = self.number()
            assert 0 <= size <= len(self.data) - self.position
            node['items'] = [self.value() for _ in range(size)]
            return node
        if tag == 5:
            node = {'kind': 'list', 'reference': len(self.references)}
            self.references.append(node)
            node['backing'] = self.value()
            return node
        if tag == 19:
            node = {'kind': 'script_class', 'reference': len(self.references)}
            self.references.append(node)
            self.classes.append(node)
            node['class_index'] = self.value()
            node['instance_arrays'] = [self.value() for _ in range(11)]
            return node
        raise AssertionError(('unreviewed original tag', tag, self.position - 1))

    def read(self):
        assert self.take(1) == bytes([12])
        reserved = self.take(1)[0]
        arguments = self.value()
        initialize = self.take(1)[0]
        state, event = self.number(), self.number()
        globals_ = [self.value() for _ in range(11)]
        frames = []
        while True:
            function = self.text()
            if not function:
                break
            assert function != '**newStateCode**'
            frames.append((function, self.number(), self.value()))
        assert self.position == len(self.data)
        return dict(reserved=reserved, event_arguments=arguments, initialize=initialize,
                    state=state, event=event, globals=globals_, frames=frames)


def scalar(stage, name):
    return stage['globals'][name]['value']


def class_values(stage):
    nodes = {}
    def visit(value):
        if isinstance(value, dict):
            if 'reference_id' in value and value.get('type') in ('script_class', 'clr_array'):
                nodes[value['reference_id']] = value
            for item in value.values():
                visit(item)
        elif isinstance(value, list):
            for item in value:
                visit(item)
    visit(stage)
    def resolve(value):
        return nodes[value['reference_id']] if value.get('type') in ('script_class_reference','array_reference') else value
    output = {}
    for name in ('input', 'alias'):
        value = resolve(stage['globals'][name])
        assert value['type'] == 'script_class' and value['script_type'] == 'integer[]'
        assert value['class_index'] == 0 and value['rank'] == 1
        assert value['element_type'] == 'integer'
        backing = resolve(value['objects'][0])
        assert backing['type'] == 'clr_array' and backing['element_type'] == 'System.Int32'
        output[name] = dict(reference=value['reference_id'], dimensions=value['integers'],
                            backing=[item['value'] for item in backing['items']])
    return output


def expected(stage, live=False, imported=False):
    result=dict(pre_same=1,pre_cross=1,pre_literal=0)
    resumed=stage in ('live_resume','final') or imported
    result.update(after=int(resumed),post_first=int(resumed and live),post_second=int(resumed and live),post_cross=int(resumed and live),post_literal=0)
    return result


def check_observation(row,stage,live=False,imported=False):
    observation=row[stage]
    for name,value in expected(stage,live,imported).items():
        assert scalar(observation,name)==value,(stage,name,scalar(observation,name),value)
    for name in ('first','second','third','literal'):assert observation['globals'][name]['value']=='integer[]'
    assert scalar(observation,'hash_first')==scalar(observation,'hash_second')
    if not imported:
        assert scalar(observation,'hash_fresh')==(scalar(observation,'hash_first') if stage in ('live_resume','final') else 0)


def main():
    report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,
                scope='Unchanged original generated-array xmrTypeName String reference identity, Windows/Linux x64',
                generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),platforms={},freshProcessImports={})
    for platform in ('windows','linux'):
        folder=ROOT/platform;rows=json.loads((folder/'metadata.json').read_text());assert len(rows)==1
        row=rows[0];assert row['accepted'] and row['suspended'] and row['migrated']
        for key in ('initial_exception','production_write_exception','binary_load_exception','queue_restore_exception','live_resume_exception','resume_exception'):assert row[key] is None,(key,row[key])
        for stage in ('initial','after_load','live_resume','final'):check_observation(row,stage,live=stage=='live_resume')
        case=folder/row['name']
        for name,digest in row['sha256'].items():
            path=case/name
            if path.is_file():assert sha(path)==digest,name
        assert row['sha256']['generator_source']==report['generatorSha256'] and row['sha256']['generator_assembly']==report['generatorAssemblySha256']
        parser=Reader((case/'snapshot.bin').read_bytes());decoded=parser.read()
        objects=decoded['globals'][5];assert objects['element_type']=='ob' and len(objects['items'])==4
        assert all(value=='integer[]' for value in objects['items'])
        assert all(value is objects['items'][0] for value in objects['items'])
        classes=decoded['globals'][9];assert classes['element_type']=='sc' and len(classes['items'])==2 and len(parser.classes)==2
        assert classes['items'][0] is not classes['items'][1]
        assert classes['items'][0]['class_index']==classes['items'][1]['class_index']==0
        assert classes['items'][0]['instance_arrays'][3]['items']==[1]
        assert classes['items'][1]['instance_arrays'][3]['items']==[2]
        report['platforms'][platform]=dict(runtime=row['runtime'],architecture=row['architecture'],processId=row['process_id'],marvinSeed=row['marvin_seed'],metadataSha256=sha(folder/'metadata.json'),snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,sourceSha256=sha(case/'source.lsl'),preSame=1,preCross=1,preLiteral=0,liveCachedEqual=1,migratedCachedEqual=0,hashFirst=scalar(row['initial'],'hash_first'),hashAfterMigration=scalar(row['final'],'hash_fresh'),wireEqualStringReferencesMerged=True,originalBackendSha256={k:v for k,v in row['sha256'].items() if k.startswith('original_')})
    for platform,folder in [('Windows','windows-imported'),('Linux','linux-imported-windows')]:
        rows=json.loads((ROOT/folder/'metadata.json').read_text());assert len(rows)==1
        row=rows[0];assert row['accepted'] and row['imported_capture'] and not row['suspended'] and not row['migrated'] and row['initial_exception'] is None
        check_observation(row,'initial',imported=True)
        baseline=json.loads((ROOT/'windows/metadata.json').read_text())[0]
        for name,value in baseline['final']['globals'].items():
            if name!='hash_fresh':assert row['initial']['globals'][name]==value,name
        report['freshProcessImports'][platform]=dict(metadataSha256=sha(ROOT/folder/'metadata.json'),cases=1,processId=row['process_id'],marvinSeed=row['marvin_seed'],retainedFirstHash=scalar(row['initial'],'hash_first'),freshHash=scalar(row['initial'],'hash_fresh'))
    report['measuredBehavior']={
        'live':'Repeated xmrTypeName for same wrapper and another wrapper of same generated class returns the same String reference; equal literal is a different reference.',
        'internalSleep':'Original live resume retains descriptor String reference identity; literal remains distinct.',
        'externalMigration':'Previously captured names and literal merge by content in migration dictionary; all compare unequal to fresh descriptor name after external resume.',
        'hashes':'String content hashes match within each process; fresh process uses its own Marvin seed. Retained scalar hashes remain captured values.',
    }
    report['boundaries']=['No claim for multiple simultaneously independent source instances or all script class kinds.','Windows/Linux snapshot hashes differ due process-specific captured Marvin hash integers; no byte identity claim.']
    report['verifierSha256']=sha(Path(__file__))
    out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n')
    print('PASS original type-name identity perOS +freshprocessimports:',sha(out))

if __name__=='__main__':main()
