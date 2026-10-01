# MAME 0.289 XP32 精简构建笔记

> 环境：Windows 10 x64（系统在 E:，工具与仓库在 G:，内存盘 R:）
> 仓库：`G:\EmuSrc\MameDev289_XP`（MAME 0.289 + XP32 兼容实验改动）
> 目标：32 位 clang 构建、tinymame + 仅 cps1 驱动、尽量精简

## 1. 编译环境：build_mingw32.bat

`mingw32.exe` 的本质：预设 `MSYSTEM=MINGW32` → 启动 `bash --login` →
`/etc/profile` + `/etc/msystem.d/MINGW32` 注入：

```
MSYSTEM=MINGW32            MINGW_PREFIX=/mingw32
MINGW_CHOST=i686-w64-mingw32
PATH=/mingw32/bin:/usr/local/bin:/usr/bin:/bin:<Windows路径>
PKG_CONFIG_PATH=/mingw32/lib/pkgconfig:...   TMP/TEMP=/tmp
```

本仓库的 .bat 走同样路径（预设 MSYSTEM 后调 `bash --login`），makefile 里
`OVERRIDE_CC = clang`（第 93 行）+ MSYSTEM=MINGW32 自动落到
`windows_x86_clang` 目标（32 位 clang）。

**三个坑（都踩过）**：
1. .bat 注释必须纯 ASCII —— cmd 按 GBK 解析批处理，UTF-8 中文注释会把
   `mingw32.exe` 拆成乱码命令；
2. .bat 行尾必须 CRLF —— LF 行尾 + 长时间子进程会让 cmd 恢复解析时读错
   偏移，产生幻影命令错误和 rc=9009（不是真构建失败）；
3. 用 `Write`/`Edit` 工具写完 .bat 后要 `sed -i "s/$/\r/"` 转 CRLF。

## 2. 链接器：lld（配套 21.1.8）

- mingw32 官方仓库已精简（无 LLVM 系包），lld 装不到，但 Aliyun 镜像还留有旧包：
  ```
  pacman -U --noconfirm https://mirrors.aliyun.com/msys2/mingw/mingw32/mingw-w64-i686-lld-21.1.8-4-any.pkg.tar.zst
  ```
  与 clang 21.1.8-4 完全同版本；依赖的 llvm-libs 21.1.8-4 已在。
- makefile 加 `LDOPTS = -fuse-ld=lld`（比 ARCHOPTS 干净：只进链接命令）。
- 效果：GNU ld 满屏 `duplicate section ... has different size` 警告消失，
  exe 97.7→78.1MB（bfd 重复保留 typeinfo 节区，lld 正确合并）。
- 注意：若开 `LTO=1`，lld 与 clang 必须严格同代（现在正好都是 21.1.8）。

## 3. 内存盘构建布局

- 仓库内 `rdisk` 符号链接 → `R:\Mame`（RAM 盘，19GB）。
- .bat 里 `BUILDDIR=rdisk`（相对路径！）。
- **陷阱**：`scripts/genie.lua` 第 19 行 `MAME_BUILD_DIR = (MAME_DIR .. _OPTIONS["build-dir"] .. "/")`
  是纯字符串拼接，BUILDDIR 传绝对路径（如 `R:/Mame`）会被当成相对路径
  拼到仓库目录后面——必须用相对路径 + 符号链接的方案。
- exe 始终生成在**仓库根**（genie targetdir = MAME_DIR，BUILDDIR 深度自动抵消），
  与 BUILDDIR 无关。
- 编译产物全部物理落在 R:（`R:\Mame\{mingw-clang,generated,projects}`），
  重启即失，全量重编约 20 分钟（-j3）。
- 仓库内旧的 `build\`（417MB）已冗余，可删（git 里被跟踪过，删后 status 会
  多一批预期中的 D）。

## 4. 精简方法论（tmp\_scan_unused.py / _prune_unused.py）

原理：不需要文件访问钩子——
1. **.d 依赖文件**（clang `-MMD` 自带）= 编译器亲自记录的"读过哪些文件"；
2. 生成的 `*.make` 里的规则前置项 = 构建期输入（.lay/.bdf/.flt/.lst/py）；
3. 保守保留：`scripts/`、`3rdparty/genie/`、所有 `*.lay/*.bdf/*.flt/*.lst`、
   根 makefile（有些输入经 `$(wildcard)` 引用，不出现在字面量里）。

**重大教训**：旧构建是经 `/tmp/mame-master` 符号链接做的（指向本仓库同一目录），
其 .d 里的绝对路径前缀不同，按"仓库外"过滤会漏掉一大块已读文件 → 误删 →
构建报 `debugvw.h not found` 等。分析脚本已加"mame-master 路径重映射"。
**以后任何配置变化（换驱动/开 DEBUG/64位）后必须重扫，不要复用旧清单。**

剪枝执行（git 兜底，随时 `git restore -- src 3rdparty` 找回）：
```
python tmp\_scan_unused.py G:\EmuSrc\MameDev289_XP\rdisk   # 扫描（参数=构建树）
python tmp\_prune_unused.py            # dry-run
python tmp\_prune_unused.py --apply    # 实删
```

## 5. 模块手术记录

| 动作 | 位置 |
|---|---|
| 删运行目录：regtests/tests/benchmarks/docs/doxygen/web/android-project/hash | 直接删；根 makefile:1553 include regtests/regtests.mak 需单独保留该文件 |
| `NO_USE_PORTAUDIO = 1` / `NO_USE_MIDI = 1` | makefile 34-35 行 |
| bgfx：删 drawbgfx/bgfxutil/bgfx\* 文件表、include 目录、BX_CONFIG_DEBUG | scripts/src/osd/modules.lua |
| bgfx：链接表去 "bgfx"/"bimg"/"bx" | scripts/src/main.lua ~205 |
| bgfx：整块删除 bx/bimg/bgfx 工程定义（403 行） | scripts/src/3rdparty.lua 1190-1592 |
| 注册表摘除 RENDERER_BGFX×2、DEBUG_IMGUI、SOUND_WASAPI、SOUND_XAUDIO2 | src/osd/modules/lib/osdobj_common.cpp REGISTER_MODULE 清单 |
| 删文件树：3rdparty/{bgfx,bimg,bx}、bgfx\(shader)、src/osd/.../{drawbgfx.\*,bgfxutil.\*,bgfx\,debugimgui.cpp} | 直接删 |
| 删 WASAPI/XAudio2/mmdevice_helpers（Vista+ API，XP 用不了） | scripts/src/osd/modules.lua sound 段 + 同上 |

**为什么 WASAPI/XAudio2 必须删**：之前链接成功是因为 libportaudio 的
pa_win_wasapi.c 用 INITGUID 顺带定义了全部 GUID/PKEY；撤掉 portaudio 后
符号悬空。而 XP32 上 WASAPI/XAudio2 本来就无法运行，删除比移植 INITGUID
更合理。**若将来要恢复 WASAPI**：git restore 相关文件 + 撤销 makefile 开关 +
处理 GUID 定义（加 `#include <initguid.h>` 或链 `-lksuser` + 自定义 PKEY 定义）。

**保留**：D3D9+HLSL（XP 主力渲染）、OpenGL、GDI、DirectSound、
lua/sqlite3（核心）、asio（http.cpp 硬依赖）、softfloat3（m68k FPU）、
调试器 win/gdbstub/none/qt 空壳。imgui 调试器随 bgfx 删除（渲染依赖 bgfx，
imgui 源码也在 bgfx 树里）。

**修正认知**：exe 只从 78→74MB，因为 libbgfx 的成员本来只在被引用时才链入；
模块手术的主要收益在**源码树（-166MB）和编译时间**，不在 exe 体积。

## 5.1 音频延迟与 Audio Mix/效果器

- **3 秒音频延迟的真凶**：mame.ini 里 `audio_latency 2`（dsound 环形缓冲 = 该值秒数，
  漂移钉死在溢出上限 = 整个缓冲深度）。改成 0（自动 0.1 秒）即消失。
  MAME 侧代码（direct_sound.cpp 的 stream_sink_update）与上游一致无病。
- **Audio Mix / 效果器**（0.26x 新增）：核心在 `src/emu/sound.cpp/h`（混音路由图以
  effect_step 构建，每扬声器独立 effects_buffer + 专用效果线程）+ `src/emu/audio_effects/`
  （compressor/eq/filter/reverb）+ UI 10 个文件。
  - **核心不摘**：混音图深度耦合，拔除等于重写 mixer，风险不成比例；
    默认禁用时效果链为直通，无运行代价。
  - **UI 已恢复**（避免自欺欺人：核心在而藏 UI 属半吊子；要根治=混音器重写，
    见上面"核心不摘"），Audio Mix / Audio Effects 菜单照常可用。
- `STRIP_SYMBOLS = 1` 已启用（makefile），链接后自动剥离符号，
  exe 74→54MB；手工剥离可用 `mingw32\bin\strip.exe tinymame.exe`。

## 5.2 exe 体积对账（map 解析，tmp\_map_analyze.py）

`MAP = 1` 生成 lld map（`rdisk\mametinymame.map`），解析得 50.2MB 真实构成：

| 贡献者 | 体积 | 性质 |
|---|---|---|
| emumem 内存系统（aspace+hedr/hedw0-3+mview） | ~14MB | 核心模板，不可拆 |
| src/frontend/mame（luaengine 5 单元 ~9MB + UI） | ~11MB | lua 绑定是模板大户 |
| m68000 四解码器（sdf/sdp/sif/sip 各 ~2MB） | ~7.7MB | 68000 必需 |
| .eh_frame + .gcc_except_table（异常展开） | ~11.4MB | MAME 用异常，关不掉 |
| sqlite3.o | 0.82MB | 仅 lsqlite3 绑定用 |
| crt/系统库 | 1.45MB | 固定 |
| 其余（osd/debug/capcom/util/zstd…） | ~5MB | — |

**关键事实**：m68008/m68000mcu/020/030/040 的巨型解码器 **不在 exe 里**
（map 零引用，归档成员未被拉取）——m68000 家族裁剪只省编译时间，
exe 收益≈0。格式动物园（formats）在 exe 里也只占 0.21MB。

exe 瘦身剩余手段：LTO（跨 TU 去重模板，重建+链接变慢但体积可期 35-40MB 级）、
-Os（-O3→-Os，估 40-45MB，速度有损）、摘 lsqlite3（-0.8MB）。
回到 0.149 的 10MB 无可能：那是新架构（emumem/lua/解码器）的本体重量。

## 6. 当前状态

- **仓库已重构**：分支 `xp32` = 官方 `mame0289` tag + 单个精简提交（2955f67f，树中
  无 build/）；原 master 历史（含全部 build blob）已删除，完整备份在
  `G:\EmuSrc\mame-master-backup.bundle`（`git clone` 该文件可整体还原）。
- .git 从 317MB 瘦到 **219MB**；`build\`（G: 417MB）与 `rdisk\`、`tinymame.exe`、
  `mametinymame.map` 均在 `.git/info/exclude` 忽略清单中，未跟踪。
- **已推送**：`git push origin xp32 --no-tags` 完成（45.58 KiB，内容寻址去重）。
  提交差异 = 26210 删除 + 37 修改 + 6 新增；37 个修改经 `--ignore-cr-at-eol`
  逐文件判定**全部为真实 XP32 补丁**（无行尾噪音），GitHub 的 25000 文件
  显示以删除为主，属精简本体，无需重写历史。
- src/ 和 3rdparty/ 未读文件 **归零**（扫描确认），源码全部是构建实际读取的。
- 工作树 835MB（含 .git 219MB）：src 196MB、3rdparty 42MB。
- 渲染 D3D9(HLSL)/OpenGL/GDI；声音 DirectSound（audio_latency 须为 0/0.1）；
  调试器 win/gdbstub。

## 6.1 驱动组队（cps1/cps2/cps3/fcrash/neogeo/pgm/pgm2）

当前 `SOURCES=cps1,cps2,cps3,fcrash,cps1bl_5205,cps1bl_pic,neogeo,pgm,pgm2`
（9 源），**1117 个驱动**编入。cps1bl_5205/pic = **CPS1 盗版专用文件**（5205 音频
DMA / PIC16C57 保护，游戏自带：sf2ceb×5、sf2mdt×3、dinopic×3、punipic×3、
wofpic、knightsb、captcommb2、jurassic99…）；snk6502（1981 Vanguard 时代，
拖着离散音频大坑）已剔除。

换驱动集的流程与机制（重要）：
1. 从 mame0289 tag 恢复驱动源：`git checkout mame0289 -- <路径>`；
2. 改 SOURCES 后**必须 REGENIE=1**（改 SOURCES 不会自动触发工程再生成）；
3. makedep 在 genie 时扫描驱动的 `#include` 决定设备集——头文件缺失 =
   对应设备不进工程 = 链接期 undefined symbol。恢复时 cpp/h 成对恢复；
4. 链接错误逐个补，本轮共恢复 ~160 文件：bus/neogeo + bus/neogeo_ctrl 全目录、
   cpu/{arm7,sh,mcs51}、sound/{ymopn,ics2115,ymz770,mpeg_audio,cdda,ay8910}、
   machine/{watchdog,alpha_8921,vic_pl192,gt913_io,gt913_kbd,gt913_snd,upd1990a,
   input_merger,74259,v3021,atmel_arm_aic,timer,nscsi_bus,nscsi_hle,nscsi_cb,
   intelfsh}、mame/igs/{igs022,igs023_video,igs025,igs028,igs036crypt}、
   capcom/{cps2*,cps3*,cps3_a*,fcrash*}、snk/ng_memcard；
5. nscsi 已裁最小集：bus.lua 只留 cd/devices/hd；devices.cpp 重写为仅
   cdrom/cdrom_2x/harddisk 三选项——applecd/cdd2000 等每台光驱内嵌不同家的
   单片机（mcs51/m6502/h8/m37710），全部剔除，CPS3 只用 nscsi_cd。

### CPS3 性能课题（已结案）

CPS3 32 位上限 80%（其他 0.289 XP32 构建只有 45%），旧版 MAME 构建可达 800%：

- 根因：CPS3 的 SH2 用 UML 式 DRC，后端在 0.289 只剩 drcbec（便携解释）+
  drcbex64（原生 x64）——**32 位 x86 原生后端 drcbex86 已被官方删除**（约 0.25x
  时代），32 位构建的 SH2 只能走 drcbec 慢速通道，比原生慢约一个数量级；
- `-drc`/`-nodrc` 选项也随 32 位后端一起被移除（emuopts.cpp 零命中），
  DRC 启用现在是设备内部自动决定，用户层无开关；
- 实测横向：本构建 80% > RetroDan UART 同补丁构建 45%（其 GCC vs 本 clang
  -O3，编译器差距约 1.8 倍）——本构建已是已知 0.289 时代 XP32 中 CPS3 最快；
- 旧版 800% 的构建基于还有 drcbex86 的旧官方版（约 <=0.25x）；
- 可行出路：a) Win10 x64 上另编 PTR64=1 变体（drcbex64 原生，CPS3 全速）；
  b) XP32 上并存一个旧版 MAME 专跑 CPS3；c) 接受现状（32 位 0.289 无解）。

## 7. 常用操作速查

```bat
build_mingw32.bat                                  REM 缺省全量/增量构建
build_mingw32.bat -j4                              REM 调并行
set MAME_BUILDDIR= && build_mingw32.bat            REM 临时改回仓库内 build\
python tmp\_scan_unused.py G:\EmuSrc\MameDev289_XP\rdisk    REM 重扫（配置变了必做）
python tmp\_prune_unused.py --apply                REM 重新剪枝
git restore -- src 3rdparty                        REM 全量反悔
git restore -- <路径>                              REM 单独找回某文件/目录
```

## 8. 风险提示

- 换任何 68020+ 驱动、或要编其他 SUBTARGET/DEBUG/64 位时，被剪掉的文件需要
  `git restore` 按需找回，并重新扫描。
- R: 是内存盘：重启后 `R:\Mame` 清空，首次构建变全量（~20 分钟）。
- `rdisk` 若重建：删链接本身用 `rmdir rdisk`（不会动 R:\Mame 内容）。
- lld/clang 已配套 21.1.8；`pacman -Syu` 大升级会破坏这套配套，升级前先想清楚。
