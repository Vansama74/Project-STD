#!/usr/bin/env python3
"""段级 A/B 对比：对两个 elf 的每个非调试段做 md5（用 objcopy --dump-section），
并打印 size -A 差异。用法: sec_ab.py A.elf B.elf [outdir]"""
import subprocess, sys, hashlib, os, tempfile
from pathlib import Path

def sections(elf):
    out = subprocess.run(['arm-none-eabi-readelf','-S','-W',elf],capture_output=True,text=True).stdout
    rows=[]
    for line in out.splitlines():
        parts=line.split()
        if len(parts)>=7 and parts[0].startswith('[') and parts[1].endswith(']'):
            pass
    # 用解析：找形如 "  [ 1] .text  PROGBITS  080..." 行
    import re
    for line in out.splitlines():
        m=re.match(r'\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)',line)
        if m:
            rows.append((m.group(1), m.group(2), int(m.group(3),16), int(m.group(5),16)))
    return rows

def sec_md5(elf, name, size, tmp):
    if size==0: return 'empty'
    p=os.path.join(tmp, name.replace('.','_').replace('/','_')+'.bin')
    r=subprocess.run(['arm-none-eabi-objcopy','--dump-section',f'{name}={p}'],capture_output=True,text=True)
    if r.returncode!=0: return 'ERR:'+r.stderr.strip()[:40]
    if not os.path.exists(p): return 'missing'
    return hashlib.md5(open(p,'rb').read()).hexdigest()

SKIP_PREFIX=('.debug','.zdebug','.comment','.ARM.attributes','.symtab','.strtab','.shstrtab')

a,b=sys.argv[1],sys.argv[2]
tmp_a=tempfile.mkdtemp(); tmp_b=tempfile.mkdtemp()
sa=sections(a); sb=sections(b)
ma={n:(t,sz) for n,t,ad,sz in sa}
mb={n:(t,sz) for n,t,ad,sz in sb}
diff=0; compared=0
allnames=[n for n in ma if not n.startswith(SKIP_PREFIX)]
for n in allnames:
    if n not in mb:
        print(f"ONLY-IN-A: {n}"); diff+=1; continue
    da=sec_md5(a,n,ma[n][1],tmp_a); db=sec_md5(b,n,mb[n][1],tmp_b)
    if da!=db:
        print(f"DIFF {n}: A={da} B={db}  sizeA={ma[n][1]} sizeB={mb[n][1]}"); diff+=1
    else:
        compared+=1
for n in mb:
    if n not in ma and not n.startswith(SKIP_PREFIX):
        print(f"ONLY-IN-B: {n}"); diff+=1
print(f"== 段级比对：{compared} 段一致，{diff} 段差异（已排除 debug*/comment/ARM.attributes）==")
