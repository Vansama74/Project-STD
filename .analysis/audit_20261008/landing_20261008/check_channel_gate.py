#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D3-D（通道按协议绑定启动）结构不变量校验（2026-10-08 落地轮）。

回答两件事，全部按**代码事实**静态判定（不需要构建产物、不需要跑板子）：

  A. **绑定先于启动**：所有 `app_proto_bind_channel()` 调用点都在某个
     `sw_app_initcall(<fn>)` 注册的初始化函数内 —— 即绑定发生在 `sw_board_init()`
     （init_task 内、`app_boot.c` 的通道启动之前）阶段。
  B. **门控覆盖**：`app_boot.c` 里 7 条真实通道的启动调用，各由
     `if (app_dispatch_channel_has_proto(CH_ID_X))` 门控（一一对应）；
     `app_rs232_1_start()`（USART6 语音 TX 桩，无通道任务、协议禁止 bind）**不带门控**。
  C. 查询 API 自身存在且**越界返回 false**（`app_dispatch.c`）并有头文件原型。

用法：python3 .analysis/audit_20261008/landing_20261008/check_channel_gate.py
退出码 = 失败项数（0 = 全绿）。
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "Application/Src"

GATED = {
    "CH_ID_TCP_SERVER": "app_tcp_server_start",
    "CH_ID_TCP_CLIENT": "app_tcp_client_start",
    "CH_ID_UDP": "app_udp_start",
    "CH_ID_UDP_CQ": "app_udp_cq_start",
    "CH_ID_UDP_GZOL": "app_udp_gzol_start",
    "CH_ID_RS485": "app_rs485_start",
    "CH_ID_RS232": "app_rs232_start",
}

PASS = 0
FAIL = 0


def check(name: str, ok: bool, detail: str = "") -> None:
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  [通过] {name}")
    else:
        FAIL += 1
        print(f"  [失败] {name}{(' — ' + detail) if detail else ''}")


def _fun_bodies(text: str, name: str) -> list[tuple[int, int]]:
    """返回 text 中函数 name 的定义体 [(start, end)]（花括号配对；跳过声明）。"""
    out: list[tuple[int, int]] = []
    for m in re.finditer(rf"\b{re.escape(name)}\s*\([^;{{]*\)\s*\{{", text):
        depth, i = 0, m.end() - 1
        while i < len(text):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    out.append((m.start(), i))
                    break
            i += 1
    return out


def in_registered_init(text: str, pos: int) -> str | None:
    """pos 是否落在本文件某个 sw_app_initcall(<fn>) 注册的函数体内；是则返回 fn。"""
    for m in re.finditer(r"sw_app_initcall\s*\(\s*([A-Za-z_]\w*)\s*\)", text):
        fn = m.group(1)
        if any(a <= pos <= b for a, b in _fun_bodies(text, fn)):
            return fn
    return None


def in_comment(text: str, pos: int) -> bool:
    """pos 是否位于注释内（行注释 / 块注释粗判：看本行与块注释状态）。"""
    line_start = text.rfind("\n", 0, pos) + 1
    line = text[line_start:pos]
    if "//" in line or line.lstrip().startswith(("*", "/*")):
        return True
    return text.rfind("/*", 0, pos) > text.rfind("*/", 0, pos)


def main() -> int:
    print("== A. 绑定点均在 sw_app_initcall 注册的 init 内 ==")
    files = sorted(SRC.glob("**/*.c"))
    n_bind = 0
    bad: list[str] = []
    for f in files:
        text = f.read_text(encoding="utf-8", errors="ignore")
        if f.name == "app_dispatch.c":
            # 本文件里的匹配是**框架自身的定义体**（`void app_proto_bind_channel(...)`），
            # 不是调用点；调用点全在协议模块里。逐条排除定义体本身。
            text = re.sub(r"void\s+app_proto_bind_channel\s*\([^;{]*\)\s*\{[^{}]*\}",
                          "", text, flags=re.S)
        for m in re.finditer(r"\bapp_proto_bind_channel\s*\(", text):
            if in_comment(text, m.start()):
                continue  # 文档注释里的函数名提及，不计
            n_bind += 1
            fn = in_registered_init(text, m.start())
            if fn is None:
                bad.append(f"{f.relative_to(ROOT)}:{text[:m.start()].count(chr(10)) + 1}")
    check(f"bind 调用点共 {n_bind} 处（已剔除注释提及），全部落在 sw_app_initcall 注册的函数体内", not bad, "; ".join(bad[:4]))
    check("bind 调用点数 > 0（脚本确实检查到了）", n_bind > 0, f"n={n_bind}")

    print("== B. app_boot.c 门控 ==")
    boot = (ROOT / "Application/Src/app_boot.c").read_text(encoding="utf-8")
    for ch, start in GATED.items():
        pat = re.compile(
            r"if\s*\(\s*app_dispatch_channel_has_proto\s*\(\s*" + ch + r"\s*\)\s*\)\s*"
            + start + r"\s*\(\s*\)\s*;", re.S)
        check(f"[{ch}] 由 app_dispatch_channel_has_proto 门控 → {start}()", bool(pat.search(boot)))
    check("app_rs232_1_start() 保持无条件调用（语音 TX 桩，非通道任务）",
          bool(re.search(r"(?<!\)\s)\n\s*app_rs232_1_start\s*\(\s*\)\s*;", boot))
          and not re.search(r"app_dispatch_channel_has_proto\s*\(\s*CH_ID_RS232_1\s*\)", boot))
    check("通道启动调用总数 = 8（7 门控 + 1 桩）",
          len(re.findall(r"^\s*app_\w+_start\s*\(\s*\)\s*;", boot, re.M)) == 8,
          f"实际 {len(re.findall(r'^\\s*app_\\w+_start\\s*\\(\\s*\\)\\s*;', boot, re.M))}")

    print("== C. 查询 API ==")
    disp_c = (ROOT / "Application/Src/app_dispatch.c").read_text(encoding="utf-8")
    disp_h = (ROOT / "Application/Inc/app_dispatch.h").read_text(encoding="utf-8")
    check("app_dispatch.c 实现存在", "bool app_dispatch_channel_has_proto(channel_id_t ch_id)" in disp_c)
    check("越界返回 false（`>= CH_ID_MAX` 判定）",
          re.search(r"\(uint32_t\)\s*ch_id\s*>=\s*\(uint32_t\)\s*CH_ID_MAX", disp_c) is not None)
    check("返回 `ch_proto_map[ch_id] != 0`（只读，不改状态）",
          re.search(r"return\s+g_dispatch\.ch_proto_map\[ch_id\]\s*!=\s*0U\s*;", disp_c) is not None)
    check("头文件原型存在 + 文档注释", "bool app_dispatch_channel_has_proto(channel_id_t ch_id);" in disp_h)

    print(f"\n通道门控结构校验：通过 {PASS} / 失败 {FAIL}")
    return FAIL


if __name__ == "__main__":
    sys.exit(main())
