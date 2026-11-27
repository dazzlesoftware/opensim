"""Stream genuine original DAG20 XML and verify every expanded node and leaf."""
import datetime
import hashlib
import json
from pathlib import Path
import struct
import xml.etree.ElementTree as ET


BASE = Path(__file__).resolve().parent


def digest(path):
    hash_value = hashlib.sha256()
    with path.open('rb') as input_file:
        for block in iter(lambda: input_file.read(1048576), b''):
            hash_value.update(block)
    return hash_value.hexdigest()


def main():
    expected_leaf_hash = hashlib.sha256(struct.pack('<i', 1) * 1048576).hexdigest()
    report = {
        'schemaVersion': 1,
        'checkedAtUtc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'passed': True,
        'scope': 'Unchanged original PostEvent and production XML helper save/reload of a20-level shared List DAG on Windows/Linux x64',
        'fullParityVerified': False,
        'sourceCompilationOrTypedEventDispatchClaimed': False,
        'expectedListOccurrences': 1048575,
        'expectedInt32Leaves': 1048576,
        'expectedLeafCellsSha256': expected_leaf_hash,
        'generatorSha256': digest(BASE / 'Reference.cs'),
        'generatorAssemblySha256': digest(BASE / 'bin/Release/net8.0/Reference.dll'),
        'platforms': {},
    }
    for platform in ['windows', 'linux']:
        metadata_path = BASE / platform / 'metadata.json'
        rows = json.loads(metadata_path.read_text())
        assert len(rows) == 1
        row = rows[0]
        assert row['postAccepted'] and row['queuedCount'] == row['restoredCount'] == 1
        assert row['generatorSha256'] == report['generatorSha256']
        for state in ['before', 'after']:
            value = row[state]
            assert value['listOccurrences'] == 1048575 and value['leaves'] == 1048576
            assert value['allLeavesOne'] and value['cellsSha256'] == expected_leaf_hash
        assert row['before']['uniqueLists'] == 20 and row['before']['rootChildrenAlias']
        assert row['after']['uniqueLists'] == 1048575 and not row['after']['rootChildrenAlias']
        xml_path = BASE / platform / row['name'] / 'event-queue.xml'
        assert xml_path.stat().st_size == row['xmlBytes'] == 61866020
        assert digest(xml_path) == row['xmlSha256']
        lists = leaves = events = 0
        actual_leaf_hash = hashlib.sha256()
        for _, node in ET.iterparse(xml_path, events=['end']):
            if node.tag in ['param', 'item']:
                kind = node.get('type')
                if kind == 'list':
                    lists += 1
                else:
                    assert kind == 'System.Int32' and node.text == '1'
                    leaves += 1
                    actual_leaf_hash.update(struct.pack('<i', int(node.text)))
            elif node.tag == 'Event':
                assert node.get('Name') == 'touch_start'
                events += 1
            node.clear()
        assert lists == 1048575 and leaves == 1048576 and events == 1
        assert actual_leaf_hash.hexdigest() == expected_leaf_hash
        report['platforms'][platform] = {
            'runtime': row['runtime'], 'architecture': row['architecture'],
            'metadataSha256': digest(metadata_path), 'xmlSha256': digest(xml_path),
            'xmlBytes': xml_path.stat().st_size,
            'originalUniqueListsBefore': row['before']['uniqueLists'],
            'originalUniqueListsAfter': row['after']['uniqueLists'],
            'everyXmlListAndLeafVerified': True,
            'leafCellsSha256': actual_leaf_hash.hexdigest(),
        }
    assert report['platforms']['windows']['xmlSha256'] == report['platforms']['linux']['xmlSha256']
    report['verifierSha256'] = digest(Path(__file__))
    destination = BASE / 'verification.json'
    assert not destination.exists()
    destination.write_text(json.dumps(report, indent=2, ensure_ascii=True) + '\n')
    print('PASS actual original DAG20 XML expansion:', digest(destination))


if __name__ == '__main__':
    main()
