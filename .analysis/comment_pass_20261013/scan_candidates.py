#!/usr/bin/env python3
"""扫描业务代码 .c/.h 中「疑似历史/调试/测量」注释行（只读，不改文件）。"""
import re, sys
from pathlib import Path

ROOT = Path('.')
SCOPE = ['Application', 'Device', 'Platform', 'Kernel', 'Core', 'Compiler']
PATTERNS = [
    (r'20\d\d-\d\d-\d\d', '日期'),
    (r'第[一二三四五六七八九十百零\d]+轮', '轮次'),
    (r'修复|修正|修补', '修复记录'),
    (r'探针|probe\s*[789]|标定', '探针/标定'),
    (r'RTT|diag|\[disp\]|\[tcp_|\[gz_ol\]|\[yn_ol\]|\[mm\]|\[xj\]', 'RTT/日志'),
    (r'\bA/B\b|A/B ', 'A/B'),
    (r'md5|elf\b|tree\s*=', 'md5/elf'),
    (r'段尺寸|\.text\s*\d|\.ccmram\s*\d|\.rodata\s*\d|\.bss\s*\d', '段尺寸'),
    (r'历史口径|旧口径|曾经|此前|原先|原来', '历史口径'),
    (r'坑|陷阱|事故|现场|复盘|教训', '坑/事故'),
    (r'#if\s+0', '#if 0'),
    (r'实测|复测|基线', '实测'),
    (r'归档|archive', '归档'),
]
RE = [(re.compile(p), n) for p, n in PATTERNS]

def scan(path):
    hits = []
    try:
        text = path.read_text(encoding='utf-8', errors='replace')
    except Exception as e:
        return hits
    for i, line in enumerate(text.splitlines(), 1):
        s = line.strip()
        if not s:
            continue
        # 只看注释行（// 或 * 开头）里的命中，避免误报字符串/代码
        is_comment = s.startswith('//') or s.startswith('*') or s.startswith('/*') or s.startswith('*/')
        if not is_comment:
            continue
        for rx, name in RE:
            if rx.search(s):
                hits.append((i, name, s[:150]))
                break
    return hits

files = []
for top in SCOPE:
    p = ROOT / top
    if p.is_dir():
        files += sorted(p.rglob('*.c')) + sorted(p.rglob('*.h'))

total = 0
rows = []
for f in files:
    if '/Third_Party/' in str(f) or 'Drivers/' in str(f):
        continue
    hits = scan(f)
    if hits:
        rows.append((len(hits), str(f), hits))
        total += len(hits)

rows.sort(reverse=True)
detailed = '-v' in sys.argv
print(f"== 候选文件 {len(rows)} 个，候选注释行 {total} 行 ==")
for n, f, hits in rows:
    print(f"{n:5d}  {f}")
    if detailed:
        for ln, name, s in hits:
            print(f"        {ln:5d} [{name}] {s}")
