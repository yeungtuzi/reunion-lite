#!/bin/bash
# Install a menu entry and the game icon (extracted from the game itself, so no
# copyrighted artwork is shipped in the repository).
#
#   ./scripts/linux/install-desktop.sh <game-root> [binary]
#
# Default binary: build/reunion-lite-qt next to this repository.
set -euo pipefail

HERE=$(cd "$(dirname "$0")/../.." && pwd)
GAME=${1:-}
BIN=${2:-$HERE/build/reunion-lite-qt}

[ -n "$GAME" ] || { echo "用法: $0 <游戏目录> [可执行文件]"; exit 1; }
[ -x "$BIN" ] || { echo "找不到可执行文件: $BIN"; exit 1; }
BIN=$(readlink -f "$BIN")

data=${XDG_DATA_HOME:-$HOME/.local/share}
apps="$data/applications"
icons="$data/icons/hicolor"

icon_src=""
for c in "$GAME/Resources/clienticon.ico" "$GAME/clienticon.ico"; do
    [ -f "$c" ] && icon_src=$c && break
done

if [ -n "$icon_src" ] && command -v convert >/dev/null 2>&1; then
    for size in 16 24 32 48 64 128 256; do
        mkdir -p "$icons/${size}x${size}/apps"
        convert "$icon_src[0]" -resize "${size}x${size}" \
            "$icons/${size}x${size}/apps/reunion-lite.png"
    done
    echo "图标已安装: $icons/*/apps/reunion-lite.png"
else
    echo "未找到 clienticon.ico（或缺少 ImageMagick），跳过图标"
fi

mkdir -p "$apps"
cat > "$apps/reunion-lite.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Reunion Lite
Name[zh_CN]=重聚 Lite
Comment=红色警戒2 重聚 启动器
Exec="$BIN" "$GAME"
Icon=reunion-lite
Terminal=false
StartupWMClass=reunion-lite-qt
Categories=Game;StrategyGame;
DESKTOP
echo "菜单项已安装: $apps/reunion-lite.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$apps" || true
