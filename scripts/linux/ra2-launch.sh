#!/bin/bash
# Launch the prepared Run directory with Proton Experimental's x86_64 Wine,
# inside the Steam Linux Runtime so the 32-bit X11 libraries are available.
#
# Why not `steam -applaunch`: Steam truncates shortcut appids to 24 bits on the
# command line (3676375577 -> 2165273) and Proton's built-in steam.exe aborts in
# lsteamclient (assert "!status") on this box, so the game never starts.
#
# Why not Proton 11 (ARM64): that Wine has no 32-bit x86 support; gamemd dies in
# wow64 before it can create a window.
#
# Why :3: KWin's rootless XWayland does not map Wine toplevel windows, so a
# dedicated Xorg is used (same workaround as the Hollow Knight runner).
#
# Usage: ra2-launch.sh [-SPAWN] [extra gamemd args]
# Env: REUNION_DISPLAY (:3), REUNION_WINEPREFIX, REUNION_COMPATDATA, REUNION_EXE
set -u
S=${STEAM_ROOT:-/opt/steamos/steam}
P=${REUNION_PROTON:-$S/steamapps/common/Proton - Experimental}
C=${REUNION_COMPATDATA:-$S/steamapps/compatdata/3676375577}
RT="$S/steamapps/common/SteamLinuxRuntime_4"
FEX="$S/steamapps/common/FEX-Emu"
DISP=${REUNION_DISPLAY:-:3}
W=${M1000_WORK_DIR:-$HOME/.local/share/m1000/work}

if [ "${1:-}" = "--inside" ]; then
    shift
    export DISPLAY="$DISP" XDG_RUNTIME_DIR=/run/user/1000
    if [ "$DISP" = ":0" ]; then
        export XAUTHORITY="${XAUTHORITY:-$(ls /run/user/1000/xauth_* 2>/dev/null | head -1)}"
    else
        export XAUTHORITY=
    fi
    export WINEPREFIX="${REUNION_WINEPREFIX:-$C/pfx}"
    export WINESERVER="$P/files/bin/wineserver"
    export WINEDLLPATH="$P/files/lib/wine/x86_64-windows:$P/files/lib/wine/i386-windows"
    export LD_LIBRARY_PATH="$P/files/lib/x86_64-linux-gnu:$P/files/lib/i386-linux-gnu:$P/files/lib:${LD_LIBRARY_PATH:-}"
    export WINEDLLOVERRIDES="ddraw=n${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
    export FEX_TSOENABLED="${FEX_TSOENABLED:-0}"
    export STEAM_COMPAT_CLIENT_INSTALL_PATH="$S"
    exec "$FEX/usr/bin/FEX" "$P/files/bin/wine" "${REUNION_EXE:-gamemd-spawn.exe}" "$@"
fi

# --- host side: make sure the dedicated Xorg and a WM exist -----------------
if [ "$DISP" != ":0" ]; then
    if ! DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1; then
        sudo -n setsid Xorg "$DISP" vt8 -config /opt/steamos/xorg-m1000.conf \
            -noreset -nolisten tcp -ac -logfile "$W/xorg-m1000.log" >/dev/null 2>&1 &
        for _ in $(seq 1 40); do DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1 && break; sleep 0.5; done
    fi
    if ! pgrep -x mutter >/dev/null 2>&1; then
        DISPLAY="$DISP" setsid mutter --replace >/dev/null 2>&1 &
        sleep 2
    fi
fi

export STEAM_COMPAT_CLIENT_INSTALL_PATH="$S"
export STEAM_COMPAT_DATA_PATH="$C"
export DISPLAY=:0
export XAUTHORITY="${XAUTHORITY:-$(ls /run/user/1000/xauth_* 2>/dev/null | head -1)}"
exec "$FEX/fex-compat-tool" waitforexitandrun -- \
    "$RT/_v2-entry-point" --verb=waitforexitandrun -- \
    "$0" --inside "$@"
