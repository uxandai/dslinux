#!/usr/bin/env bash
#
# One-liner install for libdualsense:
#   curl -sSL <url>/install.sh | bash
#   or: ./install.sh
#
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$REPO_DIR/build"

info()  { printf "\033[1;34m::\033[0m %s\n" "$*"; }
ok()    { printf "\033[1;32m::\033[0m %s\n" "$*"; }
err()   { printf "\033[1;31m::\033[0m %s\n" "$*" >&2; }

# Check deps
for cmd in cmake cc; do
    if ! command -v "$cmd" &>/dev/null; then
        err "Missing dependency: $cmd"
        err "Install with: sudo pacman -S cmake gcc   (or equivalent)"
        exit 1
    fi
done

# Build
info "Building libdualsense..."
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "$REPO_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"
ok "Build complete"

# Install library + binaries
info "Installing to /usr/local (requires sudo)..."
sudo cmake --install "$BUILD_DIR"
sudo ldconfig 2>/dev/null || true
ok "Installed: dsctl, dualsensed, libdualsense.so"

# Udev rules
info "Installing udev rules..."
sudo cp "$REPO_DIR/udev/99-dualsense.rules" /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
ok "Udev rules installed — reconnect your controller"

# Systemd user service
info "Installing systemd user service..."
mkdir -p "$HOME/.config/systemd/user"
cp "$REPO_DIR/daemon/dualsensed.service" "$HOME/.config/systemd/user/"
systemctl --user daemon-reload
ok "Service installed: systemctl --user start dualsensed"

# Summary
echo ""
ok "Installation complete!"
echo ""
echo "  Quick test:"
echo "    dsctl info"
echo "    dsctl trigger right weapon 2 7 8"
echo "    dsctl rumble 200 200"
echo ""
echo "  Start daemon:"
echo "    dualsensed                          # foreground"
echo "    systemctl --user start dualsensed   # background"
echo ""
echo "  GUI:"
echo "    python3 $REPO_DIR/gui/dualsense-gui.py"
echo ""
echo "  If 'Permission denied', reconnect your controller after udev reload."
