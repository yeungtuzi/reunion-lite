#!/bin/bash
# Launch a prepared Run/ directory (gamemd-spawn.exe -SPAWN) on ARM64 Linux.
#
# Why this shape:
#  * gamemd.exe is 32-bit x86. Proton 11 (ARM64)'s Wine has no 32-bit x86
#    support, so we need an x86_64 Wine under FEX.
#  * That Wine's 32-bit X11/winex11 libraries only exist inside the Steam Linux
#    Runtime, so the game is started through SteamLinuxRuntime_4.
#  * KWin's rootless XWayland does not map Wine toplevel windows on some
#    drivers, so a dedicated Xorg display is used (default :3).
#
# Usage: ra2-launch.sh [-SPAWN] [extra gamemd args]
# Env:
#   REUNION_DISPLAY     X display to use (default :3; ":0" = current desktop)
#   REUNION_COMPATDATA  Steam compatdata dir whose pfx is used
#   REUNION_WINEPREFIX  explicit Wine prefix (overrides compatdata/pfx)
#   REUNION_PROTON      Proton dir (default: Proton - Experimental)
#   REUNION_EXE         exe to run (default gamemd-spawn.exe; Syringe.exe for Ares)
#   REUNION_XORG_CONF   Xorg config (default: /opt/steamos/xorg-m1000.conf if
#                       present, else a generated 1920x1080 modesetting config)
set -u

S=${STEAM_ROOT:-}
if [ -z "$S" ]; then
    for d in "$HOME/.local/share/Steam" "$HOME/.steam/steam" /opt/steamos/steam; do
        [ -d "$d/steamapps" ] && S=$d && break
    done
fi
[ -n "$S" ] || { echo "ra2-launch: Steam install not found; set STEAM_ROOT" >&2; exit 2; }

P=${REUNION_PROTON:-$S/steamapps/common/Proton - Experimental}
RT="$S/steamapps/common/SteamLinuxRuntime_4"
FEX="$S/steamapps/common/FEX-Emu"
DISP=${REUNION_DISPLAY:-:3}
W=${M1000_WORK_DIR:-$HOME/.local/share/m1000/work}
mkdir -p "$W"

C=${REUNION_COMPATDATA:-}
if [ -z "$C" ]; then
    if [ -d "$S/steamapps/compatdata/3676375577/pfx" ]; then
        C="$S/steamapps/compatdata/3676375577"
    else
        for d in "$S"/steamapps/compatdata/*/; do
            [ -f "$d/pfx/system.reg" ] && C=${d%/} && break
        done
    fi
fi
if [ -z "$C" ] && [ -z "${REUNION_WINEPREFIX:-}" ]; then
    echo "ra2-launch: no Proton prefix found; run the game once through Steam or set REUNION_COMPATDATA" >&2
    exit 2
fi
export REUNION_COMPATDATA="$C"
export REUNION_DISPLAY="$DISP"

if [ "${1:-}" = "--inside" ]; then
    shift
    export DISPLAY="$DISP" XDG_RUNTIME_DIR="/run/user/$(id -u)"
    if [ "$DISP" = ":0" ]; then
        export XAUTHORITY="${XAUTHORITY:-$(ls /run/user/$(id -u)/xauth_* 2>/dev/null | head -1)}"
    fi
    export WINEPREFIX="${REUNION_WINEPREFIX:-$C/pfx}"
    export WINESERVER="$P/files/bin/wineserver"
    export WINEDLLPATH="$P/files/lib/wine/x86_64-windows:$P/files/lib/wine/i386-windows"
    export LD_LIBRARY_PATH="$P/files/lib/x86_64-linux-gnu:$P/files/lib/i386-linux-gnu:$P/files/lib:${LD_LIBRARY_PATH:-}"
    export WINEDLLOVERRIDES="ddraw=n${WINEDLLOVERRIDES:+;$WINEDLLOVERRIDES}"
    export FEX_TSOENABLED="${FEX_TSOENABLED:-0}"
    exec "$FEX/usr/bin/FEX" "$P/files/bin/wine" "${REUNION_EXE:-gamemd-spawn.exe}" "$@"
fi

# --- host side: (re)start the dedicated Xorg and a WM -----------------------
# A long-lived display goes stale after VT switches: xdpyinfo still answers but
# winex11 fails to load ("no driver could be loaded") and the game runs with no
# window. Restarting the dedicated display on every launch is cheap and makes
# the launch deterministic.
if [ "$DISP" != ":0" ]; then
    XORG_CONF=${REUNION_XORG_CONF:-}
    if [ -z "$XORG_CONF" ]; then
        if [ -f /opt/steamos/xorg-m1000.conf ]; then
            XORG_CONF=/opt/steamos/xorg-m1000.conf
        else
            XORG_CONF="$W/xorg-1920x1080.conf"
            [ -f "$XORG_CONF" ] || cat > "$XORG_CONF" <<'CONF'
Section "Device"
    Identifier "gpu"
    Driver "modesetting"
EndSection
Section "Monitor"
    Identifier "mon"
EndSection
Section "Screen"
    Identifier "scr"
    Device "gpu"
    Monitor "mon"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Modes "1920x1080"
    EndSubSection
EndSection
CONF
        fi
    fi
    for pid in $(pgrep -f "Xorg $DISP" 2>/dev/null); do sudo -n kill "$pid" 2>/dev/null; done
    sleep 2
    for pid in $(pgrep -f "Xorg $DISP" 2>/dev/null); do sudo -n kill -9 "$pid" 2>/dev/null; done
    pkill -x mutter 2>/dev/null
    sleep 1
    sudo -n setsid Xorg "$DISP" vt8 -config "$XORG_CONF" \
        -noreset -nolisten tcp -ac -logfile "$W/xorg-$DISP.log" >/dev/null 2>&1 &
    for _ in $(seq 1 40); do DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1 && break; sleep 0.5; done
    if ! DISPLAY="$DISP" xdpyinfo >/dev/null 2>&1; then
        echo "ra2-launch: failed to start Xorg on $DISP (see $W/xorg-$DISP.log)" >&2
        exit 2
    fi
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
    export XAUTHORITY="${XAUTHORITY:-$(ls /run/user/$(id -u)/xauth_* 2>/dev/null | head -1)}"
else
    unset XAUTHORITY
fi

# Show the game on the physical screen while it runs, then come back.
if [ "$DISP" != ":0" ]; then
    sudo -n chvt 8 2>/dev/null
fi
"$FEX/fex-compat-tool" waitforexitandrun -- \
    "$RT/_v2-entry-point" --verb=waitforexitandrun -- \
    "$0" --inside "$@" &
child=$!
wait "$child"
rc=$?
if [ "$DISP" != ":0" ]; then
    sudo -n chvt 2 2>/dev/null
fi
exit $rc
