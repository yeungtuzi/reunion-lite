# Linux：Wine-FEX 与 VERA20K 引擎

Linux 版启动器与 macOS 0.4 共用同一套核心和界面结构，区别只有传统引擎的实现：
macOS 用 CrossOver，Linux 用 **Wine-FEX**（Proton + FEX-Emu）。

## 引擎

| 引擎 | 说明 |
|---|---|
| Wine-FEX（默认） | 通过 `scripts/linux/ra2-launch.sh` 在 Steam Linux Runtime 里跑 `gamemd-spawn.exe` |
| VERA20K（实验） | 原生 Rust 引擎，不需要 Wine/Proton；用 `Run/` 作为 `ra2_dir` |

## VERA20K 需要做的适配（启动器已自动处理）

- `spawn.ini` / `spawn-vera.ini` 以 **UTF-8** 写出，并通过 `RA2_SPAWN_INI` 指向后者
  （gamemd 读 ANSI 代码页，VERA 读 UTF-8）
- 从 `Mod&AI/*/aimd.ini` 注入 AI 根到 `Run/aimd.ini`
- 写出 `Run/config.toml`：`ra2_dir` 用正斜杠（反斜杠会被 TOML 当成非法转义）
- 游戏速度映射：启动器 0..6 表示慢→快；`yeungtuzi/vera20k` 的 `main` 已带
  pacer 补丁（6 = 不限制、5 = 60 Hz），所以 `spawn.ini` 与 `VERA.INI` 都直接
  写启动器的 0..6 值
- 环境变量：`RA2_DIR`、`RA2_SPAWN_INI`、`VERA_TOLERANT_AI=1`、
  `VERA_FPS_CAP=60`、`VERA_FPS_CAP_IDLE=60`、`LANG=zh_CN.GBK`
- `movies03.mix` 缺失时补一个合法的空 MIX（Reunion 的合并资源里不带基础版电影包）

## 需要的 VERA20K 补丁

Reunion 的战役 AIMD 里会引用当前 RuleSet 不认识的成员（自定义单位/改名类型），
上游 `install_team_ai_registry` 会直接拒绝整个安装，导致地图加载失败：

```
active YR aimd.ini failed RuleSet resolution: [UnknownTaskForceMember ...]
```

修复在 `yeungtuzi/vera20k` 的 `linux-tolerant-ai` 分支：设置 `VERA_TOLERANT_AI=1`
时保留其余 team AI、丢弃未知成员。没有该补丁时引擎会在载入地图后退出。

## 画面 / 日志 / 音频

- 「设置 → 画面」的分辨率与渲染器会写进 `Run/RA2MD.ini [Video]` 和 `Run/ddraw.ini [ddraw]`：
  `ScreenWidth/ScreenHeight`、`ddraw width/height`、`windowed=true`、`fullscreen=false`、
  `renderer=gdi|opengl|direct3d9`。改分辨率不用重装，下次启动即生效。
- 状态栏右侧显示当前分辨率与任务计数；启动后「日志」按钮打开
  `~/.local/share/reunion-lite/last-launch.log`（两个引擎的输出都写这里）。
- 任务列表会记住上次选中的任务（`lastMission`），重开启动器自动定位。

## 音频与性能

- aibook 上加载任务时曾出现爆音，`ra2.log` 里是成片的
  `audio stream error: Buffer underrun/overrun`：默认 512 帧（约 11 ms）太小。
  「设置 → 高级 → VERA20K 音频缓冲」设成 2048 后，一次完整运行 underrun 计数为 0。
- 「性能指示」勾选后以 `VERA_FPS_COUNTER=1` 启动，进入游戏即显示 FPS/CPU 面板
  （等效 Ctrl+Alt+F）。
- 性能结论（已结案）：MTGPU Vulkan 下瓶颈是**等显示器 vsync**，各阶段耗时之和
  小于 16.6 ms（60 FPS 的帧预算），因此满足 60 FPS 要求；整段运行的平均值
  （例如 25 s 内 1249 帧 = 49.9 FPS）还包含地图加载，不代表游戏内帧率。
  测试补丁会在干净退出时写一行
  `Performance summary: adapter=..., frames=..., seconds=..., avg_fps=...`
  到 `Run/logs/ra2.log`。
- 图形后端（设置 → 画面 → VERA20K 图形后端）：
  - `auto`（默认）：存在 `/etc/vulkan/icd.d/mtvk.json` 就用 MTGPU，否则 llvmpipe
  - `mtgpu`：强制 MTGPU Vulkan + X11
  - `software`：llvmpipe（Wayland）
- aibook 的 MTGPU 是有 Vulkan 的：ICD 在 **`/etc/vulkan/icd.d/mtvk.json`**
  （`libmtvk.so`，Vulkan 1.3.290，device `M1000`），不在 `/usr/share/vulkan/icd.d`。
  但它的 surface 只支持 X11：在 Wayland 下 wgpu 报
  `vulkan not compatible with provided surface`，然后退回 llvmpipe。
  所以用 MTGPU 时启动器会显式设 `VK_ICD_FILENAMES`、`WGPU_BACKEND=vulkan`，
  并去掉 `WAYLAND_DISPLAY` 走 XWayland（`DISPLAY=:0` + 自动找 `XAUTHORITY`）。
  实测 `ra2.log`：`Using GPU adapter: M1000 (Vulkan)`，游戏内性能面板从 ~20 FPS 提到 ~60。
- GL 路径目前不可用：MTGPU 的 EGL（`libEGL_mtgpu.so`）在 wgpu 的 GL 后端会
  `khronos-egl` panic；而 llvmpipe 的 GL 又缺
  `TextureUsages(COPY_SRC | COPY_DST)`，VERA 会拒绝初始化。

## 性能排查（aibook / M1000，2026-11）

**根因**：KDE 缩放 2 时，引擎按逻辑尺寸开窗、按物理像素渲染——
1024×768 的窗口实际是 2048×1536 的 surface，GPU 占用 ~87%，帧率贴着 60 并偶发掉帧。

**处置**：`设置 → 画面` 勾选「低分辨率渲染 + 系统上采样」。
启动器会设置 `WINIT_X11_SCALE_FACTOR=1` 和 `VERA_WINDOW_SIZE=<分辨率>`（需补丁版引擎），
引擎按该分辨率渲染，合成器放大到面板——画面略糊，但 GPU 负载大幅下降。

实测（GameSpeed=1，即最快档；M1000 Vulkan + X11）：

| 渲染分辨率 | GPU | 帧率窗口（2 s） | 备注 |
|---|---|---|---|
| 1920×1080 | ~52% | 65–74 fps，偶发 48 | 满足 60 FPS，偶发掉帧待查 |
| 1600×900 | ~42% | 61–87 fps | 余量更大 |
| 默认 HiDPI（≈2560×1652） | ~87% | ~60，易掉帧 | 不建议 |

**2026-11-16 复测（补丁版引擎 main，Vulkan + X11，窗口聚焦）**：

| 渲染分辨率 | GPU | 2 s 帧率窗口 | 最坏帧 |
|---|---|---|---|
| 1920×1080 | 76% | 56.7–58.3（一次 44.4） | 25–38 ms |
| 1280×720 | 51% | 52.9–59.3 | 28–34 ms |

结论：降低分辨率能把 GPU 从 76% 降到 51%，但**最坏帧仍在 25–38 ms**，
说明剩下的偶发卡顿是 **CPU/引擎侧尖峰**（约 2 帧预算），不是填充率；
要继续逼近“稳 60”，得在引擎里找这些周期性尖峰（仿真 tick、资源上传等）。

**GL/GLES 路径（用户建议优先试）**：MTGPU 的 EGL/GLES 3.2 在 X11 下正常
（`eglinfo` 显示 `M1000 / OpenGL ES 3.2`），wgpu 也能枚举到 `M1000 (Gl)`，
但 VERA 的渲染器依赖 wgpu GL 后端没有的能力：
`per-attachment write masks`、`VIEW_FORMATS`、surface `COPY_SRC/COPY_DST`。
前两项我们在 `gpu.rs` 里对 GL 后端做了放宽，最后仍卡在
`native sprite drawing requires per-attachment write masks`——
要上 GLES 需要改渲染器（精灵绘制路径），收益不确定，暂不继续。

**GLES vs Vulkan 2D 基准**（自写 wgpu 2D 精灵 benchmark，4096/16384 个带 alpha 混合的
实例化四边形，1280×720 离屏目标；同一台 M1000）：

| 负载 | Vulkan | GL/GLES |
|---|---|---|
| 4096 quads × 32 px | 820 fps / 1.22 ms | 853 fps / 1.17 ms |
| 16384 quads × 32 px | 511 fps / 1.96 ms | 385 fps / 2.59 ms |
| 4096 quads × 128 px（填充率） | 296 fps / 3.38 ms | 239 fps / 4.18 ms |

结论：两者的 2D 开销同一量级，Vulkan 在填充率/大批量更好，GLES 只在极小批次略快。
因此没有足够收益去为 GLES 重写精灵绘制路径；GPU 端几百 fps 也说明 VERA 的 60 fps
瓶颈不在 GPU 吞吐，而在引擎每帧的 CPU 开销与周期性尖峰。

benchmark 源码：`/root/lvllm/repos/wgpu-2d-bench`（本地工具，未随仓库发布）。

### 0.3.5 构建的可切换接入（2026-11-17）

- 0.3.5 构建：`~/.local/bin/vera20k-035`（fork 0.2.8 仍是 `~/.local/bin/vera20k`）
- **aibook 上启动器已默认指向 0.3.5**：`~/.config/reunion-lite/reunion-lite.conf` 里
  `vera20kBin=/home/lf/.local/bin/vera20k-035`、`audioBuffer=1024`
- 要回滚到 0.2.8：把 `vera20kBin` 改回 `/home/lf/.local/bin/vera20k` 即可
- 已验证（启动器同款环境：`-SPAWN -SPEEDCONTROL`、`VERA_WINDOW_SIZE=1920x1080`、
  `WINIT_X11_SCALE_FACTOR=1`、`REUNION_LITE_LAUNCH=1`、`VERA_AUDIO_BUFFER=1024`）：
  任务正常进入，`frames=3282 / 52.75 s`，渲染稳定 60.0 fps，mean gap 16.7 ms，
  jitter 2–4 ms，声音无破音。
- 已知差异：0.3.5 分支还没有 `VERA_HW_CURSOR` 硬件光标移植（0.2.8 fork 有），
  切换后光标走软件绘制路径。

### 0.3.5「卡在启动画面」的真正原因：`VERA_AUDIO_BUFFER`（2026-11-17）

把 `rebase-0.3.5` 构建接到启动器流程时，引擎停在
`Initialization complete / 6 quads / frames=1`，主线程 `ep_poll` 空闲。
逐项排除：不是 `-SPEEDCONTROL`、不是 `VERA_WINDOW_SIZE`、不是焦点丢失。

A/B 结果：

| 环境 | 结果 |
|---|---|
| `VERA_AUDIO_BUFFER=4096`（或 2048） | 启动即卡死，0 帧 |
| 不设该变量（自动 = `sample_rate/100`） | 正常进任务，60.0 fps，mean gap 16.7 ms，jitter 1.9–4.5 ms |

⇒ 这台机器的 cpal 设备在较大周期下开流失败/阻塞，把主线程一起挂住。
启动器的「音频缓冲」默认值因此改回 **自动**；确有破音时再逐档（256/512）试，
不要直接给 2048 以上。

顺带确认：0.3.5 上没有帧率上限（VSync 开时按刷新率跑，实测 150–260 fps），
所以 0.3.5 分支补了 `present_cap`（`VERA_FPS_CAP` 默认 60），实测稳定 60.0 fps。

### 0.3.6 基线与 trainer（2026-11-17 晚）

- 引擎基线换成 fork 的 `main`（0.3.6 合并版），被 merge 丢掉的接线已补回：
  `present_cap`（60 fps 上限）、`VERA_FPS_LOG`、`VERA_HW_CURSOR`、`VERA_WINDOW_SIZE`、
  `VERA_FPS_COUNTER`、启动器 token、`VERA_VSYNC` 日志。
- trainer 面板需要 `--features debug-tools` 构建（CI 的 Linux 包就是这样打的）。
  热键：Linux **Ctrl+Alt+=**（Ctrl+Alt+T 与桌面终端快捷键冲突），macOS 仍是 Cmd+Alt+T。
- 速度滑杆是模拟 TPS，不是渲染帧率；启动器档位命名并默认「较快（推荐）」。
- 发布：`v0.5.7` 四平台包，Linux aarch64 包内 `vera20k` 实测
  `present mode: Fifo, queued frames: 2`、`SFX output buffer: 1024`、
  `presentation cap: 60 fps`、`hardware cursor: on`、稳定 60.1 fps / jitter 3.9 ms，
  第二实例被拒。

### tint 缓存：微基准证明是负优化（2026-11-17）

对 `CellLightGrid::tint_for_common_scalar_or_default` 做记忆化后，用同一份 micro-bench
（2048 格 × 60 帧，release 构建，aibook A78）对比上游原版：

| 版本 | ns/lookup |
|---|---|
| 上游原版（每次算 palette） | **14.4** |
| 加缓存后（thread-local + generation + 二次哈希） | 34.5 |

原因：palette 本身已在 `LightProfileCache` 里缓存，`key_to_rgb` 只是 3 次除法；
记忆化多出来的是一次哈希 + RefCell + 原子读，比省下的计算更贵。
⇒ 这条**不提上游 PR**，分支留在 `perf/lighting-tint-cache` 仅作记录。

### 流畅度与启动策略（2026-11-17）

| 问题 | 处理 |
|---|---|
| 单位行进“一顿一顿” | 启动器默认注入 `VERA20K_RENDER_INTERPOLATION=1`（retail 默认为关，模拟只有 50 TPS） |
| 帧间抖动 | `pace_frame` 改为**固定 60 Hz 网格**（迟帧跳到下一格，不累积欠账）；`desired_maximum_frame_latency` 2 → 1 |
| 偶发掉到 30 fps | 常见原因是跑了两个实例抢 GPU；引擎加 **unix flock 单实例锁**，启动器再预检一次 |
| 直接执行引擎 | Linux 下引擎要求 `REUNION_LITE_LAUNCH=1`（启动器注入），否则打印提示并退出 1 |
| 鼠标不跟手 | `VERA_HW_CURSOR=1`：软件光标不再绘制，改由合成器合成，**保留 retail 光标美术**（按当前光标 id/帧切换 `CustomCursor`） |
| 光标偏小 | 硬件光标按“面板宽度 / 窗口宽度”等比最近邻放大，与画面放大倍率一致 |
| 观测 | `VERA_FPS_LOG` 每窗口输出 `worst frame`、`mean gap`、`jitter`（帧间隔标准差）；启动器默认开性能指示 |

实测（aibook，1080p + 上采样，faster，60 上限，3 分钟含脚本化单位移动）：
帧率稳定 58–60 fps，`mean gap 16.7 ms`，`jitter 2.1–3.1 ms`，最坏帧 21–35 ms；
任务切换那一次仍有 ~0.8 s 的同步加载尖峰（已知问题）。

### CPU 侧优化（2026-11-17）

在 aibook（Cortex-A78 ×4）上用 `perf` + `top -H` + 自写计时脚本量化，逐项提交到
`yeungtuzi/vera20k`：

| 措施 | 结论 |
|---|---|
| `CellLightGrid` 按 `(profile, scalar)` 缓存调色板 tint | 每帧 1732 个格子不再重复算调色板 |
| **mimalloc**（aarch64 Linux，feature `mimalloc-alloc`） | `cpu_ms/frame` 中位数 8.58 → **7.88 ms**（约 −8%），CPU 核 0.59 → 0.55 |
| thin LTO + `codegen-units=1` | 不限帧峰值 87 → **97 fps**；二进制 91 → 78 MB；编译 4–5 → 9–10 min |
| `-C target-cpu=native`（A78，本地构建） | 与 LTO 合计降低单位帧 CPU 成本 |

**PGO**（`scripts/linux/build-vera20k-pgo.sh`，instrument → 跑一局 45 s → merge → profile-use，
与 LTO 叠加）：`cpu_ms/frame` 15.39 → **14.74**（约 −4%，同一负载下 3 轮中位数）。

**发布构建**：`-C target-cpu=neoverse-n1` + PGO + mimalloc（A78 兼容、可移植），
与 native 版性能持平（15.0 对 14.9 ms），作为对外产物口径；本地自用仍可 `native`。

**验收对照**（aibook，1080p、faster、60 上限、交替 3 轮，机器 load ≈ 7.6）：

| 构建 | fps 均值 | CPU 核 | cpu_ms/frame |
|---|---|---|---|
| 基线（LTO+native，无 mimalloc/PGO） | 52.5–53.0 | 0.81–0.82 | 15.35–15.63 |
| 发布版（mimalloc+PGO+neoverse-n1） | 52.5–53.0 | 0.78–0.79 | **14.90–15.01** |

结论：单位帧 CPU 约 −3%，负载低时 mimalloc 的单独收益更明显（−8%）。帧率仍受
引擎每帧 CPU 工作限制（`build_visible_instances` 等），未到“稳 60”。

当前水位（1080p、`GameSpeed=1`“faster”、60 上限）：帧率窗口 57–59 fps，偶发 45；
最坏帧 21–38 ms（与分辨率无关 → 引擎 CPU 尖峰）；CPU ≈ 0.55–0.60 核。

A/B 方法：同一二进制分别以 `--no-default-features` 编译（无 mimalloc），交替跑 3 轮，
用 `/proc/<pid>/stat` 的 utime+stime 除以日志里的帧数得到 `cpu_ms/frame`。

工具：`VERA_FPS_LOG=2000` 会把每 2 秒的帧率与最坏帧间隔写进 `Run/logs/ra2.log`；
`VERA_WINDOW_SIZE=1920x1080` + `WINIT_X11_SCALE_FACTOR=1` 实现低分辨率渲染 +
合成器上采样（启动器「低分辨率渲染 + 系统上采样」勾选项即这两条）。

## 已知问题（0.5）

- 部分第三方 MOD 单位渲染异常：林德拉克计划的机甲、命运之瞳等；其余任务包基本正常。
- VERA20K 必须使用带补丁的构建（见上），上游原版会在 AIMD 未知成员处直接拒绝加载地图。

## 已验证

aibook（Ubuntu 24.04 arm64 + M1000）上：

* 任务列表 3389/3390 可启动（唯一缺失项是确实不存在的 `Mod&AI\1996068576846557185`）
* 从启动器选任务 → 启动 → VERA20K 载入地图、进入游戏、模拟正常运行
* Wine-FEX 路径同样实测：`ra2-launch.sh` → FEX + Steam Linux Runtime →
  `gamemd-spawn.exe` 运行，Xorg `:3` 上出现 “Yuri's Revenge” 窗口（1286×752）
