# 现场三角复核：GZ_OL「16 正常 / 24、32 无显示」+「`0x40` 改端口不生效」

> **【修订 2026-09-15 · 用户裁决（现行口径）】** `0x40` 端口字段改为**高字节在前（BE16）**：写 10028 发 `27 2C`
> （改后即生效），发 `2C 27` 解析为 11303。本报告 §②-6、§③/§④ 里「小端 = `2C 27`」的判读为历史口径；
> 现行口径见 `doc/14_贵州治超屏协议/README.md` §13.5.1 与 `port_10028_diagnosis.md` §8.8。

> 日期：2026-09-14（复测轮）　协议：贵州治超屏（GZ_OL，"TCLY" 帧族，源项目 `9K23881580`）
> 任务：判定这两个症状是**代码/实现问题**还是**镜像与测试方法问题**，并给出一条能一次性定案的自助诊断。
> 纪律：不改 `dev_display_22_1703.c`（用户实验态）、不改 `.eide/eide.yml`、不改协议行为、未提交、未烧录。
> **本轮零新增代码**：需要的「开机自报身份 + 屏体几何 + 网络配置 + 各 UDP 服务口」诊断设施
> **已存在于工作区**（上一轮 17:31~17:35 落地，见 §①.3），本轮为**核实 + 复用 + 补齐判读口径**。

---

## 结论速览（TL;DR）

| 判定项 | 结论 |
|---|---|
| 字形越界裁剪改动是否真的在当前工作区 | **✅ 生效**（`dev_display.c:243-256` 交集裁剪；`app_render.c:665`/`:702` 行终止条件 `cur_y >= cfg->h`）——**但只在“重新编译并烧录”之后才对板子生效** |
| GZ_OL 专用 UDP 服务口是否真的在当前工作区 | **✅ 生效**（`app_udp.c` `CH_ID_UDP_GZOL` 实例 + `app_boot.c:258` 在其启动顺序中 + `app_gz_ol_proto.c:108` 第四 mask） |
| 两个症状在什么条件下**必然**出现 | **① 板上镜像仍是 16:02 那份 EIDE 产物（改动前）→ 两个症状 100% 原样复现**（24/32 全黑 = 旧「整块丢弃」语义；端口无效 = 当时根本没有专用服务口）。<br>**② 板上是 EIDE 旧口径产物（22-1703 处于 1×1 = 32×16 实验态）→ 24/32 全黑**；此时即便有新代码，若屏体是 32×16 则 24/32 只会露出**可见上部**——「完全无内容」只属于旧镜像。<br>**③ 端口测试方法问题**（未重启 / 发到旧端口 / 端口值撞 10011·20103 被跳过 / 拿 `0x50` 回显当落盘证据）→ 表现恰似「改了没生效」。 |
| 一条定案诊断 | 复用既有 `[diag]` 开机横幅 + GZ_OL 逐帧诊断（`APP_DIAG_BANNER`，默认开、`make APP_DIAG=0` 一键关）。**看不到 `[diag]` 行 = 板上不是这份代码**（旧镜像没有该设施）——一行定案。 |
| Flash/RAM 增量（诊断设施） | Flash **+5120B**（text +2464 / rodata +2656）；**RAM 0B**（data/bss/ccmram 全部零变化，A/B 实测） |
| 四口径构建 | **全部链接通过、零新增告警**（仅 3 条既有 HAL `unused parameter`），段尺寸/余量见 §⑤ |

---

## ① 代码实态核实（逐条给文件+行号）

### ①.1 字形越界裁剪：真的在源码里（读代码确认，不是只信文档）

**显示层**——`Device/Display/dev_display.c:225-260`（函数 `dev_display_draw_bitmap`）：

```240:256:Device/Display/dev_display.c
     * vis_w==w 且 vis_h==h（完全在屏内，常见路径）时本循环与旧快路径逐像素等价
     * （同源地址、同目的地址、同位序，指针寻址无额外开销）。 */
    uint16_t vis_w = ((uint32_t)x + w <= dev->screen_rows) ? w : (uint16_t)(dev->screen_rows - x);
    uint16_t vis_h = ((uint32_t)y + h <= dev->screen_cols) ? h : (uint16_t)(dev->screen_cols - y);
    if (vis_w == 0U || vis_h == 0U)
        return;

    uint16_t row_bytes = (w + 7) / 8;
    for (uint16_t row = 0; row < vis_h; row++) {
        const uint8_t *src = &bitmap[(uint32_t)row * row_bytes]; /* 被裁掉的底部行不再读取 */
        /* 索引中间值用 uint32 防 (y+row)*rows 的 uint16 回绕；交集保证终值在 pixel_map 内 */
        uint8_t *dst = &dev->pixel_map[(uint32_t)(y + row) * dev->screen_rows + x];
        for (uint16_t col = 0; col < vis_w; col++) {
            if (src[col / 8] & (0x80 >> (col % 8)))
                dst[col] = (uint8_t)color;
        }
    }
```

- **起点越界仍整体丢弃**（`dev_display.c:229-230`：`if (x >= screen_rows || y >= screen_cols) return;`）——与 `dev_display_fill` 语义对齐。
- **部分越界改裁剪**：`vis_w`/`vis_h` 取位图矩形与屏幕的交集（`:243-246`），**逐像素只画交集内的部分**（`:249-256`）。
- 旧「整块丢弃」语义已不在源码中（`git show HEAD` 侧的旧实现与本文件当前内容不同；本文件为工作区修改态）。

**渲染层**——`Application/Src/app_render.c`：

- 起点：`uint16_t cur_x = cfg->x, cur_y = cfg->y;`（`:536`）——**首行起点就是报文给定的 y**。
- 垂直对齐只在 `ALIGN_CENTER` / `ALIGN_RIGHT_DOWN` 时下移（`:612-615`）；GZ_OL 传的是 `ALIGN_LEFT_UP`（`app_gz_ol_proto_cmd.c:288-293`）→ **不存在“居中下移”这一步**。
- 折行后的行终止条件（ASCII 分支 `:661-665`、GBK 分支 `:698-702`）均为：

```665:665:Application/Src/app_render.c
                    if (cur_y >= cfg->h) return;
```

即**只有“行首已经完全越出渲染区域”才终止**；行框下缘越屏但行首仍在区域内 → 继续画，由显示层裁剪出可见部分。

**执行层**——`Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c:224-227`：

```224:227:Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c
    const uint16_t x = (p->x < d->screen_rows) ? p->x : 0U;
    const uint16_t y = (p->y < d->screen_cols) ? p->y : 0U;
    const uint16_t w = (uint16_t)(d->screen_rows - x);
    const uint16_t h = (uint16_t)(d->screen_cols - y);
```

> **注意 x/y 被“钳位”**：报文 Y ≥ 屏高时**不是丢弃、而是当 0 处理**（`y = 0`，`h = 屏高`）。

#### 逐案推演：「屏体 32×16 + 字号 24/32 + 报文 Y 偏移」（W=32 屏宽，H=16 屏高）

设报文字段为 `(px, py)`，`0x20` 先整屏清黑（`:281`）再渲染：

| 报文字段 | 执行层实际参数 | 首行是否绘制 | 可见结果 |
|---|---|---|---|
| `py = 0` | `y=0, h=16` | ✅（`cur_y=0 < 16`） | 字形框 24×24 / 32×32 与屏取交集 → 画出 **y=0..15 共 16 行**（24 点阵 = 上部 2/3；32 点阵 = 上半） |
| `py = 8` | `y=8, h=8` | ✅（**首行不做行终止判定**） | 画出 **y=8..15 共 8 行**（字形在屏幕下半的可见部分） |
| `py = 15` | `y=15, h=1` | ✅ | 画出 **1 行** |
| `py ≥ 16`（例：上位机按 64 高屏居中，24 点阵 `py=(64-24)/2=20`） | **钳位** `y=0, h=16` | ✅ | 等同于 `py=0` → **上部 16 行可见** |
| 首个 GBK 字之后**折行**（`w=32`，24 点阵每字 24px → 第 2 字必折行） | 折行后 `cur_y = y+24 ≥ h` | ❌ (`:702` 终止) | **只显示首字的可见部分**（后续行不再画） |

**推演结论**：
1. 在**当前源码**上，屏体 32×16 时字号 24/32 **必然“有内容”**（至少 1 行像素，通常 8~16 行），**不可能全黑**。
2. 因此现场「24/32 屏幕上什么都没有」与**当前源码**不自洽；它与**旧镜像**（整块丢弃 + 32×16 几何）完全自洽。
3. 若现场重烧后仍全黑，则原因不在“裁剪/行终止”，而在**渲染根本没落屏**（帧未到/被 probe 丢弃、字号字段非法、字库读空、任务未跑）——这三类**由 `[gz_ol]` 逐帧诊断一次性区分**（§③）。
4. 报文 Y 偏移在**新代码**上只影响“可见部分的位置与多少”，**不会**造成完全不可见（含 `py ≥ 屏高` 被钳 0 的情形）。

### ①.2 GZ_OL 专用 UDP 服务口：代码在位、启动顺序正确、跳过策略已实现

- **实例**：`Application/Src/Channel/app_udp.c` 尾部 GZ_OL 段——`_udp_gzol_read_port()`（`app_udp.c:545-556`）：

```545:556:Application/Src/Channel/app_udp.c
static bool _udp_gzol_read_port(void)
{
    app_board_net_cfg_t cfg;
    if (app_board_net_cfg_get(&cfg) == 0 && cfg.port > 0U && cfg.port <= 65535U) {
        g_udp_gzol_port = (uint16_t)cfg.port;
        return true;
    }

    g_udp_gzol_port = UDP_GZOL_FALLBACK_PORT;
    return false;
}
```

  - `UDP_GZOL_FALLBACK_PORT = 9528`（`app_udp.c:453`）；端口来源 = Sector1 `net_cfg.port`（与 TCP 业务口同号不同协议栈）。
  - **每轮重建前重读**（`udp_gzol_task` 的 `for(;;)` 内 `:565`）→ `0x40` 写完立即 `NVIC_SystemReset()`，重启后新端口生效。
- **跳过/回退策略**（`app_udp.c:565-585`）：端口 == `app_udp_get_port()`（10011）或 == `app_udp_cq_get_port()`（20103）→ **`busy` → 跳过绑定**（`bind_status = SKIPPED`，RTT 打印 `UDP service port %u busy ... -> skip bind`）；记录无效/port=0 → `FALLBACK(9528)`。绑定成功打印 `[diag] gzol UDP service bound OK port=%u (net_cfg.port valid)`（`app_udp.c:609-610`）。
  - **现场后果**：把端口改成 **10011 或 20103** → 不会新开服务口（但 GZ_OL 仍由 `CH_ID_UDP`（10011）应答）→ **表现恰似“改了没生效”**。这是设计使然（同口二次 bind 必 `ERR_USE`）。
- **启动顺序**（`Application/Src/app_boot.c`）：`app_net_boot_apply()`（`:233`）→ `[diag]` 横幅（`:237`）→ splash（`:240`）→ `app_tcp_server_start`/`app_tcp_client_start` → **`app_udp_start()`（`:253`）→ `app_udp_cq_start()`（`:255`）→ `app_udp_gzol_start()`（`:258`）** → RS485/RS232 →（`:268-270` 延迟体检）→ `app_default_display()`（`:274`）。**GZ_OL 专用口确在 `app_net_boot_apply()` 之后启动**，端口已按 Sector1 定稿。
- **协议绑定**（`Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c`）：四通道 `RS485`（`:86`）+ `RS232`（`:92`）+ `CH_ID_UDP` 10011（`:102`）+ **`CH_ID_UDP_GZOL`（`:108-110`）**，四 mask 共用一静态队列、RJ45 槽只 acquire 不 provide。
- **核验手段**（`0x40` 落盘证据链，`app_gz_ol_proto_cmd.c`）：`0x40` 写前/写后 read-back + 落库返回值（`:401-435`）；`0x50`/`0x60` 搜索 → `0x70` 14B 应答载荷 = `ip4+mask4+gw4+port2(小端)`（`:76-85`、`:456-465`）。**`0x50` 是回显应答（ack），不能当落盘证据**（代码注释显式标注 `ack=0x50 echo (NOT evidence)`，`:432`）。
- **TCP 业务口同值**：`app_net_boot.c:69` `app_tcp_server_set_port(cfg.port)`——所以 `net_cfg.port` 同时被 TCP Server 与 GZ_OL 专用 UDP 口消费（`0x40` 改端口对两者都生效）。

### ①.3 `Application/Inc/app_diag.h` 的来源、使用者与覆盖面（**本轮复用，未新增代码**）

**来源判定**：**不是本轮任务加的**，是本条 GZ_OL 问题线**上一轮（今天 17:31~17:35）**刚落地的“自证固件”设施——文件头自述 `2026-09-14 GZ_OL ×2 问题复测轮`（`app_diag.h:3`），与工作区时间戳一致（`app_diag.h` 17:31 / `app_boot.c` 17:32 / `dev_display.c` 17:31 / `app_render.c` 17:34 / `app_gz_ol_proto_cmd.c` 17:35 / `Makefile` 17:34）；`git status` 中 `app_diag.h` 为未跟踪（`??`）、`app_boot.c` 为修改态。`doc/06.../04_current_memory_occupancy.md` §7.2 也记有「当前工作区另有并发改动持续写入（`app_render.c`/`app_boot.c`/`app_udp.c`/`app_diag.h` 等）」。**没有任何其它模块依赖它**（全库引用点：`app_boot.c`、`app_gz_ol_proto_cmd.c`、`Makefile`）。

**现成能力（正好就是任务书要求的那张“一行定案”信息）**：

| 位置 | 宏 | 输出 |
|---|---|---|
| `Application/Inc/app_diag.h:24-25` | `APP_DIAG_BANNER`（**默认 1**；`make APP_DIAG=0` 一键关，`Makefile:64-67`） | 总开关 |
| `app_boot.c:133-190` `app_diag_boot_banner()` | 同上 | 开机横幅：`fw` / `built` / `tree`（树哈希）/ `build proto= disp= config= toolchain=`；`display screen=WxH code= scan_lines chans`；`display module=WxH ch/mod mods=RxC scan_line_px buf`；`driver linked? 1_263=? 22_1703=?`；`font chip= ... dip2=`；`netcfg VALID/INVALID ip/mask/gw/port/udp_port`；`ports t0 udp10011 tcp_biz cq_udp gzol_cached` |
| `app_boot.c:192-215` `app_diag_boot_late()`（`:268-270`，延时 2.5s） | 同上 | 通道实测：`chan udp10011=UP/DOWN port=`、`chan gzol=? port=? bind=OK/FALLBACK/SKIPPED/FAIL`、`chan tcp_srv= port=`、`netcfg readback port/udp_port` |
| `app_gz_ol_proto_cmd.c:109-330` | `GZ_OL_RTT_DIAG`（默认跟随 `APP_DIAG_BANNER`，`:118-120`） | `0x20` 每帧：载荷 dump / 解析结果 / 传给渲染的 `x y w h` / `glyph_visible=WxH of WxH` 越屏判定 / **字库 16-24-32 三档实读**（`font-probe` 段：`addr`/`inCAP`/`rd`/`nz`/`head` + 另一套布局的纯算术地址）/ 渲染后显存非黑像素数 |
| `app_gz_ol_proto_cmd.c:386-435` | 同上 | `0x40` 每帧：写前快照 / 请求值 / **落库返回值** / **写后 read-back** / 「ack 不是证据」提示 |
| `Device/Display/dev_display.c:29,46,90` | 无条件 | `g_dev_display_commit_count` 提交计数（`0x20` post-render 行引用） |
| `Application/Inc/app_render.h:147` `app_render_chip_info_get()` | 无条件 | 字库芯片名/区块数/自适应档/编码口径/DIP2（横幅引用） |

**覆盖面核对（任务书要求 → 现有设施）**：镜像身份 ✅（`fw`+`built`+`tree`+口径四元组；EIDE 构建无 Makefile 指纹时打印占位 `no-fingerprint`/`?`，仍可判几何与端口）；屏体几何 ✅（`display screen=WxH` + `module=` + `driver linked?`）；网络配置 ✅；各 UDP 服务口 ✅（10011 / CQ / GZ_OL 三者的缓存端口 + 实测绑定结果与 bind 状态）。

→ **判定：已有等价设施，按任务书要求“优先复用”，本轮不新增任何诊断代码。**

### ①.4 构建入口差异：EIDE Debug 与 Makefile 分别会烧出什么

**Makefile 默认口径（`make -j8`，`PROTO=ALL` `DISP=1_263`）**
- 显示模组：`dev_display_1_263.c` → **224×64**，CCM 显存 29568B（`DISP` 二选一，非法值 `$(error)`）。
- 协议：**全协议**（含 `ldi`、CQ、GZ_OL、安徽…），定义 `STD_ALL_PROTO`（`{` 帧族互斥守卫豁免）、**不含** `PROTO_CHONGQING` → `app_net_boot` 默认值为 **dev 口径**（IP `192.168.114.200`、`port 9528`、`udp_port 20103`）。
- 含 `CH_ID_UDP_GZOL`（`app_udp.c` 无条件编译）+ 字形裁剪改动（同一份 `dev_display.c`/`app_render.c`）。
- 含 `APP_DIAG_BANNER=1` 横幅（`app_diag.h` 默认）+ Makefile 注入的构建指纹（树哈希/口径）。

**EIDE Debug（当前 `.eide/eide.yml`，用户在 15:32 改过，本轮只读不改）**
- `defineList`：`USE_HAL_DRIVER`、`STM32F407xx`、**`PROTO_CHONGQING`**（`eide.yml:395-398`）→ **CQ 口径默认值**：IP `192.168.1.5`、`port 9528`、`udp_port 20103`（`app_net_boot.c:46-61`）。
- `excludeList`（`eide.yml:433-452`）排除：`ProtocolParser_{ShanDong,YunNan,ChongQing,QingHai,GuiZhou,Anhui}` + `rls` + `ah` + **`ldi`** + `Drivers/BSP/{key,Ringbuffer,scan}` + **`Device/Display/dev_display_1_263.c`** + `dev_display_p20.c` + `dev_display_1_969.c` + `dev_display_1_577.c` + `dev_display_1_260.c`。
- ⇒ **EIDE Debug 编入的显示模组只有 `dev_display_22_1703.c`**（几何 = 该文件当前宏 → **当前工作区为 `MODULE_ROWS=7 / MODULE_COLS=4` = 224×64**，`dev_display_22_1703.c:58-59`；CCM 显存 31648B）；协议集 = IAP + 四川三协议 + **贵州治超**（未排除）。
- ⇒ EIDE Debug **同样包含**字形裁剪改动、`CH_ID_UDP_GZOL`、`APP_DIAG_BANNER` 横幅（`app_diag.h` 默认 1）——**只要重新构建**，EIDE 产物也是修好的；只是横幅的 `build proto/disp/config/toolchain` 与 `tree=` 会打印占位值（Makefile 才注入 `DIAG_DEFS`，`Makefile:76-81`）。
- ⇒ **`doc/CLAUDE.md` 现在写的「EIDE Debug 显示模组编 1-263」已与 `eide.yml` 实态不符**（实为 1-263 被排除、22-1703 编入）——已在 §⑥ 按实态更正。

**对症状的意义**：用户 16:02 烧的是**当时**的 EIDE 产物（上一轮报告已用 `.ccmram=5040`、`nm` 含 `dev_display_22_1703_init` 不含 `1_263`、hex `du -h` 664K 三条证据锁定），那份产物**早于**本轮所有改动（17:29~17:35）→ **不含裁剪、不含专用 UDP 口**，且当时 `dev_display_22_1703.c` 处于 **1×1 = 32×16** 实验态（`.ccmram` 差值 11420 vs 当前 39164 精确等于 27744 = 31648−3904）⇒ **两个症状原样复现完全由“镜像陈旧”解释**。

---

## ② 症状复现条件枚举（可观察现象 + 判别方法）

### ②.1 问题①：24/32 点阵「没有显示」

按可能性排序：

| # | 假设 | 可观察现象 | 判别方法 |
|---|---|---|---|
| ①-1 | **板上仍是修复前的旧镜像**（未重烧 / 重烧失败 / 烧了别的 elf） | 16 正常、24/32 **全黑**（先清屏后整块丢弃）；端口问题同时存在 | RTT **完全没有 `[diag]` 行**（旧镜像无此代码）= 铁证；辅以 `arm-none-eabi-size -A` 的 `.ccmram` 与 `nm`（EIDE 系：旧 1×1 产物 **5040** = 3904+1136（无 CQ）；新 EIDE 22-1703 7×4 口径 **32787** = 31648+1136+3（算术推导，未实构建）。make 系：`DISP=1_263` **37084**、`DISP=22_1703` **39164**）与 hex `du -h`（旧 EIDE 664K / make ≈1020K） |
| ①-2 | **EIDE 旧口径产物（22-1703 处于 1×1 = 32×16 实验态）** | 16 正常（16≤16 恰好通过）、24/32 全黑（旧语义整块丢弃） | 横幅 `[diag] display screen=32x16 ... module=32x16 ... mods=1x1` |
| ①-3 | 新镜像 + **EIDE 口径 224×64，但物理台架只有一块 32×16 模组**（用户 WIP） | 24/32 可能“看起来没显示/只显示一块区域”——取决于实际接线覆盖哪一段 | 横幅 `display screen=WxH module=... mods=RxC driver linked?`，与台架模组数逐项核对；用 `0x30` 清屏 + `0x20` X=0/Y=0 单字打点验证“哪块模组对应 pixel_map 的哪个区域” |
| ①-4 | 新镜像 + 24/32 报文的 **Y 偏移** | **不可能全黑**（§①.1 推演：y∈[0,15] 恒有可见行；y≥16 被钳 0）；只可能“可见部位偏下/偏少” | `[gz_ol] 0x20 x= y= ... -> render x= y= w= h=` 与 `glyph_visible=...` 行 |
| ①-5 | 帧**未到**或被 probe/parse 丢弃（字号字段非 16/24/32、长度不足、CRC 无关但长度/结尾符不合） | 无任何变化（屏上保持上一帧/黑屏） | **完全没有 `[gz_ol] 0x20` 输出** = 帧未到/未认领；上位机侧对拍 `18 18`/`20 20` 两字节相等 |
| ①-6 | 字库侧（DIP2 选错 / 24-32 区块缺失/未编程 / 地址错） | 通常不是“空白”而是**错尺寸字形 / 0xFF 实心块**；若读回全 0 则表现为空白 | `[gz_ol] font-probe` 段三档 `rd=`/`nz/total`/`head=`：`rd=0` 且 `nz=0` → 字模全 0（区块未编程/为空）；`head` 全 `FF` → 芯片未编程（擦除态）；`rd≠0` → 读取失败（地址越界/SPI）；`alt-layout(...)` 行判「激活布局是否选错」；横幅 `font chip= ... dip2=` |
| ①-7 | 渲染未落屏（任务未跑/渲染锁争用/commit 没提交） | 屏黑 | `[gz_ol] 0x20 post-render: nonblack=? commits=?`（`nonblack>0` = 已落 `pixel_map`；`commits` 在涨 = 提交链在跑） |

### ②.2 问题②：`0x40` 改端口「不生效」（IP 生效）

| # | 假设 | 可观察现象 | 判别方法 |
|---|---|---|---|
| ②-1 | **旧镜像**（无 `CH_ID_UDP_GZOL`，服务口当时是编译期常量 10011） | 改端口后**只有 TCP 业务口变**、GZ_OL 的 UDP 服务口不变 | 横幅**无** `chan gzol=` 行 / `nm` 无 `app_udp_gzol_get_port` |
| ②-2 | **未重启**（`0x40` 应答后本应 100ms 自复位） | 若从未观察到设备重启 → 帧可能没被处理 | `[gz_ol] 0x40 write ret=... readback=...` + `ack=0x50 echo (NOT evidence) -> reboot in 100ms` |
| ②-3 | **发到了旧端口**（改完端口仍往 10011/20103 发） | 10011 仍会应答 `0x70`（兼容口保留）→ **看似“端口没变”** | 向**新端口**发 `0x60`：有 `0x70` = 新口生效；无应答 = 没生效 |
| ②-4 | **端口值撞 10011 / 20103** → 设计上跳过绑定 | 表现“改了没生效”（实际是 SKIPPED） | 横幅/体检 `chan gzol=DOWN bind=SKIPPED(port busy with 10011/CQ)` + RTT `UDP service port %u busy ... -> skip bind` |
| ②-5 | 端口值非法（0 / 记录损坏）→ **回退 9528** | 表现“改了没生效”（回到默认 9528） | `bind=FALLBACK(9528, net_cfg.port invalid)` |
| ②-6 | **拿 `0x50` 回显当落盘证据** | `0x50` 一定跟请求一致，无法证明落盘 | 改用 `0x70`（`0x60` → 14B 载荷含 port2 小端）或 `0x10` 新口探活；看 `[gz_ol] 0x40 readback=` |
| ②-7 | IP 也一起改了，仍往旧 IP 发 | 无应答 | 用 `0x70` 拿回 `ip/mask/gw/port` 四元组 |
| ②-8 | PC 侧（防火墙/UDP 源端口缓存/工具绑错本地口） | 新端口无应答，旧端口有 | 换 `nc -u`/换工具重测；先 `0x10`（应答 2B `00 00`，最短闭环） |
| ②-9 | 端口设成与改前**相同值**（如默认 9528） | “没变化” | 改一个**从未用过且避开 10011/20103** 的值（如 9560） |

### ②.3 「这两个症状在什么条件下必然出现」——判定结论

- **必然出现**：板上跑的是**本轮改动之前**的任何镜像（16:02 那份 EIDE 产物是最可能的）。此时：
  - 24/32 = 旧「整块丢弃」语义 × 屏体 32×16 → **清屏后必然全黑**（16 恰好通过）；
  - 端口 = 没有专用服务口（`0x40` 只改了 TCP 业务口）→ **UDP 服务口必然不变**。
- **只可能出现其一**：板上是**新镜像**但屏体几何不匹配台架（32×16 台架 + 224×64 口径，或反之）→ 24/32 现象可复现，端口现象**不应**复现（新镜像端口一定生效，除非落在 ②-2~②-9）。
- **不应出现**：新镜像 + 几何匹配 + 按 §④ 的步骤复测 → 24/32 **至少有可见部分**；端口**在新端口必然能被服务**（除非 SKIPPED/FALLBACK 两个已知设计分支）。
- 因此：**先做 §④ 的“镜像身份确认”，再谈代码。** 只要板上不是 17:29 之后的构建，这两个症状都不构成缺陷证据。

---

## ③ 一条能一次性定案的自助诊断（复用既有设施，零新增代码）

### ③.1 用什么

- 总开关：`Application/Inc/app_diag.h:24-25` `APP_DIAG_BANNER`（**默认 1**）。
- 一键关：`make APP_DIAG=0`（`Makefile:64-67`）或把该宏改 0。
- 期望输出（默认口径、正常板子，节选；EIDE 构建时 `proto/disp/tree` 为占位值，其余相同）：

```
[diag] ================ SELF-PROVING BANNER ================
[diag] fw=0000000000 built=<编译日期> <编译时间> tree=<8位树哈希>
[diag] build proto=ALL disp=1_263 config=Debug toolchain=gcc
[diag] display screen=224x64 code=1000000263 scan_lines=8 chans=4
[diag] display module=32x32 ch/mod=2 mods=7x2 scan_line_px=512 buf=14336
[diag] driver linked? 1_263=1 22_1703=0 (exactly one expected)
[diag] font chip=<芯片名> regions=<n> adapt=<n> ascii_raw=1 gbk190=0 cap=<KB> dip2=0
[diag] netcfg VALID ip=192.168.114.200 mask=255.255.255.0 gw=192.168.114.1 port=9528 udp_port=20103
[diag] ports t0 udp10011=10011 tcp_biz=9528 cq_udp=20103 gzol_cached=9528 (gzol rule: net_cfg.port; 0/invalid->9528; ==10011/20103->skip bind)
...
[diag] ---- post-boot check (channels started) ----
[diag] chan udp10011=UP port=10011 | cq_udp=UP port=20103
[diag] chan gzol=UP port=9528 bind=OK | tcp_srv=UP port=9528
[diag] netcfg readback port=9528 udp_port=20103 (source of truth)
[diag] tip GZ_OL frames: 9528 (or 0x40-set value) / 10011 both accepted; ...
```

复测期间每帧 `0x20` / `0x40` 另打 `[gz_ol]` 证据行（`0x20` 至少 5 行、`0x40` 3~4 行）。

### ③.2 判读表（一次性定案）

| 现场看到 | 定案 |
|---|---|
| **完全没有 `[diag]`**（RTT 里只有旧的业务日志/什么都没有） | **板上不是这份代码**（旧镜像）→ 全部现象由镜像陈旧解释：重烧 + 擦 Sector1 后复测 |
| `display screen=32x16` + `driver linked? 22_1703=1` | 板上是 22-1703 的 **1×1 实验态**几何 → 24/32 只应露出**上部**（新代码）；若仍全黑 → 回到上一行判定 |
| `driver linked? 1_263=1 22_1703=0` 且 `screen=224×64` | 屏体几何与 224×64 一致 → 24/32 应完整可见 |
| `chan gzol=... bind=OK port=P` + `netcfg readback port=P` | 端口链路正常 → 复测方法问题（②-3/②-6/②-8） |
| `bind=SKIPPED(port busy with 10011/CQ)` | 端口值撞了 10011/20103 → 换值（设计行为，非缺陷） |
| `bind=FALLBACK(9528, net_cfg.port invalid)` | 端口值非法/记录损坏 → 检查 `0x40` 载荷长度与字段 |
| `[gz_ol] 0x20` 一行都不出现 | 帧未到/未认领 → 查上位机字节流与串口/网口路径（波特率 DIP1=OFF 9600） |
| `[gz_ol] 0x20 glyph_visible=...` + `post-render: nonblack>0` | 渲染已落屏 → 屏上没内容属**扫描/屏体/接线**（或几何与台架不匹配） |
| `post-render: nonblack=0` | 渲染/字库侧（结合 `font probe` 三档 `rd/nz`） |

### ③.3 增量与影响（A/B 实测，默认口径 `PROTO=ALL` `DISP=1_263`）

| 项 | 诊断 ON | 诊断 OFF（`make APP_DIAG=0`） | 增量 |
|---|---|---|---|
| text | 167060 | 164596 | **+2464 B** |
| rodata | 200712 | 198056 | **+2656 B** |
| data | 1672 | 1672 | 0 |
| ccmram | 37084 | 37084 | 0 |
| bss | 126400 | 126400 | 0 |

- **Flash 合计 +5120B（约 +0.7%），RAM +0B**（诊断全为只读打印；无新增静态对象）。
- **唯一的行为差异**：`APP_DIAG_BANNER=1` 时 `init_task` 多一次 `osDelay(2500)`（`app_boot.c:268-270`），
  即**开机默认画面延后 2.5s 出现**（通道任务与协议处理不受影响，横幅在 splash 之前打）。关掉宏即完全消失。
- 关闭后固件与“从未加过诊断”等价（诊断块整体 `#if` 掉，未被引用的辅助函数由 `--gc-sections` 丢弃）。

---

## ④ 现场复测清单（照做即可）

### ④.1 先确认「板上跑的是哪份镜像」（**这一步不做，后面全部白测**）

1. **构建（严禁 EIDE 产物直接烧）**：
   ```bash
   cd /home/yystation/Program/3833024/Project-STD-main
   make clean && make -j8          # 默认口径：PROTO=ALL DISP=1_263（224×64）
   ```
   （要用 P10 台架 22-1703 口径时：`make clean && make -j8 DISP=22_1703`；**别用 EIDE 的 `build/Debug/*` 覆盖态产物**。）
2. **烧录前对本机构建物核对**（三条命令，任一不符即别烧）：
   ```bash
   md5sum build/Debug/Project_STD.elf build/Debug/Project_STD.hex     # 记录下来，与本节第 5 步对拍
   arm-none-eabi-size -A build/Debug/Project_STD.elf | grep -E '^\.(text|rodata|data|bss|ccmram)'
   #   默认口径应为：.text 167060 / .rodata 200712 / .data 1672 / .bss 126400 / .ccmram 37084
   #   （make APP_DIAG=0 时为 .text 164596 / .rodata 198056，其余不变——差值即诊断增量）
   arm-none-eabi-nm build/Debug/Project_STD.elf | grep -E 'dev_display_(1_263|22_1703)_init|app_udp_gzol_get_port|app_diag_boot_banner'
   #   默认口径：只应出现 dev_display_1_263_init；后两个符号（gzol / diag）都应在
   du -h build/Debug/Project_STD.hex      # 默认口径 ≈ 1020K；若 ≈664K 说明烧到了旧 EIDE 产物
   ```
3. **烧录纪律**：`openocd ... program ... verify reset exit`（或 `bash tool/flash_all.sh`）；
   **任何烧录器烧完主固件后必须擦除 Sector1（`0x08004000~0x08007FFF`）恢复出厂态**
   （否则 Bootloader 条件 D 可能判 App 损坏 → 设备进 Recovery，主固件不跑，全部端口无响应）；
   **DIP1 = OFF**（GZ_OL 波特率 9600 8N1）。
4. **上电抓 RTT**：连 SWD，RTT 通道 0 收 **`[diag]`\* 横幅**（约开机后 0~3s 内）。
   - **看不到 `[diag]` → 板上不是新镜像**（旧镜像没有这段代码）→ 检查烧录是否真的落盘（重烧 + 擦 Sector1）。
5. 从横幅抄下四处：`fw/built/tree`、`display screen=WxH`、`netcfg ... port/udp_port`、`chan gzol=... bind=`。

### ④.2 问题①复测（24/32 点阵）

- **X=0、Y=0 各发一次**，再**按居中发一次**（例如 24 点阵 Y=20、32 点阵 Y=16——模拟上位机按 64 高屏居中）：
  - 帧：`54 43 4C 59` + 序号 4B + 长度 2B（小端 = 整帧长）+ 保留 2B + 命令 4B（`0x20` 低字节）+ 载荷（`X(2 LE) Y(2 LE) 屏宽(2 忽略) 屏高(2 忽略) 字体名(4 GBK) 字号(2，两字节均为 16/24/32) 颜色(3 RGB) 文本(GBK)`）+ `0x00`。
  - **预期**：16 → 完整；24/32 → **224×64 口径完整可见**；**32×16 口径露出可见上部（不是全黑）**。
  - Y 偏移只改变“可见部位的位置/多少”，**任何 Y 都不应导致全黑**。
- 若出现异常，把每帧的 `[gz_ol] 0x20 ...` 全部五行抓回来（含 `font probe` 的 16/24/32 `rd/nz` 与 `post-render nonblack`）。

### ④.3 问题②复测（端口）

1. 选一个**从未用过、且避开 10011 与 20103** 的端口值，例如 **9560**。
2. 发 `0x40`（载荷含 `ip/mask/gw/port`）：
   - 观察 `[gz_ol] 0x40 write-before / requested / write ret / readback`；
   - 设备应在 100ms 后**自复位**（`ack=0x50` 只是回显，**不作证据**）。
3. 重启后看 RTT 横幅与体检：
   - `netcfg readback port=9560` + `chan gzol=UP port=9560 bind=OK` = 落盘 + 绑定都成功；
   - 若 `bind=SKIPPED` / `FALLBACK`，按 §③.2 判读（设计分支，不是缺陷）。
4. **向新端口实发**验证服务口真的在服务：
   - 发 `0x60`（搜索）→ 期望 **`0x70` 应答**，14B 载荷里 `port` 字段 = 9560（小端 `0x60 0x25` = 9560? 按小端算 9560=0x2558 → `58 25`）；
   - 再发 `0x10`（故障查询）→ 期望 **2B `00 00`** 应答。
   - 注意：**10011 也应同时仍可用**（兼容口保留）——用 10011 有应答 **不能**否定“新端口已生效”，必须看**新端口**的应答。
5. 避免的坑：拿 `0x50` 当落盘证据；把测试值设成 10011/20103；改完不重启；改完仍往旧 IP 发。

### ④.4 需要抓回来的三样东西

1. **RTT 的 `[diag]` 段落**（横幅 + 体检全量；复测期间同一次抓包里带 `[gz_ol] 0x20/0x40` 行最好）。
2. **`arm-none-eabi-size -A build/Debug/Project_STD.elf` 的五段**（.text/.rodata/.data/.bss/.ccmram）+ 烧录物来源（哪条命令构建、是否 EIDE）。
3. **elf md5**：`md5sum build/Debug/Project_STD.elf`（**注意**：横幅带 `__DATE__/__TIME__`，每次重新编译 md5 都会变——所以 md5 只用于“与本次构建时的记录对拍”，镜像身份的权威证据是横幅的 `fw=`+`built=`+`tree=`）。

---

## ⑤ 构建结果（四条口径，2026-09-14 复测轮）

构建脚本 `.analysis/9k23881580/build_sweep_triage.sh`，日志 `build_sweep_triage.log` 与 `build_triage_*.log`。
**四口径全部链接通过、零新增告警**（每口径仅 3 条既有 `stm32f4xx_hal_flash_ex.c` `unused parameter` 警告）。

| 口径 | text | rodata | data | ccmram | bss | heap_stack | SRAM 合计 | SRAM 余量 | CCM 余量 | elf md5 |
|---|---|---|---|---|---|---|---|---|---|---|
| `PROTO=ALL` `DISP=1_263`（默认） | 167060 | 200712 | 1672 | 37084 | 126400 | 2560 | **130632（99.7%）** | **440B** | 28452B | `75ee3576…` |
| `PROTO=CQ` `DISP=1_263` | 153884 | 200056 | 872 | 37084 | 122924 | 2564 | 126360 | 4712B | 28452B | `24e5903b…` |
| `PROTO=ALL` `DISP=22_1703` | 167316 | 200792 | 1752 | **39164** | 126400 | 2560 | **130712（99.7%）** | **360B** | 26372B | `f1dd41ac…` |
| `PROTO=CQ` `DISP=22_1703` | 154140 | 200144 | 952 | **39164** | 122924 | 2564 | 126440 | 4632B | 26372B | `87a0fe7c…` |

- **工作区最终态**：默认口径（`make -j8`，诊断 ON）重建完成并留在盘上；重复 `make -j8` 为
  **0 编译 0 链接、md5 不变**（口径指纹 stamp 正常）。最终产物：elf `57092a95337eb32d8b49e9583bd366a7`、
  hex `1caefff99d86051d4aba1f661e2c18ec`（**注意**：上文表中的 md5 是四条口径各自那一轮构建的值；
  由于横幅内嵌 `__DATE__/__TIME__`，**重新编译会改 md5**，同口径两次构建 md5 不同属预期）。
- **诊断设施增量（A/B，默认口径）**：Flash +5120B（text +2464 / rodata +2656），RAM +0B（见 §③.3）。
- **`DISP=22_1703` 口径 ccmram = 39164B**：对应 `dev_display_22_1703.c` **当前工作区态（7×4 = 224×64）**，
  与上一轮报告记的 `11420`（= 31648−27744，1×1 = 32×16 实验态）**不再相同**——该文件为用户实验态，
  其宏一变本行就变（`ccmram` 差值 27744B 可用于**反推板上镜像的几何**，见 §④.1）。
- 参考（修复前基线，同日 17:05 采）：默认口径 text 164756 / rodata 198384 / bss 126392 / ccmram 37084；
  当前 text/rodata 的差异 = 字形裁剪 + GZ_OL 专用 UDP 口 + 诊断设施（后者的净增量即 +5120B）。

---

## ⑥ 文档更新（本轮）

| 文档 | 位置 | 更新 |
|---|---|---|
| `doc/14_贵州治超屏协议/README.md` | §12.4 | 诊断输出行格式按代码实态更正（`glyph_visible=WxH of WxH` / `size-map` / `pixel_map before|post render`），补 `font probe` 行 |
| 同上 | §12.7（新增） | **「症状复现条件与判别清单」**（问题①②的枚举表 + 「在什么条件下必然出现」判定） |
| 同上 | §12.8（新增） | **「一条定案诊断」**：横幅 + 逐帧诊断的期望输出、判读表、增量（+5120B Flash / +0B RAM）、一键关（`make APP_DIAG=0`） |
| 同上 | §13.4 | 端口复测步骤补「新端口实发 `0x60`/`0x10`」与 SKIPPED/FALLBACK 判读（不动结论） |
| `doc/CLAUDE.md` | 选编口径小节 | EIDE Debug 实态更正：`eide.yml` 当前**排除 1-263、编入 22-1703**（用户在 15:32 改）；EIDE Debug 含 GZ_OL + `CH_ID_UDP_GZOL` + 裁剪 + 诊断横幅；`PROTO_CHONGQING` 在 defineList |
| 同上 | 诊断小节（新增要点行 + 文档地图/构建开关段落） | 增加 `APP_DIAG_BANNER` / `make APP_DIAG=0` / `GZ_OL_RTT_DIAG` 说明与 `app_diag.h` 引用 |
| `doc/构建开关总表.md` | §1 表格 + §5 修订记录 | 新增 `APP_DIAG_BANNER`（诊断总开关，默认 1，`make APP_DIAG=0`）与 `GZ_OL_RTT_DIAG`（跟随总开关）两行 |
| `doc/06_SRAM内部分数据迁移/04_current_memory_occupancy.md` | §7.2 | 用本轮四口径复采值替换（含 `DISP=22_1703` 的 ccmram = 39164 与几何说明），清掉 `[待更新]` 标记 |

---

## ⑦ 待用户提供的信息（决定下一步）

1. **板上镜像的身份**：烧录后 RTT 的 `[diag] fw=... built=... tree=...` 行 + `display screen=WxH` 行
   （**若完全没有 `[diag]` → 直接判定“旧镜像未重烧”**）。同时给出本次构建用的命令与 `md5sum build/Debug/Project_STD.elf`。
2. **两个复测的原始证据**：
   - 24/32：**每帧的 `[gz_ol] 0x20` 全量行**（含 `font probe` 的 `rd/nz` 与 `post-render nonblack`）+ 上位机实际发送的报文字节（尤其 Y 与字号两字节）；
   - 端口：`0x40` 的 `[gz_ol] 0x40` 行 + 重启后横幅/体检的 `netcfg readback port=` 与 `chan gzol=... bind=` + **向新端口实发** `0x60`/`0x10` 的应答抓包。
3. **台架物理事实**：屏体实际是几块什么模组（P10 32×16 单块？22-1703 7×4 整屏？1-263 7×2？），
   以及本轮期望的**目标口径**（要验 224×64 全屏效果，还是单模组台架只看上部）——这决定 `DISP` 该用哪个值、
   以及“露出上部”是否算通过。

---

## 附：与本报告相关的既有材料（不重复调查）

- `.analysis/9k23881580/gz_ol_font_size_bugfix.md`：上一轮 24/32 复盘（当时板上 = EIDE 16:02 产物 + 1×1 = 32×16）。
- `.analysis/9k23881580/gz_ol_port_setip_diagnosis.md`：上一轮端口语义不匹配判定（端口值落盘、只被 TCP 业务口消费）。
- `.analysis/9k23881580/gz_ol_udp_service_port_report.md`：专用 UDP 服务口落地（`CH_ID_UDP_GZOL`，必须重烧才生效）。
- `.analysis/9k23881580/glyph_clip_sim.py`：字形裁剪的宿主穷举验证脚本（16962 位图层 + 44 渲染层用例）。
- 本报告：`.analysis/9k23881580/field_triage_2432_and_port.md`（复测轮三角复核 + 定案诊断 + 现场清单）。

---

## ⑨ 复测轮 ② 追加（2026-09-14 晚）：「改端口 10028 仍无响应」

用户 17:56 反馈：症状只剩端口一项——`0x40` 改端口为 **10028** 后向 **10028** 发数据无响应，
向 **10011** 正常。**代码链逐段复核无缺陷**（0x40 解析/落库字段/启动应用/专用实例 bind/分发/回源，
行号证据见 `.analysis/9k23881580/port_10028_diagnosis.md` §2）；**唯一代码侧的「端口静默回滚」机制**
= 启动 `app_net_boot.c` `accept_write`（记录 CRC 坏 / IP 全 0 / port=0 → 默认值同时覆盖 IP 与 port）。
本轮把「板上实际状态」做成一次可读出的判定：**四态表（A 旧镜像 / B 启动回滚 / C 设计分支 / D 网络侧）**
+ 三行新增诊断（0x40 原始载荷 dump、横幅 `expect gzol`、启动 `netcfg INVALID` 告警）+
一次完整现场清单（十六进制帧 / RTT 判读行 / TCP-UDP 对照矩阵）→ 见
`.analysis/9k23881580/port_10028_diagnosis.md` §3/§4/§7。

> §③/§④ 的判读表仍有效；「没有 `[diag]` 行 = 板上不是本代码」这一条**依然是第一判据**。
