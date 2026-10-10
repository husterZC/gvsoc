#!/usr/bin/env python3
"""Check every narrow-format midpoint and its adjacent FP32 values in RTL/C++."""
import json
import math
from pathlib import Path
import random
import struct
import subprocess
from reference import encode, representable

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
WORK=ROOT/'build/mxcore_outputs'


def main():
    vectors=WORK/'casts.txt'
    count=0
    with vectors.open('w') as out:
        for fmt,eb,mb in ((2,8,7),(3,5,10),(4,4,3),(5,5,2)):
            values=representable(eb,mb)
            candidates={0,0x7f800000,0x7fc00000,0x7fa00000,0x007fffff,0x00800000}
            midpoints=[(a+b)/2 for a,b in zip(values,values[1:])]
            midpoints.append(values[-1]+(values[-1]-values[-2])/2)
            for midpoint in midpoints:
                bits=struct.unpack('<I',struct.pack('<f',midpoint))[0]
                candidates.update((max(0,bits-1),bits,bits+1))
            rng=random.Random(42)
            candidates.update(rng.randrange(0x7f800000) for _ in range(10000))
            for bits in sorted(candidates):
                for sign in (0,0x80000000):
                    value=struct.unpack('<f',struct.pack('<I',bits|sign))[0]
                    expected=int.from_bytes(encode(value,fmt),'little')
                    if math.isnan(value):
                        expected |= (1 << (eb+mb)) if sign else 0
                    out.write(f'{fmt:x} {bits|sign:08x} {expected:04x}\n')
                    count+=1
    command=['verilator-5.020','verilator','--cc','--exe','--build','-j','4','--Mdir',str(WORK/'cast_obj'),
             '--top-module','cast_test','-Wno-fatal','--no-timing','-f',str(WORK/'sources.f'),
             str(HERE/'rtl/mxcore_output_cast.sv'),str(HERE/'rtl/cast_test.sv'),str(HERE/'rtl/cast_driver.cpp')]
    with (WORK/'cast_build.log').open('w') as log:
        subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.check_output([str(WORK/'cast_obj/Vcast_test'),str(vectors)],text=True)
    if result.strip()!=f'CAST_PASS {count}':
        raise RuntimeError(result)
    report=dict(status='PASS',vectors=count,scope='Every positive/negative representable midpoint, '
                'adjacent FP32 values, signed zero, subnormal boundary, NaN/Inf, and 10000 random values per format')
    (WORK/'casts.json').write_text(json.dumps(report,indent=2)+'\n')
    print(result,end='')


if __name__=='__main__': main()
