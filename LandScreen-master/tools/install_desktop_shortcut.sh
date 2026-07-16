#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UI_PROGRAM="$PROJECT_DIR/build/planescreen"
LAUNCHER_DIR="$HOME/.local/bin"
LAUNCHER="$LAUNCHER_DIR/nuedc-ground-station-ui"
DESKTOP_DIR="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
DESKTOP_DIR="${DESKTOP_DIR:-$HOME/Desktop}"
DESKTOP_FILE="$DESKTOP_DIR/nuedc-ground-station-ui.desktop"

mkdir -p "$LAUNCHER_DIR" "$DESKTOP_DIR"

cat >"$LAUNCHER" <<EOF
#!/usr/bin/env bash
if pgrep -x planescreen >/dev/null 2>&1; then
    command -v notify-send >/dev/null 2>&1 && notify-send "地面站 UI" "界面已经打开，请查看任务栏。"
    exit 0
fi
if [ ! -x "$UI_PROGRAM" ]; then
    command -v notify-send >/dev/null 2>&1 && notify-send -u critical "地面站 UI 启动失败" "请先编译 $UI_PROGRAM"
    exit 1
fi
cd "$PROJECT_DIR/build"
exec "$UI_PROGRAM"
EOF

cat >"$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=地面站 UI
Comment=打开无人机巡查地面站界面
Exec=$LAUNCHER
Icon=applications-engineering
Terminal=false
StartupNotify=true
Categories=Utility;
EOF

chmod +x "$LAUNCHER" "$DESKTOP_FILE"
gio set "$DESKTOP_FILE" metadata::trusted true 2>/dev/null || true
echo "Desktop shortcut installed: $DESKTOP_FILE"
