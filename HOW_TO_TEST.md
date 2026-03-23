# How to Test — Step by Step

## Prerequisites

- DualSense (PS5) controller
- Linux with BlueZ (for BT) or USB-C cable
- Packages: `cmake`, `gcc`, PipeWire (for haptics)

## 1. Build

```bash
./build.sh
```

Expected: zero errors, binaries in `build/`.

## 2. Install udev rules

```bash
sudo cp udev/99-dualsense.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Then **reconnect** your controller (toggle BT off/on, or unplug USB).

## 3. Verify controller is detected

```bash
# Find your hidraw device
for d in /sys/class/hidraw/hidraw*; do
  uevent="$d/device/uevent"
  if grep -q "054C" "$uevent" 2>/dev/null; then
    name=$(basename "$d")
    bus=$(grep HID_ID "$uevent" | cut -d= -f2 | cut -d: -f1)
    conn="USB"
    [ "$bus" = "0005" ] && conn="Bluetooth"
    echo "$name: DualSense ($conn) -> /dev/$name"
  fi
done
```

Expected: something like `hidraw3: DualSense (Bluetooth) -> /dev/hidraw3`

```bash
# Quick check with dsctl
./build/dsctl info
```

Expected: `Connection: Bluetooth` or `Connection: USB`

## 4. Test adaptive triggers

### 4.1 Weapon mode (gun trigger feel)

```bash
./build/dsctl trigger right weapon 2 7 8
```

**Expected:** Right trigger resists between ~20% and ~70% pull, strength max.
Pull the trigger — you should feel a click/wall.

### 4.2 Feedback mode (continuous resistance)

```bash
./build/dsctl trigger left feedback 0 8
```

**Expected:** Left trigger has strong resistance from the very top.

### 4.3 Vibration mode

```bash
./build/dsctl trigger right vibration 0 8 30
```

**Expected:** Right trigger vibrates at 30 Hz. You feel buzzing as you pull it.

### 4.4 Bow mode

```bash
./build/dsctl trigger right bow 1 5 6 4
```

**Expected:** Resistance between positions 1-5, then a snap-back force.

### 4.5 Galloping mode

```bash
./build/dsctl trigger right galloping 0 8 2 5 20
```

**Expected:** Rhythmic pulsing pattern on the trigger.

### 4.6 Machine mode

```bash
./build/dsctl trigger left machine 1 8 5 3 25 5
```

**Expected:** Dual-amplitude vibration that alternates.

### 4.7 Turn triggers off

```bash
./build/dsctl trigger right off
./build/dsctl trigger left off
```

**Expected:** Both triggers return to normal (no resistance).

## 5. Test rumble motors

```bash
# Both motors full
./build/dsctl rumble 255 255
sleep 1
# Off
./build/dsctl rumble 0 0
```

**Expected:** Controller vibrates for 1 second.

```bash
# Left motor only (heavy)
./build/dsctl rumble 200 0
sleep 1
./build/dsctl rumble 0 0

# Right motor only (light)
./build/dsctl rumble 0 200
sleep 1
./build/dsctl rumble 0 0
```

**Expected:** Feel different vibration patterns (left=heavy, right=light buzz).

## 6. Test lightbar and LEDs

```bash
# Red lightbar
./build/dsctl lightbar ff0000

# Green
./build/dsctl lightbar 00ff00

# Blue
./build/dsctl lightbar 0000ff

# Player LEDs: all on (0x1F = 11111)
./build/dsctl player-leds 31

# Player LEDs: pattern (LEDs 1,3,5 = 0x15 = 10101)
./build/dsctl player-leds 21

# Mute LED pulse
./build/dsctl mute-led pulse

# Mute LED off
./build/dsctl mute-led off
```

**Expected:** Lightbar changes color, player LEDs light up, mute LED pulses.

## 7. Test daemon (Unix socket)

### 7.1 Start daemon

```bash
./build/dualsensed
```

Expected output:
```
DualSense connected via Bluetooth
Native socket: /run/user/1000/dualsensed.sock
DSX-compatible UDP: 127.0.0.1:6969
Ready.
```

### 7.2 Send commands via socat (in another terminal)

```bash
# Trigger
echo '{"cmd":"trigger","side":"R","mode":"weapon","start":2,"end":7,"strength":8}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock

# Rumble
echo '{"cmd":"rumble","left":200,"right":200}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock

# Info
echo '{"cmd":"info"}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock

# Reset
echo '{"cmd":"trigger","side":"R","mode":"off"}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock
echo '{"cmd":"rumble","left":0,"right":0}' | \
  socat - UNIX-CONNECT:$XDG_RUNTIME_DIR/dualsensed.sock
```

**Expected:** Same effects as dsctl, responses like `{"ok":true}`.

## 8. Test DSX compatibility (UDP)

With daemon running:

```bash
# DSX-format: right trigger weapon mode
echo '{"Instructions":[{"Type":1,"Parameters":[0,2,22,2,7,8]}]}' | \
  nc -u -w1 127.0.0.1 6969

# DSX-format: lightbar green
echo '{"Instructions":[{"Type":2,"Parameters":[0,0,255,0]}]}' | \
  nc -u -w1 127.0.0.1 6969

# DSX-format: multiple instructions
echo '{"Instructions":[{"Type":1,"Parameters":[0,1,23,0,8,30]},{"Type":1,"Parameters":[0,2,22,2,7,8]},{"Type":2,"Parameters":[0,255,0,0]}]}' | \
  nc -u -w1 127.0.0.1 6969

# DSX-format: reset
echo '{"Instructions":[{"Type":7,"Parameters":[0]}]}' | \
  nc -u -w1 127.0.0.1 6969
```

**Expected:** Controller responds to DSX packets. Response JSON returned via UDP.

### 8.1 Test DSX mod timeout

Send a trigger command, then wait 60 seconds without sending anything.
The daemon should print `DSX mod timeout — resetting triggers` and effects clear.

## 9. Test BT audio haptics (report 0x32)

**Requires:** BT connection (not USB).

### 9.1 Sine wave test

```bash
./build/ds-haptics-test --sine 150
```

**Expected:** Controller vibrates with a smooth 150 Hz buzz. Very different feel from legacy rumble — more refined, like a phone vibration motor.

Try different frequencies:
```bash
./build/ds-haptics-test --sine 50   # low rumble
./build/ds-haptics-test --sine 300  # high buzz
./build/ds-haptics-test --sine 1000 # very high pitch
```

### 9.2 PipeWire audio capture → haptics

```bash
# Create a PipeWire pipe sink for haptics
pw-cli load-module libpipewire-module-pipe-tunnel \
  direction=input \
  pipe.filename=/tmp/haptics-pipe \
  audio.format=u8 \
  audio.rate=3000 \
  audio.channels=2 \
  stream.props='{ media.class=Audio/Sink node.name=DualSense-Haptics }'

# In another terminal, pipe the audio to the controller
cat /tmp/haptics-pipe | ./build/ds-haptics-test --stdin
```

Then in system audio settings (pavucontrol or similar), redirect a game's audio output to "DualSense-Haptics" sink. You should feel the game audio as haptic vibration.

### 9.3 Direct file playback

```bash
# Convert any audio file to the right format and play
ffmpeg -i music.mp3 -f u8 -ar 3000 -ac 2 - | ./build/ds-haptics-test --stdin
```

**Expected:** You feel the music as vibration. Bass hits are most noticeable.

## 10. Test GUI

```bash
python3 gui/dualsense-gui.py
```

**Expected:** GTK4 window shows connected controller, battery status, trigger sliders, rumble, lightbar color picker, player LEDs.

**Note:** GUI requires daemon to be running for trigger/rumble/LED controls.

## 11. USB vs Bluetooth comparison

Run all tests above with both connections and verify:
- USB: all features work (report 0x02, no CRC)
- BT: all features work (report 0x31 with CRC, report 0x32 for haptics)
- `dsctl info` correctly reports connection type
- Adaptive triggers feel identical on both
- Audio haptics (report 0x32) only work on BT

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `Permission denied` | Missing udev rules | Step 2, then reconnect |
| `No such device` | Controller not connected | Pair via `bluetoothctl` or plug USB |
| Trigger set but no effect | Wrong connection type detected | Check `dsctl info` |
| BT CRC error in dmesg | Bug in CRC32 | File issue with `dmesg` output |
| Haptics silent | Not BT connected | Report 0x32 is BT-only |
| Haptics choppy | Audio source wrong rate | Must be 3kHz u8 stereo |
| Daemon won't start | Socket file exists | `rm $XDG_RUNTIME_DIR/dualsensed.sock` |
| DSX mod not working | UDP blocked / wrong port | Check `ss -ulnp | grep 6969` |
