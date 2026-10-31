"""Byte-for-byte baked texture serialization and container mutation parity."""
import random
import struct
from compare_texture_face import main

def cases():
    commands=[]
    # Force equal non-default overrides on distant faces, so the full encoder
    # must group masks while the baked encoder must emit separate records.
    for op in range(16):
        if op in [12,13]:
            continue
        for value in [0,0x3f800000]:
            commands.append(f'-1 {op} {value:x}')
            other=0x3f000000 if value else 0x3f800000
            for index in [0,7,8,9,20,40,44]:
                commands.append(f'{index} {op} {other:x}')
    for op in range(16):
        for value in [0.,-0.,.5,-.5,1.,-1.,3.1415927410125732,float('nan'),float('inf')]:
            bits=struct.unpack('>I',struct.pack('>f',value))[0]
            for index in [-1,0,8,9,10,11,19,20,21,40,41,42,43,44]:
                commands.append(f'{index} {op} {bits:x}')
    rng=random.Random(41015)
    for _ in range(512):
        commands.append(f'{rng.randrange(-1,45)} {rng.randrange(16)} {rng.getrandbits(32):x}')
    commands+=['-1 16 0','-1 17 0','44 0 3f800000']
    return commands

if __name__=='__main__':
    main('texture_entry',cases(),16)
