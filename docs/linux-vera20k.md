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
- 游戏速度映射：启动器 0..6 表示慢→快，VERA 的 pacer 是 1 最快、6 最慢，
  所以写进 `spawn.ini` 的 `GameSpeed` 要取 `6 - speed`；`VERA.INI` 里保留原值
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

## 已验证

aibook（Ubuntu 24.04 arm64 + M1000）上：

* 任务列表 3389/3390 可启动（唯一缺失项是确实不存在的 `Mod&AI\1996068576846557185`）
* 从启动器选任务 → 启动 → VERA20K 载入地图、进入游戏、模拟正常运行
* Wine-FEX 路径同样实测：`ra2-launch.sh` → FEX + Steam Linux Runtime →
  `gamemd-spawn.exe` 运行，Xorg `:3` 上出现 “Yuri's Revenge” 窗口（1286×752）
