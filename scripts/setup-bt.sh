#!/usr/bin/env bash
#
# Setup Bluetooth for DualSense on Linux.
#
# Handles all known issues:
#   1. Loads hidp + hid_playstation kernel modules
#   2. Disables ClassicBondedOnly in BlueZ (required for DualSense HID)
#   3. Pairs controller with agent
#   4. Captures and saves LinkKey (BlueZ bug workaround)
#
set -euo pipefail

info()  { printf "\033[1;34m::\033[0m %s\n" "$*"; }
ok()    { printf "\033[1;32m::\033[0m %s\n" "$*"; }
err()   { printf "\033[1;31m::\033[0m %s\n" "$*" >&2; }

BT_ADAPTER=$(bluetoothctl show 2>/dev/null | grep "Controller" | awk '{print $2}')
if [ -z "$BT_ADAPTER" ]; then
    err "No Bluetooth adapter found"
    exit 1
fi

# ── Step 1: Kernel modules ──────────────────────────────────────
info "Loading kernel modules..."
sudo modprobe hidp 2>/dev/null && ok "hidp loaded" || err "hidp failed (may be built-in)"
sudo modprobe hid_playstation 2>/dev/null && ok "hid_playstation loaded" || err "hid_playstation failed"

if [ ! -f /etc/modules-load.d/dualsense.conf ]; then
    echo -e "hidp\nhid_playstation" | sudo tee /etc/modules-load.d/dualsense.conf > /dev/null
    ok "Modules will load on boot"
fi

# ── Step 2: BlueZ config ───────────────────────────────────────
if grep -q "^#ClassicBondedOnly=true" /etc/bluetooth/input.conf 2>/dev/null; then
    info "Fixing BlueZ ClassicBondedOnly..."
    sudo sed -i 's/^#ClassicBondedOnly=true/ClassicBondedOnly=false/' /etc/bluetooth/input.conf
    sudo systemctl restart bluetooth
    sleep 1
    ok "BlueZ config fixed"
elif grep -q "^ClassicBondedOnly=false" /etc/bluetooth/input.conf 2>/dev/null; then
    ok "BlueZ config already correct"
else
    info "Setting ClassicBondedOnly=false..."
    echo "ClassicBondedOnly=false" | sudo tee -a /etc/bluetooth/input.conf > /dev/null
    sudo systemctl restart bluetooth
    sleep 1
    ok "BlueZ config fixed"
fi

# ── Step 3: Pair controller ─────────────────────────────────────
echo ""
info "Put your DualSense in pairing mode:"
echo "  Hold CREATE + PS button for 5 seconds"
echo "  Lightbar should flash rapidly"
echo ""
read -rp "Press Enter when lightbar is flashing..."

# Start btmon to capture link key
BTMON_LOG=$(mktemp)
sudo btmon > "$BTMON_LOG" 2>&1 &
BTMON_PID=$!
sleep 1

info "Scanning for DualSense..."
bluetoothctl scan on &>/dev/null &
SCAN_PID=$!
sleep 8

MAC=$(bluetoothctl devices 2>/dev/null | grep -i "dualsense\|wireless.*controller" | awk '{print $2}' | head -1)
kill $SCAN_PID 2>/dev/null || true

if [ -z "$MAC" ]; then
    sudo kill $BTMON_PID 2>/dev/null
    err "DualSense not found. Make sure it's in pairing mode and try again."
    exit 1
fi
ok "Found: $MAC"

info "Pairing (with agent for auto-accept)..."
# Run agent in background
(echo "agent NoInputNoOutput"; echo "default-agent"; sleep 30) | bluetoothctl &>/dev/null &
AGENT_PID=$!
sleep 1

bluetoothctl remove "$MAC" &>/dev/null || true
sleep 1
bluetoothctl pair "$MAC" 2>&1 | grep -v "^\[" || true
bluetoothctl trust "$MAC" 2>&1 | grep -v "^\[" || true
sleep 2

kill $AGENT_PID 2>/dev/null || true

# ── Step 4: Save LinkKey (BlueZ workaround) ────────────────────
info "Capturing link key..."
sudo kill $BTMON_PID 2>/dev/null || true
sleep 1

LINK_KEY=$(grep -A4 "Link Key Notification" "$BTMON_LOG" | grep -oP '([0-9a-f]{2} ){16}' | head -1 | tr -d ' ')
rm -f "$BTMON_LOG"

if [ -z "$LINK_KEY" ]; then
    err "Could not capture link key. Try pairing again."
    exit 1
fi

LINK_KEY_UPPER=$(echo "$LINK_KEY" | tr 'a-f' 'A-F')
ok "Link key: $LINK_KEY_UPPER"

MAC_PATH=$(echo "$MAC" | tr ':' '_')
INFO_FILE="/var/lib/bluetooth/$BT_ADAPTER/$MAC/info"

if sudo test -f "$INFO_FILE"; then
    if ! sudo grep -q "\[LinkKey\]" "$INFO_FILE"; then
        info "Writing link key to BlueZ storage..."
        sudo bash -c "cat >> $INFO_FILE" << EOF

[LinkKey]
Key=$LINK_KEY_UPPER
Type=4
PINLength=0
EOF
        ok "Link key saved"
    else
        ok "Link key already exists"
    fi
else
    err "BlueZ info file not found: $INFO_FILE"
    err "Controller paired but may not persist across reboots"
fi

# ── Step 5: Restart and verify ──────────────────────────────────
info "Restarting bluetooth..."
sudo systemctl restart bluetooth
sleep 3

info "Connecting..."
bluetoothctl connect "$MAC" 2>&1 | grep -v "^\[" || true
sleep 2

# Check result
if ls /dev/hidraw* 2>/dev/null | while read d; do
    grep -q "054C" "/sys/class/hidraw/$(basename "$d")/device/uevent" 2>/dev/null && echo "$d"
done | grep -q hidraw; then
    HIDRAW=$(for d in /dev/hidraw*; do
        grep -q "054C" "/sys/class/hidraw/$(basename "$d")/device/uevent" 2>/dev/null && echo "$d"
    done | head -1)
    ok "DualSense connected at $HIDRAW"
    echo ""
    echo "  Test:   ./build/dsctl info"
    echo "          ./build/dsctl trigger right weapon 2 7 8"
    echo ""
    echo "  The controller will now reconnect automatically after power-off."
    echo "  Just press the PS button."
else
    err "DualSense hidraw not found. Try: bluetoothctl connect $MAC"
fi
