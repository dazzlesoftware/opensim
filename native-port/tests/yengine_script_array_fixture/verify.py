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


def wrapper(stage):
    result = stage['globals']['input']
    assert result['type'] == 'script_class'
    assert result['script_type'] == 'integer[]' and result['class_index'] == 0
    assert result['rank'] == 1 and result['element_type'] == 'integer'
    return result


def validate_case(row):
    assert row['accepted'] and row['suspended'] and row['migrated']
    assert row['initial_exception'] is None and row['production_write_exception'] is None
    assert row['binary_load_exception'] is None and row['queue_restore_exception'] is None
    name = row['name']
    for stage_name in ('initial', 'after_load', 'live_resume', 'final'):
        stage = row[stage_name]
        current = wrapper(stage)
        assert current['clr'] == 'OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj'
        assert len(current['objects']) == 1
        if name == 'rank1_members':
            assert current['integers'] == [3]
            assert [item['value'] for item in current['objects'][0]['items']] == [0, 7, 9]
            for key, value in [('default_cell', 0), ('length', 3), ('dimension', 3),
                               ('invalid_dimension', 0), ('flat_index', 2), ('observed', 16)]:
                assert scalar(stage, key) == value
        elif name == 'rank1_identity':
            assert current['integers'] == [2]
            assert [item['value'] for item in current['objects'][0]['items']] == [31, 0]
            for key in ('alias', 'restored', 'wrapped'):
                assert stage['globals'][key]['reference_id'] == current['reference_id']
            assert scalar(stage, 'same_alias') == scalar(stage, 'same_cast') == 1
            assert scalar(stage, 'type_name') == 'integer[]'
            assert scalar(stage, 'text') == 'OpenSim.Region.ScriptEngine.Yengine.XMRSDTypeClObj'
        elif name == 'rank1_public_dimensions':
            assert current['integers'] == [1]
            assert [item['value'] for item in current['objects'][0]['items']] == [0, 0, 11]
            assert scalar(stage, 'length') == scalar(stage, 'dimension') == 1
            assert scalar(stage, 'outside_dimension') == 11 and scalar(stage, 'flat_index') == 2
        else:
            assert name == 'rank1_zero' and current['integers'] == [0]
            assert scalar(stage, 'before_wrapper') == scalar(stage, 'before_backing') == 1
            assert scalar(stage, 'length') == 0
            backing = current['objects'][0]
            if stage_name in ('after_load', 'final'):
                assert backing['type'] == 'null'
            else:
                assert backing['type'] == 'clr_array' and backing['length'] == 0
        if stage_name in ('live_resume', 'final'):
            expected_after = {'rank1_members': 16, 'rank1_identity': 1,
                              'rank1_public_dimensions': 11, 'rank1_zero': 1}[name]
            assert scalar(stage, 'after') == expected_after
            if name == 'rank1_zero':
                assert scalar(stage, 'after_wrapper') == 1
                assert scalar(stage, 'after_backing') == (stage_name == 'live_resume')
    for error_key in ('resume_exception', 'live_resume_exception'):
        error = row[error_key]
        if name == 'rank1_public_dimensions':
            assert error['type'] == 'System.IndexOutOfRangeException'
            assert error['message'] == 'Index was outside the bounds of the array.'
        else:
            assert error is None


def main():
    report = dict(schemaVersion=1, passed=True, fullParityVerified=False,
                  nativeParityVerified=False,
                  scope='Actual unchanged original rank1 generated integer-array compilation, production binary capture/import, live/migrated resume and fresh-process imports',
                  generatorSha256=sha(ROOT / 'Reference.cs'),
                  generatorAssemblySha256=sha(ROOT / 'bin/Release/net8.0/Reference.dll'),
                  platforms={}, freshProcessImports={})
    baseline = {}
    for platform in ('windows', 'linux'):
        rows = json.loads((ROOT / platform / 'metadata.json').read_text())
        assert len(rows) == 4
        cases = []
        for row in rows:
            validate_case(row)
            case = ROOT / platform / row['name']
            for filename, expected in row['sha256'].items():
                path = case / filename
                if path.is_file():
                    assert sha(path) == expected, (platform, filename)
            assert row['sha256']['generator_source'] == report['generatorSha256']
            assert row['sha256']['generator_assembly'] == report['generatorAssemblySha256']
            binary = (case / 'snapshot.bin').read_bytes()
            parser = Reader(binary)
            decoded = parser.read()
            assert len(parser.classes) == 1
            node = parser.classes[0]
            assert node['class_index'] == 0
            assert decoded['globals'][9]['element_type'] == 'sc'
            assert all(item is node for item in decoded['globals'][9]['items'])
            dimensions = node['instance_arrays'][3]['items']
            backing = node['instance_arrays'][5]['items'][0]
            assert dimensions == wrapper(row['initial'])['integers']
            assert backing['element_type'] == 'in'
            assert backing['items'] == [x['value'] for x in wrapper(row['initial'])['objects'][0]['items']]
            if row['name'] == 'rank1_identity':
                assert decoded['globals'][5]['items'][0] is node
            if platform == 'windows':
                baseline[row['name']] = row
            else:
                original = baseline[row['name']]
                assert binary == (ROOT / 'windows' / row['name'] / 'snapshot.bin').read_bytes()
                for stage in ('initial', 'after_load', 'live_resume', 'final'):
                    assert row[stage]['globals'] == original[stage]['globals']
            cases.append(dict(name=row['name'], sourceSha256=sha(case / 'source.lsl'),
                              metadataSha256=sha(case / 'metadata.json'), snapshotSha256=sha(case / 'snapshot.bin'),
                              snapshotBytes=len(binary), independentScriptClassDefinitions=1,
                              dimensions=dimensions, backingCells=backing['items']))
        report['platforms'][platform] = dict(runtime=rows[0]['runtime'], architecture=rows[0]['architecture'],
                                            metadataSha256=sha(ROOT / platform / 'metadata.json'), cases=cases)
    for platform, folder in [('Windows', 'windows-imported'), ('Linux', 'linux-imported-windows')]:
        rows = json.loads((ROOT / folder / 'metadata.json').read_text())
        assert len(rows) == 4
        for row in rows:
            assert row['accepted'] and row['imported_capture'] and not row['suspended'] and not row['migrated']
            assert row['initial']['globals'] == baseline[row['name']]['final']['globals']
            error = row['initial_exception']
            if row['name'] == 'rank1_public_dimensions':
                assert error['type'] == 'System.IndexOutOfRangeException'
            else:
                assert error is None
        report['freshProcessImports'][platform] = dict(metadataSha256=sha(ROOT / folder / 'metadata.json'),
                                                       cases=4, distinctProcessId=rows[0]['process_id'])
    report['crossPlatformSnapshotsIdentical'] = True
    report['zeroLengthBackingAfterMigration'] = None
    report['zeroLengthWrapperAfterMigration'] = 'nonnull integer[] class'
    report['publicDimensionMutationDoesNotResizeBacking'] = True
    report['verifierSha256'] = sha(Path(__file__))
    out = ROOT / 'verification.json'
    assert not out.exists()
    out.write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n')
    print('PASS four original generated-array cases perOS plus four fresh-process imports perOS:', sha(out))


if __name__ == '__main__':
    main()
