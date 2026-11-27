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


def validate(row):
    assert row['accepted'] and row['suspended'] and row['migrated']
    for key in ('initial_exception','production_write_exception','binary_load_exception',
                'queue_restore_exception','live_resume_exception','resume_exception'):
        assert row[key] is None, (row['name'], key, row[key])
    name = row['name']
    for stage_name in ('initial','after_load','live_resume','final'):
        stage=row[stage_name]
        resumed=stage_name in ('live_resume','final')
        classes=class_values(stage)
        assert scalar(stage,'after') == int(resumed)
        if name == 'element_compound_order':
            for key,value in [('index_count',1),('right_count',1),('order',21),('result_input',18),('result_alias',18)]:
                assert scalar(stage,key)==value,(name,stage_name,key)
            assert classes['input']==classes['alias']
            assert classes['input']['backing']==[18]
        elif name == 'element_compound_right_yield_rebind':
            for key,value in [('index_count',int(resumed)),('right_count',1),('order',21 if resumed else 2),
                              ('result_input',107 if resumed else 0),('result_alias',11 if resumed else 0)]:
                assert scalar(stage,key)==value,(name,stage_name,key)
            assert classes['input']['reference']!=classes['alias']['reference']
            assert classes['input']['backing']==([107] if resumed else [100])
            assert classes['alias']['backing']==[11]
        elif name == 'element_index_postfix':
            for key,value in [('cursor',1),('old',11),('result_input',12),('result_alias',23),('index_count',0),('right_count',0),('order',0)]:
                assert scalar(stage,key)==value,(name,stage_name,key)
            assert classes['input']==classes['alias']
            assert classes['input']['backing']==[12,23]
        else:
            assert name=='member_compound_right_yield_rebind'
            for key,value in [('index_count',0),('right_count',1),('order',2),('result_input',14 if resumed else 0),('result_alias',3 if resumed else 0)]:
                assert scalar(stage,key)==value,(name,stage_name,key)
            assert classes['input']['reference']!=classes['alias']['reference']
            assert classes['input']['dimensions']==([14] if resumed else [10])
            assert classes['alias']['dimensions']==[3]
            assert classes['input']['backing']==[0,0] and classes['alias']['backing']==[0,0,0]


def main():
    report=dict(schemaVersion=1,passed=True,fullParityVerified=False,nativeParityVerified=False,
                scope='Actual original generated-array assignment evaluation order and suspension/rebinding, Windows/Linux x64',
                generatorSha256=sha(ROOT/'Reference.cs'),generatorAssemblySha256=sha(ROOT/'bin/Release/net8.0/Reference.dll'),
                platforms={},freshProcessImports={})
    baseline={}
    for platform in ('windows','linux'):
        rows=json.loads((ROOT/(platform+'-frozen-observations')/'metadata.json').read_text());assert len(rows)==4
        cases=[]
        for row in rows:
            validate(row)
            case=ROOT/(platform+'-frozen-observations')/row['name']
            for name,digest in row['sha256'].items():
                path=case/name
                if path.is_file():assert sha(path)==digest,(platform,name)
            assert row['sha256']['generator_source']==report['generatorSha256']
            assert row['sha256']['generator_assembly']==report['generatorAssemblySha256']
            parser=Reader((case/'snapshot.bin').read_bytes());decoded=parser.read()
            classes=decoded['globals'][9];assert classes['element_type']=='sc' and len(classes['items'])==2
            expected=class_values(row['initial'])
            for index,name in enumerate(('input','alias')):
                node=classes['items'][index];assert node['kind']=='script_class' and node['class_index']==0
                assert node['instance_arrays'][3]['items']==expected[name]['dimensions']
                raw=node['instance_arrays'][5]['items'][0];assert raw['element_type']=='in'
                assert raw['items']==expected[name]['backing']
            assert (classes['items'][0] is classes['items'][1]) == (expected['input']['reference']==expected['alias']['reference'])
            expected_count=2 if 'yield_rebind' in row['name'] else 1
            assert len(parser.classes)==expected_count
            if platform=='windows':baseline[row['name']]=row
            else:
                assert (case/'snapshot.bin').read_bytes()==(ROOT/'windows-frozen-observations'/row['name']/'snapshot.bin').read_bytes()
                for stage in ('initial','after_load','live_resume','final'):
                    assert row[stage]['globals']==baseline[row['name']][stage]['globals']
            cases.append(dict(name=row['name'],sourceSha256=sha(case/'source.lsl'),metadataSha256=sha(case/'metadata.json'),
                              snapshotSha256=sha(case/'snapshot.bin'),snapshotBytes=(case/'snapshot.bin').stat().st_size,
                              independentScriptClassDefinitions=len(parser.classes),initialClasses=expected,
                              finalClasses=class_values(row['final'])))
        report['platforms'][platform]=dict(runtime=rows[0]['runtime'],architecture=rows[0]['architecture'],metadataSha256=sha(ROOT/(platform+'-frozen-observations')/'metadata.json'),cases=cases)
    for platform,folder in [('Windows','windows-imported-frozen-observations'),('Linux','linux-imported-windows-frozen-observations')]:
        rows=json.loads((ROOT/folder/'metadata.json').read_text());assert len(rows)==4
        for row in rows:
            assert row['accepted'] and row['imported_capture'] and not row['suspended'] and not row['migrated']
            assert row['initial_exception'] is None
            assert row['initial']['globals']==baseline[row['name']]['final']['globals']
        report['freshProcessImports'][platform]=dict(metadataSha256=sha(ROOT/folder/'metadata.json'),cases=4,processId=rows[0]['process_id'])
    report['measuredBehavior']={
        'compoundElementOrder':'Right() then Index(), each once; order21',
        'rhsSleepRebinding':'Target is resolved after RHS resumes: new input107, old alias11',
        'memberSleepRebinding':'Target field is on new wrapper after RHS resumes: new len0=14, old alias len0=3',
        'postfixIndex':'cursor increments once; old11 returned and element12 stored',
        'captureDuringRight':'index_count0/right_count1/order2, then resumed index_count1/order21',
    }
    report['crossPlatformSnapshotsIdentical']=True
    report['observationCorrection']='Script-class integer lane is copied with ToArray before observation retention; earlier aliasing observations/source+assembly preserved, production backend unchanged.'
    report['executedOutputFolders']={'Windows':'windows-frozen-observations','Linux':'linux-frozen-observations','WindowsImported':'windows-imported-frozen-observations','LinuxImported':'linux-imported-windows-frozen-observations'}
    report['verifierSha256']=sha(Path(__file__))
    out=ROOT/'verification.json';assert not out.exists();out.write_text(json.dumps(report,indent=2,ensure_ascii=True)+'\n')
    print('PASS original array-lvalue4cases perOS, raw class/backing/alias parse, fresh imports:',sha(out))


if __name__=='__main__':
    main()
