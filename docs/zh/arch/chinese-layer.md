<!-- SPDX-License-Identifier: Apache-2.0 -->
# 中文层：编码、字体与系统输入法

Open Cube OS 渲染中文的范围，从引导加载器阶段的 OCBL（不——中文
始于内核控制台）直到恢复内核 OCIRE，共用一条字体血脉、三个消费
者。组成件：

- **encoding**（`ocos/services/encoding/`）——GB2312 <-> UTF-8
  转换，仅内核机制；
- **font**（`ocos/services/font/`）——8x16 ASCII 引擎、16x16 CJK
  引擎，以及 1.1 万行的位图数据；
- **display cat 14**——控制台/区域/IME 的 ring 3 op；
- **系统 IME 引擎 + 会话协议**（`svc_display_ime_*`）。

CJK 能力**无条件编译**——旧的 `CONFIG_TUI`/`CONFIG_CHINESE` 开关
已随 L0 TUI 平面删除。

## 编码：GB2312 与 UTF-8（`svc_encoding_core.c`）

内部表示是 Unicode BMP 码点（u32）。GB2312 映射**经内嵌字体的反向
表**运行（`chinese_font_lookup` / `chinese_font_index_to_unicode`），
“使编码器/解码器与渲染器永远就同一字符集达成一致”——字体数据是
“哪些字符存在”的唯一事实来源。UTF-8 按 RFC 3629 严格解码：过长
序列、代理区、>U+10FFFF 与截断一律拒绝。

```c
typedef enum encoding_kind {
    ENC_ASCII  = 0,   /* only 0x00..0x7F bytes present               */
    ENC_GB2312 = 1,   /* GB2312 double-byte + ASCII single-byte      */
    ENC_UTF8   = 2,   /* RFC 3629 UTF-8                              */
    ENC_MIXED  = 3    /* valid for >1 of the above / undecidable     */
} encoding_kind_t;

#define ENC_E_INVAL   (-2)   /* NULL pointer / zero capacity          */
#define ENC_E_TRUNC   (-3)   /* input ends mid-sequence               */
#define ENC_E_ILLEGAL (-4)   /* byte sequence not valid in the charset */
#define ENC_E_NOSPACE (-5)   /* output buffer too small               */
#define ENC_E_UNMAP   (-6)   /* codepoint has no mapping in the target */
```

API 面：按字符的 `encoding_gb2312_decode/encode` 与
`encoding_utf8_decode/encode`；批量的 `encoding_convert`（ASCII/
UTF-8 按字节，GB2312 经字体表；混合输入按 GB2312 对待）；启发式的
`encoding_detect`（纯 ASCII -> ENC_ASCII；带 >= 1 个多字节序列的
严格 UTF-8 -> ENC_UTF8；GB2312 首/尾字节范围 -> ENC_GB2312；否则
ENC_MIXED）；`encoding_status`（调用计数器 + 字符集覆盖面）。
ring 3 面是 display op 9/10（`encoding`、`encoding-test`）。

## 字体：生成与引擎注册表

三个生成资产，三个生成器：

- `ocos/kernel/core_console_font_data.c`——8x16 ASCII 表
  （`const u8 screen_font8x16[256][16]`，bit 7 = 最左像素），由
  `tools/gen_font.py` 再生。
- `ocse/chinese/chinese_font_data.c`——CJK 16x16 GB2312 位图数据，
  由 `tools/gen_chinese_font.py` 生成（源自 Unifont）。此文件是
  **字体血脉之根**：OCIRE 的
  `ocire/generated/ocire_cn_font_data.c` 由 `ocire/ocse.mk` 中的
  sed include 重写规则*从它*生成，因此字体的重新 vendor 自动流入
  恢复内核（WP06g3-4.8）。vendor 的 PRESERVE 列表的存在，使重新
  vendor 永远无法恢复 v1.0.0 的基线裁剪字形（WP06f）。
- `tools/gen_font_gz.py`——压缩字形流水线。

引擎注册表（`svc_font_engine.h`）是 WP-01 的 L1 扩展点：

```c
typedef struct screen_font_engine {
    const char* name;
    const u8* (*lookup)(struct screen_font_engine* self, u32 cp);
    u32 glyph_w;
    u32 glyph_h;
    /* Optional: list of code points the engine claims to support. NULL = all. */
} screen_font_engine_t;

int l1_ext_register_font_engine(screen_font_engine_t* engine);
screen_font_engine_t* l1_ext_font_engine_by_name(const char* name);
screen_font_engine_t* l1_ext_default_font_engine(void);
```

默认引擎从内建 8x16 表供应字形；CJK 引擎在其旁注册。控制台的
混宽渲染器（kmain 中的 `render_chinese_init` + `console_cn_init`）
为 GB2312 码点取 16x16 单元格、为 ASCII 取 8x16。消费者：display
（控制台路径）、tui（每个页面）、sysapi（`sys_write` 的 UTF-8 宽
单元格回退）、OCSE 与 OCIRE（各自的 vendor/生成副本）。

## 区域与串口编码

- `lang [en|zh]`（display op 4）切换实时控制台区域；状态以键
  `shell.lang` 持久化在 `/etc/opencube.conf`（数据兼容）；
  `disp_lang_set_str` 只接受 "en"/"zh"，其余一律 = en。
- **`L1SVC_DISP_LANG_GET`（op 16）恰输出 `en` 或 `zh`**（无换行）
  ——这是每个 ring 3 应用 i18n 运行时所调用的探测
  （`i18n_lang_zh()`，每次 `tr_()` 调用都探测，无静态缓存——#136
  修复，使 `lang zh` 在长时间运行的应用中立即生效；见
  [private-headers.md](private-headers.md)）。
- `serial_encoding [utf8|gbk]`（op 17，WP06g3）——每个控制台字节的
  COM1 分岔都遵循的唯一设置；OCSE CLI 为安装器世界携带同一命令。
- `font` / `font-test`（op 7/8）转储引擎覆盖面并经 render_cn 渲染
  字形矩阵；`chinese` / `chinesetest`（op 5/6）走控制台 CJK 路径。

## 系统 IME 与会话协议（WP06g2）

一个拼音引擎服务全系统——编辑器**逐键**驱动它，而非内嵌自己的
引擎。ring 3 op：

```c
#define L1SVC_DISP_IME        11   /* ime [-i]                */
#define L1SVC_DISP_IME_TEST   12   /* ime-test                */
#define L1SVC_DISP_IME_BEGIN  13   /* ime session open        */
#define L1SVC_DISP_IME_KEY    14   /* ime session key feed    */
#define L1SVC_DISP_IME_END    15   /* ime session close       */
```

`IME_BEGIN`/`IME_END` 复位会话（out 为 ""）。`IME_KEY` 的 arg 是
一个十进制键码；out 的**首行是动作数字**，其余行是文本：

| 动作 | 含义 | 调用者行为 |
|---|---|---|
| `0` | 与 IME 无关 | 本地处理该键 |
| `1` | 预编辑条已更新 | 重绘合成条 |
| `2` | 已提交 | 在光标处插入 UTF-8 文本 |
| `3` | 已提交 + 调用者处理按键 | 插入并同时处理该键 |
| `4` | 服务失败 / 协议畸形 | **吞掉该键，绝不泄漏进文件**（验尸灾难 6） |

`ime -i`（交互式合成行）直通路由——实时 `\r` 重绘，与 sshd 及
TUI op 遵循同一实时输出规则（见 [tui-engine.md](tui-engine.md)）。
拼音数据本身是生成的（`svc_display_pinyin_data.c`，词组表由
`tools/gen_pinyin_phrase.py` 处理 `tools/ime_phrases.txt` 而来）。

### 编辑器桥（该协议的主要客户端）

`l1_editor_core.h` 的 `unano_ime_feed(key)` 以十进制键码作为 arg 字符
串调用 `DISPLAY/IME_KEY`；F2（键码 0x91）经 `IME_BEGIN`/`IME_END`
切换；预编辑条是 160 字节的 `unano_strip`；动作 4 的存在正是为了
让失败的服务永远无法污染被编辑的文件。TUI 桌面的编辑器与终端使用
同一引擎。用户指南：
[../manual/chinese-console.md](../manual/chinese-console.md)。

## 三世界图景

| 世界 | 中文能力 | 字体来源 |
|---|---|---|
| OCOS（主内核） | 混宽控制台 + TUI + IME + encoding 服务 | `ocos/services/font/`（同一血脉） |
| OCSE（安装器） | console_cn + render_cn + encoding，双语 GUI/向导 | `ocse/chinese/chinese_font_data.c`（血脉之根，由 `tools/gen_chinese_font.py` 再生） |
| OCIRE（恢复） | console_cn + 双语 shell 帮助（`lang [en\|zh]`） | `ocire/generated/ocire_cn_font_data.c`（从 OCSE 副本生成） |

OCBL 本身按设计纯 ASCII（512 字节 stage1 预算与 El Torito 约束
让加载器无处安放 CJK 字形表）。
