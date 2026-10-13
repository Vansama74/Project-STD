# DIP2（PE8）「字库选择不生效」取证报告（只读调查）

- 日期：2026-10-12（本报告生成时点：13:59 构建 + 烧录之后）
- 方式：**只读**。未改任何代码/配置，未做 git 写操作，未 `make clean`，未动 `build/` 既有产物（仅 `md5sum`/`nm`/`objdump` 读取），未烧录。
- 相关入口：`Application/Src/app_render.c`（字库芯片选择）、`Application/Src/app_key.c`、`Device/IO/dev_key.c`、`Core/Src/gpio.c`、`Platform/Src/pl_gpio.c`、`Platform/Src/pl_hub75.c`、`.eide/eide.yml`、`build/Debug/builder.params`。

---

## 0. 结论摘要（先给答案）

1. **DIP2 的字库选择在代码里是「上电（sw initcall）读一次」的语义，运行中拨动不会重读**——必须在**复位/断电重启**后才生效；且它是**直读 PE8 引脚电平**（`dev_key_get_state()` → `!pl_gpio_read()`），既不回去抖缓存，也**没有任何轮询/重读/持久化**。全仓唯一写 `s_active_config` 的地方就是 `_render_init()` 里那一次 `_dip_font_select()`。
2. **代码侧 PE8 干净**：全仓只有 DIP2 用 PE8（`DIP_SW2_Pin=GPIO_PIN_8 / DIP_SW2_GPIO_Port=GPIOE`）；HUB75/显示驱动全部走 `pl_hub75.c` 的共享引脚表（R1..R10/G/B 里没有 PE7/PE8）；缅甸模块的 PE12/PE11/PE10 接管在本轮 Debug 镜像里根本不存在。用户未提交的 `dev_display_1_263.c / dev_display_1_260.c / app_boot.h / .eide/eide.yml` 改动**都不涉及 PE8**。
3. **最近三次提交无关**：`c624915 → cd9ce31 → c61c895` 两次注释工程**没有碰** `app_render.c / app_key.c / dev_key.c / gpio.c / pl_gpio.c / pl_hub75.c`（`git diff --stat` 为空）；被改的 17 个工程源文件经「去注释后逐字节比对」确为 **comment-only**（"零代码变化"成立）。
4. **板上镜像是今天 13:59:43 的 EIDE 构建**（`flash_all.sh` 13:59:49 刷 `build/Debug/Project_STD.hex`，日志 `Flash SUCCESS` + 擦 Sector1）：`PROGRAM_CODE=9K2F3317E0`、**只编 `dev_display_1_263.c`**、且该驱动当前宏是 **1×1（32×32 画布）**。elf md5 = `33e888bf40e71e768d3923272135320f`。
5. **最可能被当成"DIP2 不生效"的四类原因（按可能性）**：① 运行中拨动没重启（设计行为）；② PE8 电平恒定（开关/接线硬件）；③ 观察对象不经字库（出厂测试/协议位图）或"只有 32×32 有画面"被误判为字库错；④ 文档与代码**方向相反**造成的期望错位（`doc/02` §7.3 + `app_render.c` 文件头说 OFF→W25Q64，**代码实际是 OFF→MX25L256**）。详见 §6。

---

## 1. 实现路径与采样时机（含 file:line）

### 1.1 读引脚 → 选配置（链路完整）

| 环节 | 位置 | 说明 |
|---|---|---|
| 引脚定义 | `Device/IO/dev_key.c:67` | `g_dip2 = {.id=DEV_KEY_DIP2, .port=PL_PORT_E, .pin=8, .active_low=true}` |
| 读逻辑 | `Device/IO/dev_key.c:48-52` | `_dip_get_state()` = **`!pl_gpio_read(key->port, key->pin)`**（active_low 取反） |
| 表填充 | `Device/IO/dev_key.c:98-119` | `dev_key_init()`（`hw_dev_initcall`，RTOS 前）填 `s_keys[DEV_KEY_DIP2]` 并绑 `dip_key_ops` |
| 硬件读 | `Platform/Src/pl_gpio.c:40-45` | `pl_gpio_read()` = `(GPIOx->IDR & (1U<<pin)) != 0`（PL_PORT_E → GPIOE） |
| 便捷 API | `Device/Inc/dev_key.h:58-62` | `static inline dev_key_get_state(id)` → `ops->get_state(k)`（**直读，无缓存**；`k` 为空返回 false） |
| 选择逻辑 | `Application/Src/app_render.c:359-366` | `_dip_font_select()`：`if (dev_key_get_state(DEV_KEY_DIP2)) → FONT_CHIP_W25Q64; else → FONT_CHIP_MX25L256` |
| 切换写点 | `Application/Src/app_render.c:380-386` | `app_render_chip_select()` 只改 `static const font_chip_config_t *s_active_config`（`app_render.c:185`） |
| 消费者 | `Application/Src/app_render.c:258-260`（字符地址）、`:335-336`（自适应字号） | 渲染取址 = `_flash_addr_cfg(s_active_config, …)`；自适应候选 = `s_active_config->adaptive_*` |

**映射关系（以代码为准）**：

```
DIP2 开关 ON  → PE8 被拉低 → pl_gpio_read=false → get_state()=true  → FONT_CHIP_W25Q64
DIP2 开关 OFF → PE8 上拉为高 → pl_gpio_read=true  → get_state()=false → FONT_CHIP_MX25L256
```

### 1.2 采样时机：**只在开机执行一次**

- `Application/Src/app_render.c:368-376`：

```c
static void _render_init(void)
{
    s_render_display = dev_display_get();
    s_render_font    = dev_w25qxx_get();
    s_persist_addr   = dev_storage_capacity(s_render_font) - 4096 * 2;
    _dip_font_select();          /* ← 全仓唯一调用点 */
}
sw_app_initcall(_render_init);
```

- `grep` 结果（全仓）：`_dip_font_select` 仅此一处调用；`app_render_chip_select` 的调用者也只有它（`Application/Src/app_render.c:362,364`）。
- **运行期无任何重读**：无轮询任务、无周期函数、无按键触发。
- `app_render.h:125-128` 提供的运行时切换 API（"可在任意时刻调用，立即生效"）**全仓无消费者**（只有声明与实现）。
- 字库选择**不持久化**（flash 里没有"上次选择"记录；`s_persist_addr` 属于另一套未启用的显示持久化 API，见 §9）。
- ⇒ **运行中拨动 DIP2 一定不生效，必须复位/断电重启**（这一点在 `app_render.c` 文件头第 5 行「上电时…」与 `doc/02` §2.3「上电时 `_render_init()` 自动读取」均有文字描述，属设计行为）。

### 1.3 镜像级证据（当前 `build/Debug` elf，13:59:43 构建）

反汇编 `_render_init`（0x08046d00，`arm-none-eabi-objdump -d`）：

```
8046d2a: bl dev_key_get            ; dev_key_get(5) = DEV_KEY_DIP2
8046d36: blx r3                    ; → _dip_get_state()（间接调用 ops->get_state）
8046d38: cbz r0, 8046d4a           ; r0==0（get_state false）→ MX25L256 分支
8046d3a: ldr r3,=0x2000033c        ; s_active_config
8046d3e: str r2(0x080674bc)        ; = &s_chip_configs[FONT_CHIP_W25Q64]
...
8046d4a: str r2(0x080674d0)        ; = &s_chip_configs[FONT_CHIP_MX25L256]
```

- `_dip_get_state`（0x0804927c）：`pl_gpio_read(port,pin)` → `eor r0,#1`（取反）→ 返回。
- `s_chip_configs` @ `0x080674bc`（0x28 = 2 项 × 20B）；`s_active_config` @ `0x2000033c`。
- **全 elf 中 `0x2000033c` 只有 3 处引用**（`_render_init` 写、`_render_text` 路径读 ×2）⇒ 运行时不会被任何其它模块改写。
- 符号齐备：`dev_key_init`(0x080490f8, `hw_initcall_2`)、`__sw_initcall_3__render_init`(0x0807d598)。

### 1.4 与 DIP1（波特率，已知能工作）的读法对比

| | DIP1（波特率） | DIP2（字库） |
|---|---|---|
| 位置 | `Application/Src/app_uart_baud.c:54` | `Application/Src/app_render.c:361` |
| API | `dev_key_get_state(DEV_KEY_DIP1)` | `dev_key_get_state(DEV_KEY_DIP2)` |
| 时机 | `sw_app_initcall`（`:57`）**只读一次** | `sw_app_initcall`（`:376`）**只读一次** |
| 引脚 / 端口 | PE7 / GPIOE，上拉低有效 | PE8 / GPIOE，上拉低有效 |
| 运行中切换 | 支持（另有 `app_uart_baud_apply()`，MTC `7B 40` 用） | **不支持**（无重读、无消费者） |

**推论**：两条链路的"读引脚方法/时序"完全同构。若 DIP1 实测有效而 DIP2 无效，**问题不在读法**，只可能在 ① PE8 侧硬件（开关/焊接/被外部拉死）② 观察方式（没重启 / 画面不经字库）③ 镜像与源码不一致（§5）。

---

## 2. 初始化顺序核实（"去抖缓存未初始化"假设不成立）

`.sw_initcall` 实测执行序（`arm-none-eabi-nm -n`，地址升序 = 调用序）：

```
0x0807d530 __sw_initcall_2__app_flash_ldi_storage_init
0x0807d538 __sw_initcall_2_dev_display_start
0x0807d540 __sw_initcall_2_dev_key_sw_init
0x0807d548 __sw_initcall_2_dev_w25qxx_rtos_init
0x0807d550 __sw_initcall_3_iap_module_init
0x0807d558 __sw_initcall_3_ldi_module_init
0x0807d560 __sw_initcall_3_sc_etc_proto_init
0x0807d568 __sw_initcall_3_sc_mtc_proto_init
0x0807d570 __sw_initcall_3_sc_ol_proto_init
0x0807d578 __sw_initcall_3_app_dispatch_init
0x0807d580 __sw_initcall_3__factory_test_init
0x0807d588 __sw_initcall_3__app_key_init      ← DIP 快照（app_key.c）
0x0807d590 __sw_initcall_3_app_light_sensor_init
0x0807d598 __sw_initcall_3__render_init       ← _dip_font_select()
0x0807d5a0 __sw_initcall_3_app_uart_baud_init
```

- `dev_key_init()`（键表就绪）是 **hw_dev（RTOS 前，`main()` 内）** ⇒ 任何 sw 阶段读都有效。
- `_app_key_init` 先于 `_render_init` 执行；**但 `app_render` 用的是直读引脚（`dev_key_get_state`），根本不经过 `app_key` 的缓存** ⇒ 二者先后对 DIP2 结果无影响。
- **顺带发现（文档漂移，非缺陷）**：`Application/Src/app_key.c` 现在**没有** 20 ms 去抖轮询任务（全文只有 `s_dip1_state/s_dip2_state` 两个快照 + `sw_app_initcall(_app_key_init)`；`app_key_get_state(DEV_KEY_DIP2)` 全仓无消费者）。`doc/CLAUDE.md:862-863` 写的「`app_key` 纯轮询 (3 次一致去抖)」「DIP2=配置选择」是旧口径。

---

## 3. PE8 引脚占用排查（穷尽清单，代码侧干净）

### 3.1 定义与配置

- `Core/Inc/main.h:120-121`：`DIP_SW2_Pin GPIO_PIN_8 / DIP_SW2_GPIO_Port GPIOE` —— **PE8 全仓唯一用途**。
  （其它 `GPIO_PIN_8` 分别是：`HUB75_B9`=**PF8**、`KEY_TST`=**PD8**、`RS485_RE`=**PA8**、`HUB75_R3`=**PB8**，与 PE8 无关。）
- `Core/Src/gpio.c:116-120`：`MX_GPIO_Init()` 把 `DIP_SW1|DIP_SW2` 配成 **`GPIO_MODE_INPUT` + `GPIO_PULLUP`**（无 EXTI）。
- `Core/Src/gpio.c` 再无 PE8 相关语句；`pl_gpio_init()`（`Platform/Src/pl_gpio.c`，`hw_pl_initcall`）就是调用 `MX_GPIO_Init()`，全程序只调用一次。

### 3.2 显示 / HUB75 侧（重点怀疑对象）

- `Platform/Src/pl_hub75.c:8-46` 三张表 `g_hub75_pin_r/g/b[HUB75_CHANNEL_MAX]` 全部由 `main.h` 宏拼出：R1..R10（PG9/PG15/PB8/PE1/PE4/PF0/…）、G/B 同理，**没有 PE7/PE8**。
- 7 个显示驱动逐个核对（`grep -c "g_hub75_pin"` / `grep -c "GPIO_PIN_8"`）：

  | 驱动 | 引用共享表 | 直写 `GPIO_PIN_8` |
  |---|---|---|
  | `dev_display_1_263.c` | 6 | 0 |
  | `dev_display_1_260.c` | 9 | 0 |
  | `dev_display_1_577.c` | 6 | 0 |
  | `dev_display_1_969.c` | 6 | 0 |
  | `dev_display_22_1703.c` | 4 | 0 |
  | `dev_display_22_1665.c` | 20 | 0 |
  | `dev_display_p20.c` | 9 | 0 |

  ⇒ 显示层不可能把 PE8 配成输出或复用（`pl_hub75.c` 自身只写 OE/LAT/CLK/A-D，均在 GPIOD）。
- 扫描/建表只写各自 R/G/B 的 `BSRR` 字（`pl_hub75_bsrr_flush`），端口互不相同也不会碰 GPIOE bit8。

### 3.3 全仓 PE8 / GPIOE 写入者

- `grep -rn "PL_PORT_E"`（Application/Device/Platform）：只有 `dev_key.c`（DIP1/DIP2/SW1-3）与 `pl_gpio.c` 的端口基址表。
- `grep -rn "GPIOE->|HAL_GPIO_WritePin(GPIOE"`：除 `Core/Src/system_stm32f4xx.c` 的启动期 `GPIOE->AFR/MODER/OSPEEDR/OTYPER/PUPDR` 初始化块（STM32Cube 生成的 `SystemInit` 分支，本工程 `SystemInit()` 走 FPU 分支、不执行这些 GPIO 复位值写入）外，**无任何 GPIOE 直接写**。
- `pl_gpio_config_output()` / `pl_gpio_release_exti()`（`Platform/Inc/pl_gpio.h:41,60`）**唯一消费者 = 缅甸模块** `app_mm_proto_cmd.c:92,94`（PE12/PE11/PE10 干接点接管 + 逐线摘 EXTI）；缅甸模块在当前 Debug 目标 `excludeList` 内，不在板上镜像中。且即使编入，其 `pl_gpio_release_exti` 是**逐线**操作（`EXTI->IMR/EMR/RTSR/FTSR` 按位清 + 写 1 清 `EXTI->PR` + 条件清 `SYSCFG->EXTICR`），不会波及非 EXTI 的 PE8。
- `pl_exti.c` 只注册 SW1/2/3（PE12/11/10）与 KEY_TST（PD8）四条回调，PE8 无 EXTI。

### 3.4 用户未提交改动的逐条核对（重点怀疑对象已排除）

| 文件 | diff 实质 | 与 PE8 / 字库的关系 |
|---|---|---|
| `Device/Display/dev_display_1_263.c` | 仅 1 行注释（"P6 户外全彩（兼容1-561）"） | 无关 |
| `Device/Display/dev_display_1_260.c` | 几何宏 `MODULE_ROWS 1→7`、`MODULE_COLS 1→2`（1×1 → 7×2；`TOTAL_CHANNELS` 2→4、buffer 1024→14336B） | **不涉 PE8**；且 1_260 在 `targets.Debug` 与 `targets.Myanmar` **两个 excludeList 内 ⇒ 两份镜像都不编**（改动静默） |
| `Application/Inc/app_boot.h` | `PROGRAM_CODE "9K12D40100" → "9K2F3317E0"` | 仅版本号（开机画面 / IAP 0x03）；与 DIP2 无关，但可作**镜像指纹**（§5） |
| `.eide/eide.yml` | `targets.Debug.excludeList`：去掉 `dev_display_1_263.c`、`dev_display_1_260.c` 两行；末尾新增 `dev_display_1_969.c` + `dev_display_1_260.c` | 净效果 = **Debug 由「编 1_969」改为「编 1_263」**（恰好 1 个显示驱动）；未动 `app_render.c` / `dev_key.c` |
| `.analysis/comment_pass_20261008/README.md`、三个协议模块、doc/14/15/16/18 | refine 二轮产物（注释/文档） | 无关（§4 已验证 comment-only） |

---

## 4. 最近三次提交核实（"注释工程零代码变化"成立）

- `git diff --stat c624915..HEAD -- Application/Src/app_render.c Application/Src/app_key.c Device/IO/dev_key.c Core/Src/gpio.c Platform/Src/pl_gpio.c Platform/Src/pl_hub75.c Application/Src/app_uart_baud.c` → **空**（这 7 个文件在两次注释提交里一次都没被动过；各自最后的改动提交分别是 `a039b76`(app_render)、`228f307`(app_key/dev_key)、`dea8131`(gpio.c)、`c624915`(pl_gpio)、`613efe1`(pl_hub75)）。
- 两次提交共 113 文件；其中工程 `.c/.h` 仅 17 个（云南治超 6 / 贵州治超 7 / 缅甸 4 / `app_factory_test_mm.c` / 3 个模块头文件）。对每个文件做「`gcc -fpreprocessed -dD -E -P` 去注释后 md5 比对」：

```
comment-only: Application/Inc/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.h
comment-only: Application/Inc/ProtocolParser_Myanmar/app_mm_proto.h
comment-only: Application/Inc/ProtocolParser_YunNan_Overload/app_yn_ol_proto.h
comment-only: Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto.c
comment-only: Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_cmd.c
comment-only: Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_default.c
comment-only: Application/Src/ProtocolParser_GuiZhou_Overload/app_gz_ol_proto_parse.c
comment-only: Application/Src/ProtocolParser_Myanmar/app_mm_proto.c
comment-only: Application/Src/ProtocolParser_Myanmar/app_mm_proto_cmd.c
comment-only: Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto.c
comment-only: Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_cmd.c
comment-only: Application/Src/ProtocolParser_YunNan_Overload/app_yn_ol_proto_parse.c
comment-only: Application/Src/app_factory_test_mm.c
```

（同一命令对 `.analysis/comment_pass_20261008/archive/**` 下的归档副本报 CODE-DIFF —— 那些是**轮次改前快照**，按定义与源码不同，不是工程源文件，不计入。）

- **结论**：DIP2 现象与 `cd9ce31 / c61c895` 两次提交无关；也与「冲突修复落地」轮（`app_boot.c`/`app_dispatch.c`/`app_ldi_cmd.c`/`app_net_boot.c`/`app_default_display.c`/`dev_display_22_1665.c`）无关——那些文件都不参与 DIP2 读链。
- 全仓 `_dip_font_select()` 自 `a039b76`（本仓首个提交，2026-08 前后）以来**从未变过**（`git log -L '/_dip_font_select/,/^}/:Application/Src/app_render.c` 只有首次引入；各分支 `yy / origin/yy / upstream/yy / dev-local` 内容逐字相同；更老的 `origin/main / origin/wyh / lj / feat/old_font_lib` 里还没有双字库机制）。

---

## 5. 构建 / 镜像口径（板上跑的是哪一份？）

### 5.1 现状（`build/Debug`，今天 13:59）

| 项 | 值 | 证据 |
|---|---|---|
| 构建方式 | **EIDE**（`.build_stamp` 仍是 2026-09-29 的 make 残留） | `stat build/Debug/.build_stamp` |
| 产物时间 | `Project_STD.elf/.hex/.bin` = **2026-10-12 13:59:43** | `stat` |
| 源清单 | `build/Debug/builder.params`（13:59:42，EIDE 自己写）含 `app_render.c`、`app_key.c`、`app_uart_baud.c`、`Device/IO/dev_key.c`、**`dev_display_1_263.c`**；**不含** `dev_display_1_260.c / 1_969 / 22_1665`、不含 `ProtocolParser_Myanmar` | `grep builder.params`；`Project_STD.objlist` 205 条 |
| 段尺寸 | `.text 146704 / .rodata 103920 / .data 1608 / .ccmram 17260 / .bss 115960 / ._user_heap_stack 2560` | `arm-none-eabi-size -A` |
| md5 | elf `33e888bf40e71e768d3923272135320f`；bin `95c3e6889e7589c23f3993a171d66bdc`；hex `98141afaec2f4ecdc614473cda4b9d2f` | `md5sum` |
| 版本指纹 | elf 内含字符串 **`9K2F3317E0`**（= 当前工作区 `app_boot.h`，未提交值；`git HEAD` 的值是 `9K12D40100`） | `arm-none-eabi-strings … | grep` |
| 显示驱动 | 链接期只有 `dev_display_1_263.o` + 基类 `dev_display.o`（`.lnp`/`.map` 实证） | `grep dev_display … .lnp` |

### 5.2 板上镜像 = 这一份（终端记录实证）

- 终端日志（`flash_all.sh`，写入时刻 **13:59:49**，紧跟构建）：加载 `…/Project-STD-main/build/Debug/Project_STD.hex` → `J-Link: Flash download: … 1 range affected (262144 bytes)` → `erase 0x08004000, 0x08007FFF`（擦 Sector1 恢复出厂态）→ `Flash SUCCESS`。
- 另有 13:44:12 的一次 EIDE J-Link 调试会话（`JLinkGDBServerCLExe`）：下载了 ~253 KB 的镜像（initcall 段在 `0x0807D670/0x0807D710`，**比 13:59 构建低 32B**）、断点设在 `main`(≈0x08047DF8) 与 `app_rs232_start` 一带，并读取过 CCM 地址 `0x100039xx`（当前镜像里该区间是 `_1_263_hub75_buff`(0x100039ec) / `s_task_scan_tcb`(0x10003948) 一带）⇒ 用户当时正在**调试显示/启动路径**（与"DIP2 不生效"的观察场景吻合）。
- **判断"板上是哪一份"的方法（按可靠性）**：
  1. IAP `0x03` 上报的 version（= `PROGRAM_CODE`，当前应为 `9K2F3317E0`；若是 `9K12D40100` 或更早值 ⇒ 板上是旧镜像 / 未烧进新构建）；
  2. 开机 **5 s splash** 的 `FW:9K2F3317E0 / MD:1000000263` 两行（`app_boot.c:80-121`，用**当前激活字库**渲染 —— 既是镜像指纹，也是 DIP2 的最佳观察窗口）；
  3. `md5sum` / `arm-none-eabi-size -A` 对拍（EIDE 口径 ≈ 250 KB Flash；make 全协议口径 `.rodata≈195776`，两者一眼可分）；
  4. `arm-none-eabi-strings build/Debug/Project_STD.elf | grep <PROGRAM_CODE>`。
- **陷阱提醒**：EIDE 与 `make` **共用 `build/Debug`**（现状同一目录里既有 9-29 的 `.build_stamp` 又有 13:59 的 EIDE 产物）。EIDE 构建覆写产物后，同口径 `make` 可能 **0 编译 0 链接**静默复用；反之亦然。因此"板上是哪一份"**只能看 md5/尺寸/内嵌字符串，不能看目录或 mtime**。

---

## 6. 根因候选（按可能性排序，各自证据与判别方法）

### A. 运行中拨动 DIP2 未复位 —— **设计行为**（最可能被当成"不生效"）
- 证据：§1.2（唯一调用点、只在 `sw_app_initcall` 读一次、无重读/无轮询/不持久化）。
- 判别：**拨动 DIP2 → 断电（或按复位）→ 看 5 s 开机画面**。若一档出 `FW:/MD:` 正常、另一档花掉 ⇒ DIP2 完全正常，只是要求重启。若重启后两档一模一样 ⇒ 转 B/C/D。
- 反证要点：`app_render.h:125` 写着"可在任意时刻调用，立即生效"（指 `app_render_chip_select()` 这个 API 本身），容易被误读成"拨开关即时生效"。

### B. PE8 电平恒定（开关/接线/焊接硬件）
- 证据：读法本身没问题（DIP1 同法工作，§1.4）；代码侧无第二消费者（§3）。
- 判别：
  1. 万用表量 **PE8↔GND**：DIP2 OFF 应 ≈3.3 V、ON 应 ≈0 V（若两档都 3.3 V 或都 0 V ⇒ 开关/走线问题）；**同时量 PE7 做对照**（DIP1 已知有效）；
  2. 调试器 halt 后读 `GPIOE->IDR`（0x40021010）bit8，边拨边读；
  3. 若怀疑"丝印 DIP2 其实不是 PE8"，把两档分别读 PE7/PE8 交叉验证。

### C. 板上镜像不是当前源码那一份（旧镜像 / 烧录未生效 / 口径错）
- 证据：§5（13:59 构建 + 13:59 烧录；但 13:44 调试下载的是另一份 32B 更小的镜像）；EIDE/make 共用产物的陈旧覆盖陷阱。
- 判别：IAP 0x03 version / splash `FW:` 行 / elf md5 三选一与 §5.1 对拍。若 version ≠ `9K2F3317E0` ⇒ 先重新烧录（并遵守"烧完擦 Sector1"纪律，`flash_all.sh` 已默认做）。

### D. 文档与代码方向相反 → 期望错位
- **证据（本轮新发现，值得优先澄清）**：
  - `doc/02_LDI协议与外设接口/字库芯片更换与移植指导.md:54-70`（§2.3 表格）与 `:456-459`（§7.3 清单）写：**OFF → W25Q64、ON → MX25L256**；
  - `Application/Src/app_render.c:5-7`（文件头注释）同样写：**OFF → W25Q64、ON → MX25L256**；
  - 但实际代码 `Application/Src/app_render.c:361-364` **与其上一段注释 `:355-357`** 写的是：**ON（get_state=true）→ W25Q64、OFF → MX25L256**；
  - 且 `doc/02` §2.3 的 `get_state()` 列本身与实现矛盾：文档说"引脚 HIGH（上拉）→ 取反 → **true**"，而 `_dip_get_state()` 是 `!pl_gpio_read()` ⇒ HIGH 应得 **false**。
  - ⇒ 文档 + 文件头 vs 代码 + 函数内注释，**2:2 对立**，其中必有错；离线无法判定哪个符合实物（本仓自 `a039b76` 起就是 ON→W25Q64，更老分支无双字库机制，没有历史参照）。
- 判别（决定性）：调试器读 **字库 Flash 的 JEDEC id**（`dev_w25qxx` 实例的 `device_id`/`capacity`：W25Q64 ≈ `EF 40 17`/8 MB；MX25L256 ≈ `C2 20 19`/32 MB），再读 `s_active_config`（当前 elf @ `0x2000033c`；`0x080674bc`=W25Q64 表、`0x080674d0`=MX25L256 表）或调 `app_render_chip_current()`，对照"哪一档显示正常"即可定案。
- 附带量化（为什么"选错"一定看得出来）：两套表的同一字符地址相差甚远，例如 16pt GBK「迎」在 W25Q64 表 ≈ **159,104 (0x26D80)**、在 MX25L256 表 ≈ **3,194,848 (0x30C0E0)**；且 **W25Q64 表 32pt GBK 尾部会寻址到 ~9.7 MB**、**MX25L256 表尾部 ~29.3 MB** ⇒ 若板上字库是 8 MB 芯片却选了 MX25L256 表，读地址越出容量（`dev_w25qxx._read_locked` 无容量边界检查，3 字节地址回绕）⇒ 出**错字/实心块**，绝不该"毫无变化"。

### E. 观察对象不经过字库（易误判）
- 出厂测试（TEST 键：七色满屏 / 斜扫 / 老化循环）、协议位图帧（RLS bitmap、安徽点命令）、纯色填充都不会读字库 ⇒ 此时拨 DIP2 当然"没变化"。
- 判别：**只看 5 s 开机画面 `FW:/MD:` 两行**（必走字库），或发一条走 `app_render` 的文本帧。

### F. 当前镜像的显示几何（32×32）导致"画面不对"被误当成字库问题
- 证据：板上镜像编 `dev_display_1_263.c`，其宏 **`_1_263_MODULE_ROWS=1 / _1_263_MODULE_COLS=1` ⇒ 画布 32×32**；镜像级佐证：`_1_263_pixel_map` 0x10003dec → `_1_263_hub75_buff` 0x100039ec 相差 0x400 = **1024 B = 32×32**，`_1_263_row_dst` = 64 B = 32 行。
- 后果：若实物屏是 224×64（7×2 拼屏，见 `doc/01`「口径更正 2026-09-14」），**只有左上角 32×32 有内容**；`_render_welcome()`（`Application/Src/app_default_display.c`，FONT_SELF_ADAPT 兜底 FONT_16、居中）在 32 宽画布上会把 64 宽的整行**裁掉左右各 16 px**（`dev_display.c` 的交集裁剪），看起来像"字库/字显示不全"。
- 用户的未提交改动与这条互相印证（`dev_display_1_260.c` 改成 7×2 = 224×64、Debug 换到 1_263、1_263 注释加"兼容1-561"）⇒ 用户很可能正在找"与实物屏匹配的驱动几何"。
- 判别：先确认屏体实际分辨率与驱动宏是否一致（改宏必须重编重烧；`DISP` 口径见 `doc/构建开关总表.md`）。

### G. 已排除项
- **PE8 被别的模块占用/复用/摘 EXTI**：§3（显示层不用 PE8；缅甸 PE12/11/10 且不在镜像内；`pl_gpio_release_exti` 逐线）。
- **去抖缓存未初始化**：§2（`app_render` 直读引脚；且 `_app_key_init` 先于 `_render_init`）。
- **注释工程/冲突修复轮引入代码变化**：§4（comment-only 实测；关键文件未被触碰）。
- **HUB75 通道表扩展吃掉 PE8**：`pl_hub75.c` 十通道表内无 PE7/PE8（§3.2）。
- **字库选择被持久化覆盖**：无持久化；`app_render_save/restore`（`app_render.h:214-218`）无消费者（§9）。

---

## 7. 现场验证步骤（按顺序执行，每步给出"判据"）

1. **先验证"要不要重启"**：设定 DIP2 某一档 → **断电/复位** → 盯开机 5 s 内 `FW:9K2F3317E0 / MD:1000000263` 两行 → 换另一档再断电/复位，重复。
   - 判据：两档画面不同（一档正常、一档花/错字）⇒ **DIP2 功能正常**，此前的"不生效"= 运行中拨动（候选 A）。两档完全相同 ⇒ 继续下一步。
2. **确认板上镜像**：读 IAP `0x03` 上报 version（应为 `9K2F3317E0`），或对拍 `md5sum build/Debug/Project_STD.elf` = `33e888bf40e71e768d3923272135320f`；不一致 ⇒ **重新烧录**（`bash tool/flash_all.sh`，默认擦 Sector1），烧完重做第 1 步。
3. **量 PE8 电平**：万用表/示波器量 PE8（或 `GPIOE->IDR` bit8 = `0x40021010`）在两档下的电平，同时量 PE7 做对照。
   - 判据：PE8 必须一档 0 V、一档 3.3 V；若恒定 ⇒ 硬件/开关问题（候选 B），查 DIP2 开关、PE8 走线焊点、是否被外部电路拉死。
4. **读选择结果**：调试器看 `s_active_config`（当前 elf `0x2000033c`）或调 `app_render_chip_current()`；两档应分别为 `0x080674bc`(W25Q64) / `0x080674d0`(MX25L256)。
   - 判据：若 PE8 有变化而 `s_active_config` 不变 ⇒ 镜像/编译问题（回到第 2 步）。
5. **读字库芯片型号**：调试器看 `dev_w25qxx` 实例的 `device_id`（JEDEC）与 `capacity`（W25Q64≈`0xEF4017`/8 MB；MX25L256≈`0xC22019`/32 MB）→ 与第 4 步结果对照，判定"哪一档才应显示正常"，从而裁决 `doc/02` 与代码谁的方向对（候选 D）。
6. **确认屏体几何**：当前镜像 = `1-263 @ 1×1 = 32×32`。若实物屏是 224×64（7×2），先改驱动宏（`dev_display_1_263.c` 的 `_1_263_MODULE_ROWS/COLS`）重编重烧再谈字库——否则会在"只有 32×32 有内容"的画面上做字库判断（候选 F）。
7. **不要用出厂测试画面判断**：TEST 键序列（七色/斜扫）与协议位图帧不经字库（候选 E）。
8. 需要更细的观测时，可临时在 `_dip_font_select()` 之后加一行 `SEGGER_RTT_printf(0, "dip2=%d chip=%s\n", …)`（RTT 底层在编译清单内、业务代码当前 0 调用；用完即删，属临时手段不落盘）。

---

## 8. 若判为代码缺陷：最小修复方案与风险（仅方案，未实施）

### 方案 1：映射方向与实物不符 → 只换两个分支（1 行级）
- 改动：`Application/Src/app_render.c:361-364` 把两个 `app_render_chip_select(...)` 互换，并同步修正文件头 `:5-7`（以及 `doc/02` §2.3/§7.3）。
- 风险：**必须先在台面用 JEDEC/实测确认实物字库芯片**；改反了会把"出厂默认档"选错，影响所有已部署板（属高风险、需全量回归）；若只是本板特例，不应改源码。

### 方案 2：要求"运行中拨动即生效" → 增加受控重读（+ 重绘策略）
- 改动：把 `_dip_font_select()` 包成 `void app_render_dip_refresh(void)`（`app_render.h` 声明），由一个低频周期点调用（≥500 ms，建议复用现成的 1 s `app_light_sensor` 任务或新建"只创建一次"的静态栈任务，栈落 CCM 以合项目内存纪律）；仅在"与上次采样值不同"时切换。
- **必须同时定义重绘策略**：只切 `s_active_config` 不会重画当前画面（下一帧用新表），要"看得见"就得重渲染（可能与协议内容冲突）——这也是当初采用"上电一次"设计的原因。**建议先做需求裁决再动代码。**
- 风险：低频轮询引入新任务/新代码；切换瞬间正在渲染的一帧字形可能混合两套表（对单帧无严重后果，但需在文档写清）。

### 方案 3（推荐长期）：用 JEDEC/capacity 给 DIP2 做**合理性校验 + 兜底**
- 改动：`_dip_font_select()` 里在按 DIP2 选完之后，用 `dev_storage_capacity(s_render_font)`（`dev_w25qxx` 已识别 JEDEC）做一次越界校验：若所选配置的**最大寻址 > 容量**（W25Q64 表尾 ~9.7 MB、MX25L256 表尾 ~29.3 MB，见 §6-D），则自动回退到另一档 + 一次性 RTT 告警。
- 优点：不依赖 DIP 极性判断，直接消除"选到容量装不下的表"这一**必然出错**的组合；且保留 DIP2 作为主选择（容量不能唯一决定字库布局，只能兜底）。
- 风险：极小；需注意容量相同但布局不同的板子仍需 DIP2 正确。

### 方案 4（零风险可观测性）：临时 RTT 一行
- 见 §7 第 8 步；不改变行为，仅用于现场取证（项目 2026-09-29 起自研诊断设施已删，业务代码零 RTT 调用，加一行需自行删除）。

---

## 9. 附：本轮顺带发现（只读记录，未改文档）

1. **文档漂移（建议后续同步）**
   - `doc/CLAUDE.md:862-863`：IO 表把 DIP2 用途写成「配置选择」、把 DIP1/DIP2 写成「`app_key` 纯轮询 (3 次一致去抖)」——与现行代码不符（`app_key.c` 无轮询任务，仅上电快照；DIP2 = `app_render` 字库芯片选择）。
   - `doc/02` §2.3 的 `get_state()` 列与 `_dip_get_state()` 的取反语义相反；§7.3 与 `app_render.c` 文件头（:5-7）的 DIP2↔芯片对应关系与代码（:361-364）相反（§6-D，需硬件裁决后统一）。
   - `doc/08_协议模块接入规则/02_串口协议接入.md:34-38` 关于 DIP1/DIP2 的说明与代码一致（但依赖 §2.3 的口径）。
2. **死 API**：`app_render_save()` / `app_render_restore()`（`Application/Inc/app_render.h:214-218`，实现 `app_render.c:767-836`）全仓无消费者；`s_persist_addr = capacity - 8192` 因此也不会被写（与字库选择无关，仅为记录）。
3. **`app_key.c` 的 DIP 快照无消费者**：`app_key_get_state(DEV_KEY_DIP1/DIP2)` 只被 `app_rls_cmd.c:190-192`（SW1/2/3）之外的地方使用——DIP 两项实际无人读，纯调试观察点（调试器里看 `s_dip2_state` @ `0x200047dc` 可作参考值）。

---

## 10. 本轮执行的关键命令（可复跑）

```bash
# 实现与调用点
grep -rn "DEV_KEY_DIP2\|_dip_font_select\|app_render_chip_select" --include=*.c --include=*.h Application Device Platform
# 镜像级证据
arm-none-eabi-objdump -d --start-address=0x08046d00 --stop-address=0x08046d80 build/Debug/Project_STD.elf
arm-none-eabi-objdump -d --start-address=0x0804927c --stop-address=0x0804929c build/Debug/Project_STD.elf
arm-none-eabi-nm -n build/Debug/Project_STD.elf | grep "__sw_initcall\|_1_263_\|s_active_config\|g_dip2"
arm-none-eabi-objdump -d build/Debug/Project_STD.elf | grep -c 2000033c      # = 3（无第二写点）
# 引脚占用
grep -rn "GPIO_PIN_8\b" Core Device Platform Application | grep -v "GPIO_PIN_8[0-9]"
grep -rn "PL_PORT_E\|pl_gpio_config_output\|pl_gpio_release_exti" --include=*.c Application Device Platform
for f in Device/Display/dev_display_*.c; do echo "$f $(grep -c g_hub75_pin $f) $(grep -c 'GPIO_PIN_8\b' $f)"; done
# 提交核实
git diff --stat c624915..HEAD -- Application/Src/app_render.c Application/Src/app_key.c Device/IO/dev_key.c Core/Src/gpio.c Platform/Src/pl_gpio.c Platform/Src/pl_hub75.c
for f in $(git diff --name-only c624915..HEAD -- '*.c' '*.h'); do
  a=$(git show c624915:"$f" | gcc -fpreprocessed -dD -E -P - | md5sum); b=$(git show HEAD:"$f" | gcc -fpreprocessed -dD -E -P - | md5sum);
  [ "$a" = "$b" ] && echo "comment-only: $f"; done
# 镜像指纹
stat -c "%y %n" build/Debug/Project_STD.elf build/Debug/Project_STD.hex build/Debug/builder.params
md5sum build/Debug/Project_STD.elf; arm-none-eabi-strings build/Debug/Project_STD.elf | grep 9K2F3317E0
arm-none-eabi-size -A build/Debug/Project_STD.elf
```
