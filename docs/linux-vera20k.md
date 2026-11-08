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

## 已验证

aibook（Ubuntu 24.04 arm64 + M1000）上：

* 任务列表 3389/3390 可启动（唯一缺失项是确实不存在的 `Mod&AI\1996068576846557185`）
* 从启动器选任务 → 启动 → VERA20K 载入地图、进入游戏、模拟正常运行
