#!/usr/bin/env python3
"""一次性外科式编辑：把新疆费显协议模块收录进 .eide/eide.yml（2026-10-12 轮）。

设计约束（见本轮任务口径）：
  * 只插行、不改动/不重排任何既有行；缩进与风格照抄邻近条目（`- path:` / `- <inc>`）。
  * Debug / Myanmar 的 incList 在改前**逐字节相同**，故所有定位都先锚定 `  <Target>:` 行、
    再在该目标块内取**首个**匹配项，避免误改另一个目标。
  * 每步断言锚点存在且目标行尚不存在（幂等：重复执行会报错退出，不会插两次）。

插入内容（共 6 行新增 + 1 个虚拟文件夹块 5 行）：
  1. files:  `Application/protocol` 下新增虚拟文件夹 ProtocolParser_XinJiang（3 个 .c，
     排在 `rls` 之后 = 该虚拟文件夹列表末位；EIDE 实际链接序由 objlist 路径字典序决定）
  2. targets.Debug.defineList    += XJ_FAULT_SCREEN=0
  3. targets.Debug.incList       += Application/Inc/ProtocolParser_XinJiang
  4. targets.Myanmar.incList     += Application/Inc/ProtocolParser_XinJiang（兜底）
  5. targets.Myanmar.excludeList += <virtual_root>/Application/protocol/ProtocolParser_XinJiang
  6. targets.Release：不动（incList 仍是已删除的 Drivers/BSP 旧路径，历史残留目标）
"""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[3]
YML = ROOT / ".eide/eide.yml"

XJ_FILES = [
    "Application/Src/ProtocolParser_XinJiang/app_xj_proto.c",
    "Application/Src/ProtocolParser_XinJiang/app_xj_proto_parse.c",
    "Application/Src/ProtocolParser_XinJiang/app_xj_proto_cmd.c",
]
XJ_INC = "Application/Inc/ProtocolParser_XinJiang"
XJ_VFOLDER = "ProtocolParser_XinJiang"
XJ_EXCL = "<virtual_root>/Application/protocol/ProtocolParser_XinJiang"

lines = YML.read_text(encoding="utf-8").split("\n")
edits: list[tuple[int, list[str]]] = []  # (insert-after index in ORIGINAL lines, new lines)


def find(pred, start=0, what=""):
    for i in range(start, len(lines)):
        if pred(lines[i]):
            return i
    raise SystemExit(f"锚点未找到：{what}")


# ---- 1. files：`rls` 虚拟文件夹之后追加 ProtocolParser_XinJiang -----------------
i_rls = find(lambda l: l == "            - name: rls", what="`- name: rls` 虚拟文件夹")
i_rls_end = find(lambda l: l == "              folders: []", i_rls, "rls 的 folders: []")
assert XJ_VFOLDER not in "\n".join(lines[:i_rls_end]), "files 里已存在 XinJiang 条目"
block = [f"            - name: {XJ_VFOLDER}", "              files:"]
block += [f"                - path: {p}" for p in XJ_FILES]
block += ["              folders: []"]
edits.append((i_rls_end, block))

# ---- 2. Debug.defineList += XJ_FAULT_SCREEN=0 ---------------------------------
i_proto_cq = find(lambda l: l == "        - PROTO_CHONGQING", what="Debug 的 - PROTO_CHONGQING")
assert not any(l == "        - XJ_FAULT_SCREEN=0" for l in lines), "defineList 已有 XJ_FAULT_SCREEN"
edits.append((i_proto_cq, ["        - XJ_FAULT_SCREEN=0"]))

# ---- 目标块定位（先锚 `  <name>:`，再块内取首个匹配）----------------------------
def target_span(name: str) -> tuple[int, int]:
    start = find(lambda l: l == f"  {name}:", what=f"目标 {name}")
    nxt = [i for i in range(start + 1, len(lines))
           if lines[i].startswith("  ") and lines[i].endswith(":") and not lines[i].startswith("    ")]
    return start, (nxt[0] if nxt else len(lines))


def insert_after_in_target(name: str, anchor: str, new_line: str, before_anchor: str | None = None):
    s, e = target_span(name)
    if before_anchor:  # 插到 anchor 之前（用于「追加到列表末尾」语义）
        i = find(lambda l: l == before_anchor, s, f"{name} 的 {before_anchor}")
        assert new_line not in "\n".join(lines[s:e]), f"{name} 已有 {new_line}"
        edits.append((i - 1, [new_line]))
        return
    i = find(lambda l: l == anchor, s, f"{name} 的 {anchor}")
    assert i < e, f"{name} 块边界定位失败"
    assert new_line not in "\n".join(lines[s:e]), f"{name} 已有 {new_line}"
    edits.append((i, [new_line]))


# ---- 3/4. Debug / Myanmar incList += XJ -I ------------------------------------
insert_after_in_target("Debug", "        - Application/Inc/ProtocolParser_Myanmar", f"        - {XJ_INC}")
insert_after_in_target("Myanmar", "        - Application/Inc/ProtocolParser_Myanmar", f"        - {XJ_INC}")

# ---- 5. Myanmar.excludeList 末尾 += XJ 排除 -----------------------------------
insert_after_in_target("Myanmar", None, f"      - {XJ_EXCL}", before_anchor="    settings:")

# ---- 应用编辑（从后往前，保持索引有效）----------------------------------------
for at, new in sorted(edits, key=lambda x: -x[0]):
    lines[at + 1:at + 1] = new

YML.write_text("\n".join(lines), encoding="utf-8")
for at, new in sorted(edits):
    print(f"  插入行号 ≈{at + 1}: {new}")
print(f"OK：{YML} 已更新（{len(edits)} 处插入，共 {sum(len(n) for _a, n in edits)} 行）")
sys.exit(0)
