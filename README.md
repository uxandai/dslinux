# libdualsense

DualSense (PS5) adaptive triggers, rumble, and LED control over Bluetooth and USB on Linux.

A C library (`libdualsense.so`), daemon (`dualsensed`), and CLI tool (`dsctl`) — no kernel driver modification needed.

## Features

- All adaptive trigger modes: feedback, weapon, vibration, bow, galloping, machine
- Multi-zone feedback and vibration with per-position strength
- Slope feedback (linearly interpolated resistance)
- Legacy rumble motors (left/right, 0-255)
- Lightbar RGB control
- Player indicator LEDs
- Mute button LED
- Works over **Bluetooth** and USB
- BT CRC32 and sequence tag handling (transparent to user)
- Daemon with Unix socket IPC (JSON protocol)
- **DSX-compatible UDP listener** (port 6969) — existing Windows game mods work through Proton/Wine without modification
- 60-second mod timeout with automatic reset
- Supports DualSense (0CE6) and DualSense Edge (0DF2)
- GTK4 GUI for visual control

## Quick Install

```bash
./build.sh && ./install.sh
```

## Build

```bash
./build.sh              # release build
./build.sh debug        # debug with sanitizers
./build.sh clean        # remove build dir
```

Or manually:

```bash
cmake -B build
cmake --build build
```

Dependencies: cmake, gcc (or clang), Linux kernel headers. No external libraries required.

## Install

```bash
# Install binaries and library
sudo cmake --install build

# Install udev rules (required for non-root access)
sudo cp udev/99-dualsense.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger

# (Optional) Install systemd user service
mkdir -p ~/.config/systemd/user
cp daemon/dualsensed.service ~/.config/systemd/user/
systemctl --user daemon-reload
```

After installing udev rules, **reconnect your controller** (unplug and replug, or toggle BT).

## Quick Start

```bash
# Check if controller is found
./build/dsctl info

# Set right trigger to weapon mode (gun resistance)
./build/dsctl trigger right weapon 2 7 8

# Set left trigger to vibration
./build/dsctl trigger left vibration 0 8 30

# Rumble both motors
./build/dsctl rumble 200 200

# Set lightbar to red
./build/dsctl lightbar ff0000

# Turn everything off
./build/dsctl trigger right off
./build/dsctl trigger left off
./build/dsctl rumble 0 0
```

## dsctl CLI Reference

```
dsctl [--device /dev/hidrawN] <command> [args...]
```

If `--device` is omitted, auto-detects the first DualSense controller.

### Trigger Commands

```bash
dsctl trigger <left|right> off
dsctl trigger <left|right> feedback <position:0-9> <strength:1-8>
dsctl trigger <left|right> weapon <start:2-7> <end:3-8> <strength:1-8>
dsctl trigger <left|right> vibration <position:0-9> <amplitude:1-8> <frequency:1-255>
dsctl trigger <left|right> bow <start:0-8> <end:0-8> <strength:1-8> <snap:1-8>
dsctl trigger <left|right> galloping <start:0-8> <end:1-9> <foot1:0-6> <foot2:1-7> <freq:1-255>
dsctl trigger <left|right> machine <start:0-8> <end:1-9> <ampA:0-7> <ampB:0-7> <freq:1-255> <period>
```

**Trigger mode descriptions:**

| Mode | Effect |
|------|--------|
| `off` | Disable trigger effect |
| `feedback` | Continuous resistance from position onwards |
| `weapon` | Gun trigger — resistance between start and end |
| `vibration` | Vibrating from position at given frequency |
| `bow` | Like weapon but with snap-back force |
| `galloping` | Rhythmic two-foot oscillation pattern |
| `machine` | Dual-amplitude vibration with period switching |

### Other Commands

```bash
dsctl rumble <left:0-255> <right:0-255>
dsctl lightbar <RRGGBB>                    # hex color, e.g. ff0000
dsctl player-leds <mask:0-31>              # 5 LEDs, bitmask
dsctl mute-led <off|on|pulse>
dsctl info                                 # show connection type
```

## Daemon (dualsensed)

For real-time control from games/scripts, use the daemon instead of spawning `dsctl` repeatedly:

```bash
# Start daemon
./build/dualsensed

# Or with systemd
systemctl --user start dualsensed
```

Send JSON commands via Unix socket:

```bash
# Test with socat
echo '{"cmd":"trigger","side":"R","mode":"weapon","start":2,"end":7,"strength":8}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock

echo '{"cmd":"rumble","left":128,"right":64}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock

echo '{"cmd":"lightbar","r":0,"g":255,"b":0}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock

echo '{"cmd":"info"}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock
```

### Daemon JSON Protocol

**Commands:**

```json
{"cmd":"trigger","side":"R","mode":"weapon","start":2,"end":7,"strength":8}
{"cmd":"trigger","side":"L","mode":"vibration","position":0,"amplitude":8,"frequency":30}
{"cmd":"trigger","side":"R","mode":"feedback","position":3,"strength":6}
{"cmd":"trigger","side":"L","mode":"off"}
{"cmd":"rumble","left":128,"right":64}
{"cmd":"lightbar","r":255,"g":0,"b":128}
{"cmd":"player-leds","mask":21}
{"cmd":"mute-led","mode":"pulse"}
{"cmd":"info"}
```

**Responses:**

```json
{"ok":true}
{"ok":false,"error":"invalid parameters"}
{"ok":true,"connection":"bluetooth"}
```

## Debugging

### Check if controller is visible

```bash
# List hidraw devices
ls -la /dev/hidraw*

# Find DualSense by VID:PID
for d in /sys/class/hidraw/hidraw*; do
  cat "$d/device/uevent" 2>/dev/null | grep -q "054C.*0CE6" && echo "$(basename $d): DualSense"
done

# Check permissions
stat /dev/hidraw*
```

### Test hidraw access directly

```bash
# See if you can open the device
python3 -c "open('/dev/hidraw3', 'rb')" 2>&1

# If permission denied, install udev rules:
sudo cp udev/99-dualsense.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
# Then reconnect the controller
```

### Monitor HID traffic

```bash
# Watch raw HID reports (requires root)
sudo cat /dev/hidraw3 | xxd | head -20

# With btmon (Bluetooth HID monitor)
sudo btmon

# With usbmon (USB traffic)
sudo modprobe usbmon
sudo cat /sys/kernel/debug/usb/usbmon/0u | grep -i "054c"
```

### Verify BT output reports

```bash
# Send a test report and check dmesg for errors
./build/dsctl trigger right weapon 2 7 8
dmesg | tail -5

# If CRC errors appear, connection type detection may be wrong:
./build/dsctl info
# Should show "Bluetooth" when connected wirelessly
```

### Common issues

| Issue | Fix |
|-------|-----|
| `Permission denied` opening hidraw | Install udev rules, reconnect controller |
| Trigger effect not felt | Check `dsctl info` shows correct connection type |
| BT report rejected | CRC32 mismatch — file a bug |
| Controller not found | Check it's paired and connected: `bluetoothctl devices Connected` |
| Kernel driver conflict | The kernel driver and dsctl can coexist — both use hidraw |

## Library API (C)

```c
#include <dualsense.h>

ds_device_t *dev = ds_open(NULL);  // auto-detect, or pass "/dev/hidrawN"

// Adaptive triggers
ds_trigger_weapon(dev, DS_TRIGGER_RIGHT, 2, 7, 8);
ds_trigger_vibration(dev, DS_TRIGGER_LEFT, 0, 8, 30);
ds_send(dev);  // flush to controller

// Rumble
ds_rumble(dev, 200, 200);
ds_send(dev);

// LEDs
ds_lightbar(dev, 255, 0, 0);
ds_player_leds(dev, 0x15);  // LEDs 1, 3, 5
ds_send(dev);

// Cleanup
ds_trigger_off(dev, DS_TRIGGER_LEFT);
ds_trigger_off(dev, DS_TRIGGER_RIGHT);
ds_rumble(dev, 0, 0);
ds_send(dev);
ds_close(dev);
```

Link with `-ldualsense`.

## DSX Game Mod Compatibility

`dualsensed` includes a **DSX-compatible UDP listener** on port 6969.  Existing Windows game mods that send DSX packets (e.g., from Nexus Mods) work through Proton/Wine without modification.

### How it works

1. Start the daemon: `dualsensed`
2. Launch a game through Steam/Proton that has a DSX mod installed (.asi file)
3. The mod sends UDP JSON packets to `127.0.0.1:6969`
4. `dualsensed` receives them and applies trigger/LED effects to your controller

### DSX Protocol

The daemon understands the full DSX instruction set:

```bash
# Test DSX protocol manually with netcat:
echo '{"Instructions":[{"Type":1,"Parameters":[0,2,22,2,7,8]}]}' | nc -u 127.0.0.1 6969

# Type=1: TriggerUpdate
# Parameters: [controller=0, side=2(Right), mode=22(WEAPON), start=2, end=7, strength=8]
```

**Supported DSX instruction types:**

| Type | Name | Description |
|------|------|-------------|
| 0 | GetDSXStatus | Query — returns controller info |
| 1 | TriggerUpdate | Set adaptive trigger effect |
| 2 | RGBUpdate | Set lightbar color |
| 3 | PlayerLED | Set player LEDs (legacy) |
| 5 | MicLED | Set microphone LED |
| 6 | PlayerLEDNewRevision | Set player LEDs (preset) |
| 7 | ResetToUserSettings | Clear all mod effects |

**Supported DSX trigger modes (Type=1, Parameters[2]):**

| ID | Mode | ID | Mode |
|----|------|----|------|
| 0/20 | Off | 14 | Bow |
| 21 | Feedback | 15 | Galloping |
| 22 | Weapon | 18 | Machine |
| 23 | Vibration | 24 | Slope Feedback |
| 8 | Vibrate Trigger | 25 | Multi-Position Feedback |
| 12 | Custom Raw | 26 | Multi-Position Vibration |

### Daemon options

```bash
dualsensed                    # default: Unix socket + UDP 6969
dualsensed --dsx-port 7070    # custom DSX port
dualsensed --no-dsx           # disable UDP, Unix socket only
```

If no DSX mod packets are received for 60 seconds, all trigger effects are automatically reset.

## Steam / Gaming Notes

- **Native DualSense games** (Ratchet & Clank, Spider-Man, etc.): Disable Steam Input per-game for native adaptive trigger support. These games send trigger effects directly to the controller — no extra software needed.
- **Games with DSX mods**: Start `dualsensed`, install the .asi mod, play through Proton. The mod talks to our daemon instead of DSX.
- **Non-native games without mods**: Use `dsctl` or the GUI to set trigger effects manually.
- **Steam Input ON**: Triggers and haptics are lost — Steam emulates XInput which has no trigger API.

## License

MIT
