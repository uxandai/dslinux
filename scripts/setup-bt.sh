#!/usr/bin/env bash
#
# Setup Bluetooth for DualSense on Linux.
#
# Loads required kernel modules and pairs the controller.
# Run once after boot (or add modules to /etc/modules-load.d/).
#
set -euo pipefail

info()  { printf "\033[1;34m::\033[0m %s\n" "$*"; }
ok()    { printf "\033[1;32m::\033[0m %s\n" "$*"; }
err()   { printf "\033[1;31m::\033[0m %s\n" "$*" >&2; }

# Step 1: Load kernel modules
info "Loading kernel modules..."
sudo modprobe hidp 2>/dev/null && ok "hidp (BT HID Profile)" || err "hidp failed"
sudo modprobe hid_playstation 2>/dev/null && ok "hid_playstation (DualSense driver)" || err "hid_playstation failed"

# Step 2: Make modules load on boot
info "Persisting modules for next boot..."
echo -e "hidp\nhid_playstation" | sudo tee /etc/modules-load.d/dualsense.conf > /dev/null
ok "Created /etc/modules-load.d/dualsense.conf"

# Step 3: Pair controller
echo ""
info "Put your DualSense in pairing mode:"
echo "  Hold CREATE + PS button for 5 seconds"
echo "  Lightbar should flash rapidly"
echo ""
read -rp "Press Enter when lightbar is flashing..."

info "Scanning for DualSense..."
bluetoothctl scan on &
SCAN_PID=$!
sleep 8

MAC=$(bluetoothctl devices 2>/dev/null | grep -i "dualsense\|wireless.*controller" | awk '{print $2}' | head -1)
kill $SCAN_PID 2>/dev/null || true

if [ -z "$MAC" ]; then
    err "DualSense not found. Make sure it's in pairing mode and try again."
    exit 1
fi

ok "Found: $MAC"

info "Pairing..."
bluetoothctl pair "$MAC"
bluetoothctl trust "$MAC"

# Step 4: Connect (with agent workaround)
info "Connecting..."
# Start agent in background for auto-accept
(echo "agent NoInputNoOutput"; echo "default-agent"; sleep 30) | bluetoothctl &
AGENT_PID=$!
sleep 2
bluetoothctl connect "$MAC" 2>&1 || true
sleep 3
kill $AGENT_PID 2>/dev/null || true

# Step 5: Verify
if ls /dev/hidraw* 2>/dev/null | while read d; do
    grep -q "054C" "/sys/class/hidraw/$(basename "$d")/device/uevent" 2>/dev/null && echo "$d"
done | grep -q hidraw; then
    HIDRAW=$(ls /dev/hidraw* 2>/dev/null | while read d; do
        grep -q "054C" "/sys/class/hidraw/$(basename "$d")/device/uevent" 2>/dev/null && echo "$d"
    done | head -1)
    ok "DualSense connected at $HIDRAW"
    echo ""
    echo "Test with: ./build/dsctl info"
else
    err "DualSense hidraw not found. Check 'journalctl -u bluetooth' for errors."
    echo ""
    echo "Common fix: run bluetoothctl interactively:"
    echo "  bluetoothctl"
    echo "  agent NoInputNoOutput"
    echo "  default-agent"
    echo "  connect $MAC"
fi
