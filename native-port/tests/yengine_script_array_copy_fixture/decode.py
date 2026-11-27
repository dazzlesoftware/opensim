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
        if tag == 20:
            lead = self.take(1)
            size = 1 if lead[0] < 0x80 else 2 if lead[0] < 0xe0 else 3
            assert lead[0] < 0xf0
            return {'kind': 'char', 'value': ord((lead + self.take(size - 1)).decode('utf-8'))}
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


class ObjectReader:
    def __init__(self,data):self.data=data;self.p=0
    def take(self,n):assert 0<=n<=len(self.data)-self.p;v=self.data[self.p:self.p+n];self.p+=n;return v
    def n(self):return struct.unpack('<i',self.take(4))[0]
    def t(self):
        n=shift=0
        while True:
            x=self.take(1)[0];n|=(x&127)<<shift;shift+=7
            if x<128:return self.take(n).decode()
            assert shift<=35
    def typ(self):
        location=[self.t(),self.n(),self.n()];kind=self.take(1)[0];index=self.n();out=dict(kind=kind,index=index,location=location)
        if kind==0:
            out['sizes']=[self.n() for _ in range(11)];out['virtual']=self.n();out['base']=self.t();out['rank']=self.n();out['element']=self.t() if out['rank'] else None
            while True:
                slot=self.n()
                if slot<0:break
                self.t();self.t()
            for _ in range(self.n()):
                self.t()
                for _ in range(self.n()):self.n();self.t();self.t()
        elif kind==1:
            self.t()
            for _ in range(self.n()):self.t()
        else:assert kind in (2,3)
        return out
    def read(self):
        assert self.take(11)==b'YObjectCode';assert self.n()==-11;self.source_hash=self.t();self.sizes=[self.n() for _ in range(11)]
        self.states=[self.t() for _ in range(self.n())];self.globals=[]
        while True:
            name=self.t()
            if not name:break
            self.globals.append([name,self.t(),self.n()])
        self.types={}
        while True:
            name=self.t()
            if not name:break
            self.types[name]=self.typ()
        self.methods={};method=None;source_file=''
        while True:
            kind=self.take(1)[0]
            if kind==2:assert method is None and self.p==len(self.data);return self
            if kind==5:
                name=self.t();result=self.t();args=[[self.t(),self.t()] for _ in range(self.n())];self.methods[name]=dict(result=result,args=args,locals={},records=[]);continue
            if kind==0:method=self.methods[self.t()];continue
            if kind==1:method=None;continue
            assert method is not None
            r=dict(kind=kind)
            if kind==3:r['number']=self.n();r['text']=self.t()
            elif kind==4:
                n=self.n();name=self.t();typ=self.t();method['locals'][n]=dict(name=name,type=typ)
            elif kind==6:r['number']=self.n()
            elif kind==21:r['text']=self.t()
            elif kind in (20,22,23):pass
            elif 7<=kind<=19:
                f=self.t();source_file=f or source_file;r['location']=[source_file,self.n(),self.n()];r['op']=struct.unpack('<h',self.take(2))[0]&65535
                if kind==8:r['strings']=[self.t(),self.t()]
                elif kind in (9,11):r['number']=self.n()
                elif kind in (10,13,18):r['strings']=[self.t()]
                elif kind==12:r['strings']=[self.t(),self.t()]+[self.t() for _ in range(self.n())]
                elif kind==14:r['strings']=[self.t()]+[self.t() for _ in range(self.n())]
                elif kind==15:self.take(8)
                elif kind==16:self.take(4)
                elif kind==17:r['number']=self.n()
                elif kind==19:r['labels']=[self.n() for _ in range(self.n())]
            else:raise AssertionError(kind)
            method['records'].append(r)


def constructor_coordinates(path,source):
    code=ObjectReader(path.read_bytes()).read();method=code.methods['default state_entry'];records=method['records']
    constructors=[]
    for i,r in enumerate(records):
        if r.get('strings')==['integer[].$new(integer)']:
            following=next(x for x in records[i+1:] if 'op' in x)
            assert following['op'] in (0xfe0e,0x13) and following['kind']==9
            local=method['locals'][following['number']];assert local['type']=='sdtclobj' and local['name'].startswith('tmp$')
            location=following['location'];lines=source.splitlines();line=lines[location[1]]
            assert line[location[2]]=='(',location
            new_column=line.index('new integer[]')
            assert location[2]>new_column
            constructors.append(dict(localNumber=following['number'],localName=local['name'],portableLocation=location,nativeNewTokenLocation=[location[0],location[1],new_column],callLocation=r['location']))
    assert len(constructors)==1
    return constructors[0]
