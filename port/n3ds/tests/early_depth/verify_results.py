#!/usr/bin/env python3
"""Validate a completed diagnostic SD results directory; mismatches are observations."""
import argparse
import array
import csv
import hashlib
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('directory',type=Path)
p.add_argument('--output',type=Path)
a=p.parse_args()
rows=list(csv.reader((a.directory/'results.csv').open()))
complete=[r for r in rows if r[0]=='complete']
assert len(complete)==1 and complete[0][1]=='1', 'Run incomplete/failed; old captures are not evidence'
reference_only=next(r[1]=='1' for r in rows if r[0]=='reference_only')
phases=2 if reference_only else 4
results={}
for r in rows:
    if r[0]=='result':
        key=(int(r[1]),int(r[3]));assert key not in results
        results[key]=r
assert len(results)==10*phases
# Decode the emitted subset using pinned libctru register addresses.
command_groups={}
for r in rows:
    if r[0]=='command':command_groups.setdefault(tuple(map(int,r[1:4])),[]).append(r)
for case in range(10):
    for phase in range(phases):
        for segment in range(2 if case==7 else 1):
            regs={};cleared=False;draws=0
            for r in command_groups[case,phase,segment]:
                reg,mask,value=int(r[5],16),int(r[6],16),int(r[7],16)
                byte_mask=sum(255<<(8*b) for b in range(4) if mask&(1<<b))
                regs[reg]=(regs.get(reg,0)&~byte_mask)|(value&byte_mask)
                if reg==0x63 and value&1 and all(k in regs for k in (0x61,0x62,0x6a,0x118)):cleared=True
                if reg in (0x22e,0x22f) and value&1:
                    assert cleared
                    assert regs[0x62]&1==int(phase>=2) and regs[0x118]&1==int(phase>=2)
                    assert regs[0x61]&3==int(phase==3) and regs[0x6a]&0xffffff==0
                    draws+=1
            assert draws==(1 if case==7 else 2)
hashes={};comparisons=[];baselines={}
for case in range(10):
    for phase in range(phases):
        streams=[]
        for suffix in ('rgba','d24s8'):
            path=a.directory/f'case{case:02}-phase{phase}.{suffix}'
            raw=path.read_bytes();assert len(raw)==96000*4
            hashes[path.name]=hashlib.sha256(raw).hexdigest()
            words=array.array('I');words.frombytes(raw)
            import sys
            if sys.byteorder!='little':words.byteswap()
            streams.append(words)
        if phase==0:baselines[case]=streams
        differences=[sum(x!=y for x,y in zip(base,cur)) for base,cur in zip(baselines[case],streams)]
        assert differences==list(map(int,results[case,phase][4:6]))
        if phase==1:assert differences==[0,0], 'Off/off control differs'
        if phase<2:
            color,depth=streams
            assert color.count(0x102030ff)>1000 and sum(x!=0 for x in depth)>1000
            if case in (5,9):assert all((x>>24)==2 for x in depth if x)
            if case==4:assert color.count(0xff0000ff)>1000 and color.count(0x00ff00ff)>1000 and len(set(x&0xffffff for x in depth if x))>1
        comparisons.append(dict(case=case,phase=phase,color_differences=differences[0],depth_stencil_differences=differences[1]))
# Cutout must change both color and depth relative to fully opaque near/far.
assert baselines[0][0]!=baselines[4][0] and baselines[0][1]!=baselines[4][1]
assert baselines[1][0]!=baselines[8][0], 'Depth-write-disable scenario vacuous'
report=dict(status='complete',reference_only=reference_only,fixture_sha256=next(r[1]for r in rows if r[0]=='fixture_sha256'),
    off_off_pairs=10,off_off_words_compared=1920000,comparisons=comparisons,capture_sha256=hashes,
    limitation='This checks diagnostic integrity only. Native console identity must be established separately. No performance or universal equivalence claim.')
text=json.dumps(report,indent=2)+'\n'
if a.output:a.output.write_text(text)
else:print(text)
