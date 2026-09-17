#!/bin/bash
# ---------------------------------------------------------------------------
# Run a complete GXDE (Deepin) desktop inside a NESTED FlakeWM session.
#
# The nested session is isolated from the host session:
#   * its own XDG_RUNTIME_DIR          -> $ROOT/run
#   * its own D-Bus session bus        -> $ROOT/run/bus (dbus-daemon)
#   * its own XDG_CONFIG/CACHE/DATA    -> $ROOT/{config,cache,data}
#   * its own Wayland/X11 sockets      -> wayland-1 (see wayland.lock), :N
#
# Only the host Wayland socket and the system bus are shared with the host,
# because a nested compositor has to present to the host compositor and
# logind/udev/polkit live on the system bus.
#
# This mirrors the host GXDE session entry:
#   /usr/share/wayland-sessions/gxde-wlcom.desktop -> /usr/bin/startgxde_wlcom
#   exec /usr/bin/gxde-wlcom -s /usr/bin/startdde
# with gxde-wlcom replaced by ./build/flakewm.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

WM_BIN="${FLAKEWM_BIN:-$REPO_ROOT/build/flakewm}"
ROOT="${FLAKEWM_NESTED_ROOT:-$REPO_ROOT/.nested}"
SESSION_CMD="${FLAKEWM_SESSION_CMD:-/usr/bin/startdde}"

# Extra CLIs the session needs on PATH (the host session does the same).
export PATH="$PATH:/sbin:/usr/sbin"

die() {
    echo "nested-gxde-session: $*" >&2
    exit 1
}

[ -x "$WM_BIN" ] || die "compositor binary not found or not executable: $WM_BIN"
[ -x "$SESSION_CMD" ] || [ -x "$(command -v "$SESSION_CMD" 2>/dev/null || true)" ] ||
    die "session command not found: $SESSION_CMD"
command -v dbus-daemon >/dev/null || die "dbus-daemon not found"

# ---------------------------------------------------------------------------
# Host display. XDG_RUNTIME_DIR is replaced by the isolated one below, so the
# host Wayland socket is referenced through the absolute path.
# ---------------------------------------------------------------------------
HOST_RUNTIME="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
HOST_WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
HOST_WAYLAND_PATH="$HOST_RUNTIME/$HOST_WAYLAND_DISPLAY"
[ -S "$HOST_WAYLAND_PATH" ] || die "host Wayland socket not found: $HOST_WAYLAND_PATH"

# ---------------------------------------------------------------------------
# Isolated runtime tree.
# ---------------------------------------------------------------------------
RUN_DIR="$ROOT/run"
LOG_DIR="$ROOT/log"
CONFIG_DIR="$ROOT/config"
CACHE_DIR="$ROOT/cache"
DATA_DIR="$ROOT/data"
STATE_DIR="$ROOT/state"

mkdir -p "$RUN_DIR" "$LOG_DIR" "$CONFIG_DIR" "$CACHE_DIR" "$DATA_DIR" "$STATE_DIR"
chmod 700 "$RUN_DIR"
rm -f "$RUN_DIR"/wayland-*.lock "$RUN_DIR"/bus.pid

WM_LOG="$LOG_DIR/wm.log"
SESSION_LOG="$LOG_DIR/session.log"
: >"$WM_LOG"
: >"$SESSION_LOG"

WM_PID=""
BUS_PID=""

cleanup() {
    set +e
    [ -n "$WM_PID" ] && kill -TERM "$WM_PID" 2>/dev/null
    sleep 1
    [ -n "$WM_PID" ] && kill -KILL "$WM_PID" 2>/dev/null
    [ -n "$BUS_PID" ] && kill -TERM "$BUS_PID" 2>/dev/null
}
trap cleanup EXIT INT TERM

# ---------------------------------------------------------------------------
# Private D-Bus session bus.
# ---------------------------------------------------------------------------
echo "nested-gxde-session: starting private session bus at unix:path=$RUN_DIR/bus"
# --print-address goes to fd 1 (captured below), --print-pid goes to fd 3.
dbus-daemon --session --fork \
    --address="unix:path=$RUN_DIR/bus" \
    --print-address=1 --print-pid=3 \
    3>"$RUN_DIR/bus.pid" 2>"$LOG_DIR/dbus.log" \
    | { read -r address; printf '%s' "$address" >"$RUN_DIR/bus.address"; }
DBUS_SESSION_BUS_ADDRESS="$(cat "$RUN_DIR/bus.address" 2>/dev/null || true)"
BUS_PID="$(cat "$RUN_DIR/bus.pid" 2>/dev/null || true)"
[ -n "$DBUS_SESSION_BUS_ADDRESS" ] || die "failed to start private session bus"
echo "nested-gxde-session: session bus $DBUS_SESSION_BUS_ADDRESS (pid $BUS_PID)"

# ---------------------------------------------------------------------------
# Isolated session environment, mirroring startgxde_wlcom.
# ---------------------------------------------------------------------------
export XDG_RUNTIME_DIR="$RUN_DIR"
export XDG_CONFIG_HOME="$CONFIG_DIR"
export XDG_CACHE_HOME="$CACHE_DIR"
export XDG_DATA_HOME="$DATA_DIR"
export XDG_STATE_HOME="$STATE_DIR"
export DBUS_SESSION_BUS_ADDRESS
unset WAYLAND_DISPLAY WAYLAND_SOCKET

export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=Deepin
export DDE_CURRENT_COMPOSITOR=GXWM
export KYWC_USE_LAYOUT_MANAGER=1
export QT_QPA_PLATFORM=wayland
unset GTK_IM_MODULE DTK2_XWAYLAND

# Advertise the isolated environment to D-Bus activated services.
if command -v dbus-update-activation-environment >/dev/null 2>&1; then
    dbus-update-activation-environment \
        XDG_RUNTIME_DIR XDG_CONFIG_HOME XDG_CACHE_HOME XDG_DATA_HOME \
        XDG_STATE_HOME XDG_SESSION_TYPE XDG_SESSION_DESKTOP \
        DDE_CURRENT_COMPOSITOR KYWC_USE_LAYOUT_MANAGER QT_QPA_PLATFORM \
        LANG LANGUAGE LC_ALL HOME PATH 2>/dev/null || true
fi

# ---------------------------------------------------------------------------
# Nested compositor: presents to the host compositor, then runs the session.
# ---------------------------------------------------------------------------
echo "nested-gxde-session: host display    $HOST_WAYLAND_PATH"
echo "nested-gxde-session: nested display  $RUN_DIR/wayland-*"
echo "nested-gxde-session: session         $SESSION_CMD"
echo "nested-gxde-session: logs            $LOG_DIR"

XDG_RUNTIME_DIR="$HOST_RUNTIME" WAYLAND_DISPLAY="$HOST_WAYLAND_DISPLAY" \
    "$WM_BIN" --nested --session "$SESSION_CMD" >"$WM_LOG" 2>&1 &
WM_PID=$!

# ---------------------------------------------------------------------------
# Session process tree: everything the compositor forks shares its process
# group, so one kill takes the whole nested desktop down.
# ---------------------------------------------------------------------------
wait "$WM_PID"
