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

# --- host side: (re)start the dedicated Xorg and a WM -----------------------
# A long-lived :3 goes stale after VT switches: xdpyinfo still works but
# winex11 fails to load ("no driver could be loaded") and the game runs with no
# window. Restarting the dedicated display on every launch is cheap and makes
# the launch deterministic.
if [ "$DISP" != ":0" ]; then
    for pid in $(pgrep -f "Xorg $DISP" 2>/dev/null); do sudo -n kill "$pid" 2>/dev/null; done
    sleep 2
    for pid in $(pgrep -f "Xorg $DISP" 2>/dev/null); do sudo -n kill -9 "$pid" 2>/dev/null; done
    pkill -x mutter 2>/dev/null
    sleep 1
    sudo -n setsid Xorg "$DISP" vt8 -config /opt/steamos/xorg-m1000.conf \
        -noreset -nolisten tcp -ac -logfile "$W/xorg-m1000.log" >/dev/null 2>&1 &
    for _ in $(seq 1 40); do DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1 && break; sleep 0.5; done
    DISPLAY="$DISP" setsid mutter --replace >/dev/null 2>&1 &
    sleep 2
fi

export STEAM_COMPAT_CLIENT_INSTALL_PATH="$S"
export STEAM_COMPAT_DATA_PATH="$C"
# The runtime only maps the X socket that matches DISPLAY, so advertise the
# display we actually want *before* entering the container (with :0 it maps
# only X0 and winex11 cannot open :3 -> "no driver could be loaded").
export DISPLAY="$DISP"
if [ "$DISP" = ":0" ]; then
    export XAUTHORITY="${XAUTHORITY:-$(ls /run/user/1000/xauth_* 2>/dev/null | head -1)}"
else
    unset XAUTHORITY
fi
exec "$FEX/fex-compat-tool" waitforexitandrun -- \
    "$RT/_v2-entry-point" --verb=waitforexitandrun -- \
    "$0" --inside "$@"
