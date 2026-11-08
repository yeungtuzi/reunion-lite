# Reunion Lite macOS 工作计划

> 目标：在这台 Apple Silicon Mac 上，用**原生 SwiftUI 启动器** + **CrossOver 26 的 Wine**，
> 通过 reunion-lite 的战役扫描/`Run` 准备逻辑，启动 **原版 `gamemd.exe`**（尤里的复仇引擎），
> 运行 RA2 任务、尤里的复仇任务和第三方任务包。
>
> 明确不做：不使用 vera20k 引擎；不把 `ReunionLite.exe` 塞进 Wine 里当 UI；不复制/分发游戏资源。
>
> 版本：2026-11-08 · 基于本机实测与上游 `yeungtuzi/reunion-lite` v0.2 源码调研

---

## 0. 实施进度（滚动更新）

| 阶段 | 状态 | 证据 |
|---|---|---|
| P0 环境/冒烟 | ✅ 完成（CrossOver 26.3 可运行） | 手动进入训练关、窗口化 1080p/1024×768 |
| P1 core 移植 + `rlctl` | ✅ 完成 | 3,390 任务 / 3,389 可启动；cache 命中 0 ms |
| P2 `Run` 准备 | ✅ 完成 | `prepare` 产出 GBK `spawn.ini`、覆盖清单、存档隔离；基础文件硬链接回收 705 MB |
| P3 Wine 启动集成 | ✅ 实测通过 | 原生 cnc-ddraw + 1:1 窗口 + 速度控制；CPU 从 114% 降到 ~30% |
| P4 SwiftUI 启动器 | ✅ 完成 | 单行选择、任务包侧栏、完整选项、启动/日志/设置 |
| P5 打包 | ✅ 完成 | `build-app.sh` 生成并签名 .app；`install-app.sh` 安装到 /Applications；窗口位置记忆 |

代码仓库：`~/vscode/reunion-lite-macos`（`README.md` 有命令速查）。

---

## 1. 结论先行

1. **CrossOver 装 26.3.0，不要装 27。**
   `gamemd.exe` / `gamemd-spawn.exe` 是 PE32（32 位 x86）。CrossOver 26 仍带
   `win32on64`（在 64 位进程内跑 32 位 Windows 代码），这是 Apple Silicon 上运行
   32 位 YR 引擎的前提；CrossOver 27 已宣布彻底移除 32 位支持，升级即失效。
2. **启动器不用 Wine 跑，改成原生 SwiftUI + C++ core。**
   复用 reunion-lite v0.2 的 `src/core`（扫描、启动计划、地图改写），补一层 macOS
   平台实现（iconv GB18030、POSIX 硬链接、进程启动）。UI 用 SwiftUI，启动器本身
   只有几 MB、常驻内存 < 100 MB，符合“轻量 + 原生 UI 框架”的要求。
3. **Wine 只负责跑游戏本体**：`gamemd-spawn.exe -SPAWN ...` → `gamemd.exe`。
   CrossOver 26.3 的 `wine` 命令支持 `--bottle / --cx-app / --workdir /
   --wait-children`，可由 Swift 的 `Process` 直接调用。
4. **本机预研已跑通一半**：v0.2 core 在 macOS 上原生编译成功，扫描出
   **3,390 个任务 / 1,247 个任务包，其中 3,347 个可启动**（150 ms）；
   本地 0.1 版 core 因反斜杠路径问题可启动数为 0 —— 这正是要迁到 v0.2 core 的原因。
5. **开源 Wine 不是可行替代**：Gcenx 当前发布页只有 11.21 的 wine-devel/staging，
   没有带 `win32on64` 的构建；上游 Wine 在 Apple Silicon 上无法执行 PE32。
   因此本项目**只支持 CrossOver 26.x**，不采用 Parallels/虚拟机兜底（已确认）。

---

## 2. 本机现状（2026-11-08 实测）

### 2.1 系统与工具链

| 项目 | 结果 |
|---|---|
| macOS | 27.2（26B123），arm64 |
| Rosetta 2 | 已安装（`arch -x86_64 true` 通过） |
| 命令行工具 | CLT + Apple clang 21.0.0，`/Library/Developer/CommandLineTools` |
| Xcode | **未安装**（`xcodebuild` 不可用）→ 用 SwiftPM + 手工打包 `.app` |
| Swift | 6.4（swiftlang-6.4.0.34.1），SDK 27.0 |
| 磁盘 | 245 GB 可用；APFS，大小写不敏感 |
| CrossOver | **未安装**（`~/Applications/CrossOver` 里只有几个旧 bottle 链接，`~/Library/Application Support/CrossOver` 为空） |
| Wine | **未安装**；Homebrew `wine-stable` cask 已于 2026-09-01 被 Gatekeeper 禁用 |
| 其他 | Parallels Desktop 已安装，但**本项目明确不使用**（不走虚拟机兜底） |

### 2.2 游戏目录 `~/Documents/ra2reunion`（9.6 GB）

| 目录/文件 | 说明 |
|---|---|
| `Maps/CP` | 2,353 个任务目录，1,246 个 `Battle*.ini` |
| `Mod&AI` | 1.6 GB，287 个任务包/模组注册 ini；`Mod&AI/Mod` 下 9 个模组 |
| `Run` | 2.0 GB，Windows 客户端生成的运行目录（可安全重建为硬链接，能省回 ~2 GB） |
| `YR/` | 原版 YR 数据（`gamemd.exe`、`ra2.mix`、`ra2md.mix`、`MAPSMD03.MIX` 等） |
| `zh/` | 简体中文资源（`game.fnt`、`language.mix`、`subtitle*.txt`） |
| `Resources/Render/` | 4 套渲染器：`CnC_DDraw`、`CnC_DDraw_Experimental`、`CnC_DDraw_Legacy`、`DDrawCompat` |
| `gamemd-spawn.exe` | PE32，CnCNet spawner |
| `gamemd.exe` / `YR/gamemd.exe` | PE32，YR 1.001 引擎 |
| `RA2MD.ini` | 当前 `Renderer=CnC_DDraw_Experimental`、窗口化、2560×1440 |

基础文件核对：`runprep` 需要的 24 个基础文件**全部存在**（含
`Resources/Voice/English/audiomd.mix`、`Resources/shroud.shp`、`thememd.mix/ini`），
不需要额外下载。

### 2.3 扫描器实测（原生 macOS 编译 v0.2 core）

| 指标 | 结果 |
|---|---|
| 目录指纹 | 1,246 个 ini，12.8 ms |
| 全量扫描 | **150 ms**，3,390 条任务 / 1,247 个包 |
| 可启动 | **3,347** |
| 被排除 | 43（映射盘符后为 1） |
| 排除原因 | 3 个「林德拉克计划」包引用 `D:\ra2master\...`（本机实际在 `~/Documents/ra2master`，**可映射修复**，共 42 条）+ 1 个缺目录的包 |
| 本地 0.1 版 core | 0 / 3,390 可启动（`\` 未转换，`ValidateMission` 全失败） |

> 上游 v0.2 core 的 `Fs()` 已加 `#ifndef _WIN32` 的反斜杠转换；encoding 用 iconv
> GB18030 并对非法字节容错（修掉过 `[Tags]` 截断导致的 `gamemd` 崩溃）。

### 2.4 上游源码状态

- 本地 `~/Documents/ra2reunion/ReunionLite-src` 是 **0.1 快照**（无 `.git`）。
- 上游最新 **v0.2**（2026-11-07）新增 `src/platform/linux`（Qt6 + FEX/Proton），
  同一套 `src/core`；Apache-2.0。
- v0.2 的 Linux 平台层正好是 macOS 移植的模板：`encoding.cpp` 的 iconv 容错、
  `runprep.cpp` 的 POSIX 硬链接/覆盖清单/存档隔离。

### 2.5 CrossOver 26.3 包内已核实的信息

从官方 26.3.0 安装包解包检查（检查后已删除）：

| 项目 | 结果 |
|---|---|
| App 体积 | 929 MB（安装后） |
| 下载包 | 369 MB |
| 引擎 | `bin/wineloader` 为 **x86_64** Mach-O（Rosetta），`lib/wine/i386-windows/*.dll` 为 **PE32** → 走 `win32on64` |
| CLI | `bin/wine` 是 Perl 包装器，确认支持 `--bottle`、`--cx-app`、`--workdir`、`--wait-children`、`--env`、`--no-update`、`--debugmsg` |
| Bottle 模板 | `win10_64`、`win11_64`、`win7_64` 等 13 种；`bin/cxbottle --bottle X --create --template win10_64` 可脚本化创建 |
| Bottle 默认位置 | `~/Library/Application Support/CrossOver/Bottles/<name>` |

> `--cx-app` 接受 `Z:\...` 形式的 Windows 路径；`--workdir` 接收原生路径并自动转换。
> 具体 Z: 盘是否默认存在、`LANG=zh_CN.GBK` 下 Wine 的 ANSI 代码页行为，放到 P0 冒烟验证。

---

## 3. 版本选择：为什么是 CrossOver 26.3.0

### 3.1 结论

- **装 CrossOver 26.3.0**（Homebrew cask 当前就是 26.3.0；或官网 zip）。
- **不要升级到 27（及以后）**，也不要用 CrossOver 的“原生 ARM64 预览版”。
- 安装后**立即关闭自动更新**（CrossOver 的 cask 标了 `auto_updates`，会自己升）。
- 需要保留 Rosetta 2（系统已在）。

### 3.2 理由

1. `gamemd.exe` / `gamemd-spawn.exe` 是 32 位 x86 PE（已用 `file` 核对）。Apple Silicon
   上唯一可行的执行方式是 CrossOver 的 `win32on64`：64 位 x86_64 Wine 进程
   （Rosetta 翻译）里运行 32 位 Windows 代码段。
2. CrossOver 27 的官方说明明确：**32 位应用不再运行，32 位 bottle 全部淘汰**；
   26 的 64 位 bottle 仍可跑 32 位应用，但 27 起不再提供该能力。
3. 上游/开源 Wine 不可行：Apple 从 Catalina 起没有 32 位 dyld；上游 Wine 没有
   `win32on64` 那套自定义 clang thunk。Gcenx 当前只发 wine-devel/staging 11.21，
   不含该补丁。Homebrew 的 `wine-stable` cask 也已被 Gatekeeper 禁用。
4. 27 还会移除 Intel Mac 支持、要求 macOS 14+；对本机没有好处，只丢掉 32 位能力。

### 3.3 安装与固定版本（P0 执行）

```bash
# 方式 A：Homebrew（当前 26.3.0）
brew install --cask crossover

# 方式 B：官网 zip（同上版本）
# https://media.codeweavers.com/pub/crossover/cxmac/demo/crossover-26.3.0.zip

# 校验
/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine --version
# 打开一次 CrossOver，登录/输入已购买的许可证
# Preferences → Updates：关闭自动检查/自动更新
```

> 如果购买的是 25 或更早的许可证，26 可能需要付费升级；先确认许可证覆盖 26.x。
> 这不会影响方案本身，但要提前知道预算。

### 3.4 体积账

| 项目 | 大小 |
|---|---|
| CrossOver 26.3 App | ~0.93 GB |
| Bottle（win10_64，装完 cnc-ddraw 之外不装运行库） | ~0.3–0.5 GB |
| 启动器 `.app` | < 10 MB |
| 游戏 | 已有 9.6 GB，**不复制进 bottle**（通过 `Z:` 盘直接访问） |

只比现在多约 **1.3–1.5 GB**。另外把现有 `Run`（2.0 GB）重建为硬链接可回收约 2 GB，
净增其实接近 0。删除时 `rm -rf` App + bottle 即可，无系统级残留。

---

## 4. 总体架构

```
┌─────────────────────────────────────────────────────────────┐
│ ReunionLite.app  (SwiftUI, arm64, 非沙盒, ad-hoc 签名)        │
│                                                             │
│  MissionListView ── 搜索 / 任务包筛选 / 阵营 / 难度           │
│  OptionsPanel   ── 模组 难度 资金 速度 存档 迷雾 Ares/Phobos  │
│  LaunchButton   ── 启动 / 重新扫描 / 打开日志                 │
│         │                                                    │
│         │  C ABI (rl_api.h, JSON)                            │
│         ▼                                                    │
│  ReunionCore (C++17, 静态链接)                                │
│    ├─ core/      上游 v0.2：campaign 扫描/缓存、launch plan、   │
│    │             地图补丁、spawn.ini、路径解析                  │
│    ├─ platform/mac/  iconv GB18030、POSIX 硬链接、             │
│    │              Run 准备、存档隔离、Wine 路径映射             │
│    └─ config/      游戏根目录、bottle、盘符映射、编码策略        │
│         │                                                    │
│         │  Process (posix_spawn)                             │
│         ▼                                                    │
│  CrossOver 26.3  wine --bottle RA2 --wait-children           │
│    └─ Z:\...\Run\gamemd-spawn.exe -SPAWN ...                 │
│         └─ gamemd.exe  (PE32, win32on64 + Rosetta)           │
│              └─ cnc-ddraw (Run\ddraw.dll) → OpenGL/GDI        │
└─────────────────────────────────────────────────────────────┘
```

### 4.1 为什么 UI 不跑在 Wine 里

| 把 `ReunionLite.exe` 放进 Wine | 原生 SwiftUI 方案 |
|---|---|
| Win32 控件 + Wine 的 DPI/输入/主题，体验割裂 | 原生控件、暗色模式、HiDPI、菜单栏、快捷键 |
| ANSI 代码页、`FindFirstFile`、硬链接都要经过 Wine | 文件操作用 POSIX，编码用 iconv，行为确定 |
| 需要额外 .NET/Win32 依赖、启动慢 | 无运行库依赖，启动即用 |
| 内存/CPU 常驻更高 | 常驻 < 100 MB，空闲 0 CPU |

### 4.2 为什么复用 v0.2 core 而不是重写

- 扫描/缓存/选项合并/地图补丁/存档隔离这些“脏活”已经和 Windows 客户端对齐，
  重写成本高、回归风险大。
- 同一份 core 已支撑 Linux arm64 版本，macOS 只差平台层。
- Apache-2.0，vendored 进本仓库并保留 NOTICE；上游更新可 `git diff` 同步。

### 4.3 引擎路线（重要）

reunion-lite 的模型是 **YR 引擎跑一切**：

- 尤里的复仇战役 → `gamemd.exe`；
- RA2 原版战役 → 同一 `gamemd.exe` + `RA2` 模组（`Ra2Mode=False`，`Mod&AI/Mod/RA2`）；
- 任务包 → `gamemd.exe` + 对应 mod / Ares / Phobos / TX。

**真正的 `game.exe`（RA2 1.006）不在范围内**：它没有 CnCNet spawner 的
`-SPAWN` 入口，战役进度在 `RA2.ini`，地图/INI 布局也不同；reunion-lite 本身
只实现 `gamemd` 这条链路。已确认采用“RA2 战役走 YR 引擎 + RA2 mod”的方案，
不再做 `game.exe` 直启。

---

## 5. 关键技术决策

| # | 决策 | 理由 / 备注 |
|---|---|---|
| D1 | 启动器 = SwiftUI + SwiftPM，**不装 Xcode** | 机器只有 CLT；`swift build` 可编 SwiftUI，`.app` 用脚本组装 + ad-hoc 签名 |
| D2 | core 以 **v0.2** 为基线，vendored 到 `Sources/ReunionCore` | 0.1 在 macOS 上 0 可启动；v0.2 已有非 Windows 路径修正与 iconv 容错 |
| D3 | C ABI + JSON 桥接，不做 Swift/C++ 互操作 | 3,390 条任务 JSON 约 2–4 MB，序列化 < 20 ms；接口稳定、易测 |
| D4 | CrossOver **26.3.0**，bottle 类型 `win10_64` | 32 位依赖 `win32on64`；27 移除 |
| D5 | 游戏目录**不复制**进 bottle，用 `Z:` 盘 | 省 9.6 GB；`--workdir` 原生路径 + `--cx-app Z:\...` |
| D6 | 盘符映射 `D:` → `~/Documents`（可配置，见 `RL_DRIVE_MAP`） | 修复 42 条「林德拉克计划」任务；bottle 里也建 `d:` → `~/Documents` |
| D7 | `spawn.ini` 以 **GB18030/GBK** 写出；启动游戏时 `LANG=zh_CN.GBK` | 与 Windows 客户端一致；macOS 确实提供 `zh_CN.GBK` locale。P0 做 UTF-8 A/B |
| D8 | 用 `--wait-children` 等待整棵进程树 | spawner 会先退出/派生 `gamemd.exe`，只 `--wait` 会提前返回 |
| D9 | 每次启动重建 `Run` 覆盖层（`.rl_overlay` + `.rl_base`） | 与 Windows 客户端行为一致，避免任务包串味；`Run` 用硬链接，重建只花秒级 |
| D10 | cnc-ddraw 用独立 `ddraw.ini`（gdi 或 opengl、窗口化、1280×720、maxfps=60） | 现有 ini 有大量重复键且是 Windows 分辨率；Wine 下 GDI 最稳 |
| D11 | 渲染优先 `CnC_DDraw`（cnc-ddraw），失败再 `DDrawCompat` | DDrawCompat 走 D3D9→Metal，链路更长、风险更高 |
| D12 | 启动器非沙盒 + 用户选择目录 + 安全书签 | 需要读 `~/Documents`、启动外部进程；首次触发 TCC 授权 |
| D13 | 日志：`~/Library/Logs/ReunionLite/` + `Run/.rl_launch.log` | 含 wine 命令行、stdout/stderr、退出码，便于排错 |

### 5.1 启动命令形态（目标）

```bash
CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
cd "$HOME/Documents/ra2reunion/Run"
LANG=zh_CN.GBK "$CX/wine" --bottle RA2 --no-update --wait-children \
  --workdir "$HOME/Documents/ra2reunion/Run" \
  --cx-app 'Z:\Users\lf\Documents\ra2reunion\Run\gamemd-spawn.exe' \
  -SPAWN -LOG -CD -speedcontrol -b=27 -b=38 -b=44 -b=11 -AI-CONTROL -AFFINITY:1048575
```

- `extraArgs` 来自 `Resources/ClientDefinitions.ini` 的 `ExtraCommandLineParams`。
- Ares 任务改走 `Syringe.exe "gamemd.exe" -SPAWN ...`（P3 spike 验证注入是否可用）。
- 退出后把 `Run/RA2MD.ini` 同步回根目录、把存档移回任务包目录（复用 v0.2 逻辑）。

---

## 6. 目录与产物

```
~/vscode/reunion-lite-macos/
├── PLAN.md                      # 本文档
├── Package.swift                # SwiftPM：ReunionCore(C++) + ReunionLite(SwiftUI)
├── Sources/
│   ├── ReunionCore/             # vendored v0.2 core + platform/mac + rl_api
│   │   ├── include/rl_api.h     # C ABI（JSON 进出）
│   │   ├── core/                # 上游 core（保留 LICENSE/NOTICE）
│   │   └── platform/mac/        # encoding.cpp、fs_mac.cpp、runprep.cpp
│   └── ReunionLite/             # SwiftUI
│       ├── App.swift
│       ├── Models/Campaign.swift
│       ├── Services/CoreClient.swift、Launcher.swift、Config.swift
│       └── Views/…
├── Resources/                   # AppIcon.icns、默认 ddraw.ini、zh-Hans.lproj
├── scripts/
│   ├── build-app.sh             # swift build → .app + ad-hoc 签名
│   ├── setup-bottle.sh          # cxbottle 建 bottle、建 d: 链接、写配置
│   └── smoke-wine.sh            # P0 冒烟
└── docs/
    ├── TEST-MATRIX.md
    └── TROUBLESHOOTING.md
```

运行期文件：

```
~/Library/Application Support/ReunionLite/config.json   # 游戏目录、bottle、映射
~/Library/Logs/ReunionLite/launch-YYYYMMDD-HHMMSS.log   # 启动诊断
~/Documents/ra2reunion/Client/campaign.cache             # 与 Windows 客户端兼容的缓存（可选）
~/Documents/ra2reunion/Run/                              # 每次启动重建的覆盖层
```

---

## 7. 分阶段计划

> 工时按“一人、熟悉 C++/Swift、有 AI 辅助”估算；可与 P0 并行的提前标注。

### P0 · 环境与手动冒烟（0.5–1 天）

**目标**：先证明“CrossOver 26.3 能在这台机器上把 `gamemd-spawn.exe` 跑出窗口”，再写代码。

任务：

1. 安装 CrossOver 26.3.0，登录许可证，**关闭自动更新**，记录版本号。
2. 创建 bottle：
   ```bash
   CX=/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin
   "$CX/cxbottle" --bottle RA2 --create --template win10_64 --description "RA2/YR"
   "$CX/wine" --bottle RA2 --version
   ls "$HOME/Library/Application Support/CrossOver/Bottles/RA2/dosdevices/"
   ```
   确认 `z:` 存在；建 `d:` → `~/Documents`（即 D: 盘根；`D:\ra2master\...` 才能解析）。
3. 用**现有 Windows 客户端生成的** `Run/spawn.ini` 直接冒烟（先不接启动器）：
   ```bash
   cd ~/Documents/ra2reunion/Run
   LANG=zh_CN.GBK "$CX/wine" --bottle RA2 --no-update --wait-children \
     --workdir "$PWD" --cx-app 'Z:\Users\lf\Documents\ra2reunion\Run\gamemd-spawn.exe' -SPAWN
   ```
4. 记录结果：是否出窗口、是否进任务、分辨率/鼠标/声音、`wine` stderr。
5. 若失败：换 `ddraw.ini` 的 `renderer=gdi` / `opengl`、`windowed=true`、
   `width/height=1280/720`，开 `--debugmsg +ddraw`；仍失败则先解决问题再进入 P1。

**验收**：能进到任务加载画面或主菜单；截图 + 命令 + 日志归档到 `docs/`。
**回滚**：删除 bottle，CrossOver 不影响系统。

### P1 · core 移植 + 原生 CLI（2–3 天，可与 P0 并行）

**目标**：不依赖 Wine，在 mac 上完成扫描、选任务、生成启动计划。

任务：

1. Vendor v0.2 `src/core`，保留 LICENSE/NOTICE；加 `platform/mac/encoding.cpp`
   （直接复用 v0.2 Linux 的 iconv GB18030 容错实现）。
2. `fs_mac.cpp`：`ListIniFileStamps`（`stat` 取 size/mtime），保证 cache 指纹稳定；
   注意 Windows FILETIME 与 mac `timespec` 的 epoch 不同，缓存只在 mac 内部复用。
3. 路径层：
   - 所有 `\` → `/`；`Join` 输出统一规范化；
   - **盘符映射**：`D:\ra2master` → `~/Documents/ra2master`，未知盘符给明确报错；
   - 相对路径按游戏根目录解析。
4. C ABI `rl_api.h`：`rl_scan`、`rl_plan`、`rl_prepare`、`rl_launch_spec`、
   `rl_free`；返回 JSON（UTF-8），错误带人类可读中文。
5. CLI `rlctl`：`scan` / `plan <mission>` / `prepare <mission>` / `launch --dry`，
   用于无 UI 回归。
6. 修掉扫描的 43 条排除（映射后应为 3,389/3,390，剩 1 条缺目录的保留排除）。

**验收**：
- `rlctl scan` 输出 3,390 条、可启动 ≥ 3,389；
- `rlctl plan TRN01T` 的 `spawn.ini` 与 Windows 客户端一致（逐字段 diff）；
- 3,390 条全量 `plan` 成功率与 Windows 侧一致（参考 `audit_test` 思路）。

### P2 · Run 准备移植（2–3 天）

**目标**：`rlctl prepare` 产出的 `Run/` 与 Windows 客户端等价。

任务（从 v0.2 `platform/linux/runprep.cpp` 移植）：

1. 基础文件硬链接（`link()`，跨卷/失败回退 copy）+ `.rl_base` 标记；
2. `.rl_overlay` 清理、陈旧 `.MAP`/CSF 清理、`thememd.*` 备份/恢复；
3. mod / 任务目录覆盖（含 CSF 平铺到根、`ra2md.ini` 不覆盖用户设置）；
4. 同包全部 `.MAP` 的兄弟地图准备（续关）；
5. 地图补丁（资金、自动存档、数值修复、军备反转、透明迷雾 `shroud.shp`）；
6. Ares / Phobos / TX 挂载；
7. `RA2MD.ini` 选项写入（音乐音量、`SkipAnimation`）；
8. `spawn.ini` 同时写根目录与 `Run/`（GBK）；
9. 存档按任务包隔离（`.rl_saves` 标记 + 进/出搬移）；
10. **Wine 路径**：`spawn.ini` 的 `Game=` / `Mission=` 用 Windows 形式
    （`D:\...` 或 `Z:\...`），不要写 `/Users/...`。

**验收**：
- 同一任务下与 Windows 客户端的 `Run/` 做文件清单差分（名称/大小/数量）；
- 连续启动 10 次不残留旧任务包文件；
- `Run` 占用从 2.0 GB 降到接近 0（硬链接）且游戏可正常读。

### P3 · Wine 启动集成（1–2 天）

**目标**：`rlctl launch` 一条命令完成“准备 + 启动 + 等待 + 收尾”。

任务：

1. 组装命令行（§5.1），`cwd=Run`，环境变量：`LANG`、`WINEDEBUG`、
   `CX_BOTTLE` 不设（用 `--bottle`）；
2. `Process` 等待 `--wait-children` 退出；超时/崩溃检测；退出码记录；
3. 退出后 `SyncSettingsBack`（`RA2MD.ini` + 存档）；
4. 单实例锁（`.rl_lock`），避免重复启动；
5. 诊断：把完整命令行、wine stdout/stderr、`Run/.rl_overlay` 清单写入日志；
6. Ares 路径 spike：`Syringe.exe "gamemd.exe" -SPAWN ...` 是否能注入；
   不行则 UI 明确提示“该任务需要 Ares，当前 Wine 路径不支持”，不静默失败。

**验收**：从 CLI 启动一关 → 玩到退出 → 设置/存档正确回写；日志可复现问题。

### P4 · SwiftUI 启动器（3–4 天）

**目标**：替代终端，完整可用、轻量。

任务：

1. `Package.swift`：`ReunionCore`（C++17 + `-liconv`）+ `ReunionLite`（SwiftUI）。
2. 数据层：`CoreClient` 调 C ABI，`JSONDecoder` 映射 `Mission/Pack/Options`；
   扫描在后台队列，缓存命中直接出列表。
3. 界面（参考 Windows 客户端布局，但用原生控件）：
   - 左：搜索框 + 任务包下拉 + 任务表（名称/包/阵营/难度/场景）；
   - 右：模组、难度、初始资金、游戏速度、自动存档、音乐、播放简报、
     透明迷雾、Ares、Phobos、地形扩展、自动维修、数值修复、军备反转；
   - 底：状态栏（条数/缓存命中/上次启动结果）、启动、重新扫描、打开日志；
   - 首次运行：选游戏目录（`NSOpenPanel`）+ 选 bottle + 自检（文件齐备、Wine 可执行）。
4. 启动流程：按钮 → 禁用 UI + 进度 → `rl_prepare` → `Process` → 恢复 UI + 结果提示。
5. 轻量约束：无 WebView/Electron/Qt；启动 < 1 s；空转 CPU 0；内存 < 100 MB。
6. 细节：中文界面、暗色模式、HiDPI、⌘R 重新扫描、⌘↩ 启动、错误用 sheet 展示
   可复制日志。

**验收**：全程不碰终端完成“选任务 → 启动 → 退出 → 下一关”；崩溃/失败有可读原因。

### P5 · 打包与安装（0.5 天）

1. `scripts/build-app.sh`：`swift build -c release` → 组装 `ReunionLite.app`
   （`Info.plist`、`AppIcon.icns`、`zh-Hans.lproj`）→ `codesign --force -s -`。
2. 首次运行说明：Gatekeeper（右键打开）、Documents 授权、CrossOver 许可证。
3. `scripts/setup-bottle.sh`：幂等创建 bottle、`d:` 链接、默认 `ddraw.ini`。
4. 产物：`dist/ReunionLite-0.1-macos-arm64.zip`（不含游戏文件）。

### P6 · 验收测试矩阵（1.5–2 天）

见 §9。每项记录：任务名、结果、日志、截图；失败项进 `docs/TROUBLESHOOTING.md`。


## 8. 风险与对策

| # | 风险 | 概率 | 影响 | 对策 |
|---|---|---|---|---|
| R1 | CrossOver 自动升级到 27，32 位失效 | 中 | 高 | 关闭自动更新；记录 26.3；保留安装包；升级前先测 bottle |
| R2 | macOS 27 + Rosetta 下 Wine 窗口/输入异常 | 中 | 高 | P0 先冒烟；cnc-ddraw 窗口化 + gdi；必要时锁 1280×720 |
| R3 | 32 位 `win32on64` 在新系统失效 | 低 | 致命 | 锁定 26.3 并保留安装包/许可证；先用最小 32 位程序验证；不使用 Parallels/虚拟机兜底 |
| R4 | `spawn.ini`/地图 GBK 编码不匹配，任务路径乱码 | 中 | 高 | 写 GBK + `LANG=zh_CN.GBK`；P0 做 UTF-8 A/B；路径尽量 ASCII |
| R5 | 军械/任务包引用 `D:\ra2master` 等旧盘符 | 确定 | 中 | 盘符映射表 + bottle `d:` 链接；UI 显示映射结果 |
| R6 | `Run` 硬链接失败（跨卷/权限） | 低 | 中 | 回退 copy（v0.2 已有）；首次提示耗时 |
| R7 | Ares/Syringe 注入在 Wine 下失败 | 中 | 中 | P3 spike；失败时任务列表标注“需 Ares”；不阻塞主流程 |
| R8 | 过场 Bink 视频/音乐异常 | 中 | 低 | 可关“播放简报/过场”；音乐音量写 `RA2MD.ini`；记录已知问题 |
| R9 | Documents TCC 首次拒绝 | 中 | 中 | 首次运行引导 + 系统设置深链；安全书签保存授权 |
| R10 | 无 Xcode，SwiftUI 打包踩坑 | 低 | 中 | SPM + 手工 bundle；先做 20 行 Hello SwiftUI 验证 |
| R11 | APFS 大小写敏感（外接盘） | 低 | 高 | 启动自检：`Maps/CP` 与 `Maps/Cp` 大小写探测，失败明确提示 |
| R12 | 游戏路径含中文/空格 | 中 | 中 | 全链路 UTF-8 + 引号；测试一个中文目录 |
| R13 | 缓存格式与 Windows 客户端冲突 | 低 | 低 | 指纹不匹配自动重建；缓存可配置到 App Support |
| R14 | 磁盘占用被用户介意 | 确定 | 低 | 不复制游戏；`Run` 硬链接化；提供卸载脚本 |

---

## 9. 测试矩阵（P6）

| # | 场景 | 任务样例 | 关注点 |
|---|---|---|---|
| T1 | 原版训练 | `TRN01T` / `TRN02T` | 简报、无视频、能进图 |
| T2 | RA2 盟军 | `ALL01T` / `ALL03U` | RA2 mod 加载、中文 |
| T3 | RA2 苏军 | `SOV01T` | 同上 |
| T4 | 尤里复仇 | `YUR01T`（若有） | YR 数据、CSF |
| T5 | 林德拉克计划 ×3 | `Mod&AI「林德拉克计划…」` | `D:` 映射、42 条恢复 |
| T6 | 中文名任务包 | 任选 | 路径/编码 |
| T7 | Ares 任务 | 列表筛选 | Syringe 注入 |
| T8 | Phobos 任务 | 列表筛选 | 与 Ares 组合 |
| T9 | 透明迷雾 | 选项开 | `shroud.shp` |
| T10 | 军备反转 | 选项开 | 地图单位替换 |
| T11 | 数值修复/自动维修 | 选项开 | `mapCodeInis` 合并 |
| T12 | 初始资金/速度/难度 | 各档 | 地图 `[Basic]` 与 `spawn.ini` |
| T13 | 自动存档 | 5/10/20 分钟 | `Saved Games` |
| T14 | 音乐/过场 | 开关 | `RA2MD.ini`、Bink |
| T15 | 存档续关 | 打完一关进下一关 | 同包兄弟地图、存档搬移 |
| T16 | 窗口/全屏/分辨率 | 1280×720 / 2560×1440 | cnc-ddraw |
| T17 | 连续启动 10 次 | 不同包交替 | 覆盖层清理、无残留 |
| T18 | 异常路径 | 改错 game root / bottle | 错误提示、不崩溃 |
| T19 | 中文目录 | 复制到 `~/文档/ra2reunion` | TCC、编码、Wine 路径 |
| T20 | 退出回写 | 改游戏内设置后退出 | `RA2MD.ini` 同步 |

---

## 10. 里程碑与工期

| 里程碑 | 内容 | 累计 |
|---|---|---|
| M0 | CrossOver 装好，手动进一关 | 0.5–1 d |
| M1 | `rlctl` 能扫描 + 准备 `Run` + 启动 | 4–6 d |
| M2 | SwiftUI 能完整启动一关 | 7–10 d |
| M3 | 选项/存档/任务包全覆盖 | 10–14 d |
| M4 | 打包 + 回归 + 文档 | 11–16 d |

可并行：P1/P2 不依赖 CrossOver，P0 装环境时即可开工；P4 可先用假数据搭 UI。

---

## 11. 立即可执行的下一步

1. **你安装 CrossOver 26.3.0**（约 1.3 GB），建 `RA2` bottle；装好后告诉我版本号和
   `wine --version` 输出，我们跑 §7 P0 的冒烟命令。
2. **我这边同时开工 P1/P2**（不依赖 Wine）：vendor v0.2 core、mac 平台层、
   盘符映射、`rlctl` 扫描/准备 `Run`。目标是在你装好 CrossOver 之前，
   `rlctl scan` 已经是 3,389/3,390 可启动、`rlctl prepare` 能产出与 Windows
   客户端等价的 `Run/`。
3. 已确认采用「RA2 战役走 YR 引擎 + RA2 mod」，不做 `game.exe` 直启；
   「林德拉克计划」按 `D:` → `~/Documents` 映射执行（已实测：可启动 3,389/3,390）。

---

## 12. 证据附录（本次预研实测）

```
# 本机原生编译 v0.2 core 后扫描
fingerprint files=1246 hash=bfc7bb710d597ede 12.8ms
packs=1247 missions=3390 issues=0 scan=150.0ms
launchable=3347 excluded=43
 14  任务目录不存在：D:\ra2master\「林德拉克计划 3...
 14  任务目录不存在：D:\ra2master\「林德拉克计划+1...
 14  任务目录不存在：D:\ra2master\「林德拉克计划+2...
  1  任务目录不存在：Mod&AI\1996068576846557185

# 本地 0.1 core 同样方式：launchable=0 excluded=3390（反斜杠未转换）

# 游戏二进制
gamemd-spawn.exe: PE32 executable (GUI) Intel 80386
gamemd.exe:       PE32 executable (GUI) Intel 80386

# CrossOver 26.3.0 包内
bin/wineloader: Mach-O 64-bit executable x86_64
lib/wine/i386-windows/kernel32.dll: PE32 executable (DLL) Intel 80386
bin/wine --help: --bottle / --cx-app / --workdir / --wait-children / --env ...
bin/cxbottle --create --template win10_64 ...
```

参考：

- 上游仓库：<https://github.com/yeungtuzi/reunion-lite>（v0.2）
- CrossOver 27 移除 32 位：<https://www.codeweavers.com/blog/mjohnson/2026/06/11/whats-in-and-whats-out-for-crossover-27>
- CrossOver 架构（win32on64 / Rosetta）：<https://github.com/stoicswe/endfield_finewine/blob/main/docs/project-information/03-crossover-wine-architecture.md>
- CrossOver 终端运行：<https://codeweavers.helpjuice.com/en_US/run-a-windows-app-from-terminal>
- 上游 ARM64 Linux 指南（仅作平台层参考，不采用其运行方式）：<https://github.com/yeungtuzi/reunion-lite/blob/v0.2/docs/ARM64-LINUX-MT1000.md>
