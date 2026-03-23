# DualSense Linux — Adaptive Triggers & Vibration over Bluetooth

## Goal

Userspace library + daemon enabling DualSense adaptive triggers and rumble vibration over Bluetooth on Linux — feature parity with wired USB, without kernel driver modifications.

## Non-goals (this phase)

- Audio-based haptics (VCM actuators driven by audio stream) — see `docs/bt-audio-haptics-notes.md`
- Controller speaker / headphone jack audio
- Kernel driver / DKMS module
- Windows support

---

## Technical Foundation

### Protocol Summary

DualSense communicates via HID over BT Classic (BR/EDR), not BLE. Two output report formats:

| | USB | Bluetooth |
|--|-----|-----------|
| Report ID | 0x02 | 0x31 |
| Total size | 64 bytes | 78 bytes |
| CRC32 | No | Yes (last 4 bytes) |
| Seq tag | No | Yes (byte [1], upper nibble) |
| Offset shift | baseline | +1 vs USB |

### BT Output Report Layout (78 bytes)

```
[0]      Report ID = 0x31
[1]      seq_tag (bits 7-4, wraps 0-15) | flags (bits 3-0) = 0x02
[2]      Feature flags byte 0 (0x03=native rumble, 0x0F=emulation)
[3]      Feature flags byte 1 (0x40 | 0x15 for LEDs)
[4]      Right rumble motor (0-255)
[5]      Left rumble motor (0-255)
[6-9]    Audio/misc (unused for our scope)
[10]     Mute button LED (0=off, 1=on, 2=pulse)
[11]     Reserved
[12]     Right trigger effect mode
[13-22]  Right trigger params (10 bytes)
[23]     Left trigger effect mode
[24-33]  Left trigger params (10 bytes)
[34-37]  Reserved
[38]     Motor attenuation (0=full, 7=weakest)
[39]     Reserved
[40]     Enhanced rumble flags (0x03=normal, 0x07=enhanced)
[41-42]  Reserved
[43]     LED brightness mode (1=fade-in, 2=full)
[44]     Player/mute LED brightness
[45]     Player LED bitmask (5 LEDs, bits 0-4)
[46]     Lightbar Red (0-255)
[47]     Lightbar Green (0-255)
[48]     Lightbar Blue (0-255)
[49-73]  Reserved / DualSense Edge extras
[74-77]  CRC32 (little-endian)
```

### USB Output Report Layout (64 bytes)

Same structure as BT bytes [2..48] mapped to [1..47]. No seq_tag, no CRC.

```
[0]      Report ID = 0x02
[1]      Feature flags byte 0
[2]      Feature flags byte 1
[3]      Right rumble motor
[4]      Left rumble motor
...      (all offsets = BT offset - 1)
[11]     Right trigger effect mode
[12-21]  Right trigger params
[22]     Left trigger effect mode
[23-32]  Left trigger params
...
```

### CRC32 Algorithm

```
Polynomial: 0xEDB88320 (standard CRC32, reflected)
Seed:       0xA2 (BT output report preamble)
Pre-computed seed: 0xEB1C1A49 = crc32(0xFFFFFFFF, [0xA2])

Calculation:
  crc = 0xEB1C1A49
  for each byte in report[0..73]:
      crc = table[(crc & 0xFF) ^ byte] ^ (crc >> 8)
  write crc as LE u32 at report[74..77]
```

### Adaptive Trigger Effect Types

| Mode ID | Name | Params (10 bytes) |
|---------|------|-------------------|
| 0x05 | Off | all zeros |
| 0x01 | Simple Feedback | [position, strength, 0...] |
| 0x02 | Simple Weapon | [start, end, strength, 0...] |
| 0x06 | Simple Vibration | [frequency, amplitude, position, 0...] |
| 0x21 | Feedback | [zone_mask_lo, zone_mask_hi, force_3bit_packed(4 bytes), 0...] |
| 0x22 | Bow | [zone_mask_lo, zone_mask_hi, combined_forces(2 bytes), 0...] |
| 0x23 | Galloping | [zone_mask_lo, zone_mask_hi, combined_forces, frequency, 0...] |
| 0x25 | Weapon | [zone_mask_lo, zone_mask_hi, strength-1, 0...] |
| 0x26 | Vibration | [zone_mask_lo, zone_mask_hi, force_packed(4 bytes), 0, 0, frequency, 0] |
| 0x27 | Machine | [zone_mask_lo, zone_mask_hi, amplitudes, frequency, period, 0...] |

**Zone mask**: 10 zones (positions 0-9), bit per zone in 16-bit LE value.
**Force packing**: 3 bits per zone, packed LE across 4 bytes. Value = `(strength - 1) & 7`.

---

## Architecture

```
┌─────────────┐     ┌──────────────┐     ┌──────────────┐
│  Game / App  │────▶│  dualsensed  │────▶│  /dev/hidrawN│──▶ DualSense
│  (client)    │ IPC │  (daemon)    │ HID │  (kernel)    │    (controller)
└─────────────┘     └──────────────┘     └──────────────┘
       │                    │
  Unix socket          libdualsense.so
  JSON protocol        C library
```

### Components

1. **`libdualsense`** — C library (.so)
   - Device discovery (hidraw enumeration by VID/PID)
   - USB/BT auto-detection (input report length: 64=USB, 78=BT)
   - Output report building (BT: 0x31+CRC, USB: 0x02)
   - Trigger effect encoding
   - Rumble control
   - Lightbar / LED control
   - Thread-safe, stateful (holds fd, seq counter)

2. **`dualsensed`** — Daemon
   - Holds hidraw fd open persistently (no open/close per command)
   - IPC via Unix socket (JSON protocol)
   - Coalesces rapid updates (max ~250 reports/sec over BT)
   - Auto-reconnect on device disconnect
   - Systemd unit file

3. **`dsctl`** — CLI tool
   - Thin client for dualsensed
   - Drop-in alternative to dualsensectl for scripting

4. **Python bindings** — ctypes wrapper around libdualsense

---

## Implementation Plan

### Phase 1: Python PoC (validate protocol)

**Goal:** Send one BT output report, feel trigger resistance.

**Files:**
```
poc/
├── dualsense_hid.py     # hidraw open, VID/PID scan, read/write
├── crc32.py             # CRC32 with seed 0xA2
├── report.py            # Build 78-byte BT / 64-byte USB output report
├── triggers.py          # Encode trigger effects (all modes)
├── demo_triggers.py     # Interactive demo: cycle through effects
└── demo_rumble.py       # Rumble test
```

**Steps:**
1. Enumerate `/dev/hidraw*`, match VID=054C PID=0CE6 via sysfs
2. Detect BT vs USB (read input report, check length)
3. Build output report with right trigger = Weapon(start=2, end=7, strength=8)
4. Calculate CRC32 for BT, append
5. Write to hidraw fd
6. Verify: trigger should resist

**Validation criteria:** Physical trigger resistance felt on controller connected over BT.

### Phase 2: C library — libdualsense

**Goal:** Production-quality shared library.

**API:**
```c
// Lifecycle
ds_device_t* ds_open(const char* path);  // NULL = auto-detect
void ds_close(ds_device_t* dev);
ds_conn_type_t ds_connection_type(ds_device_t* dev);  // USB / BT

// Triggers
void ds_trigger_off(ds_device_t* dev, ds_trigger_t side);
void ds_trigger_feedback(ds_device_t* dev, ds_trigger_t side,
                         uint8_t position, uint8_t strength);
void ds_trigger_weapon(ds_device_t* dev, ds_trigger_t side,
                       uint8_t start, uint8_t end, uint8_t strength);
void ds_trigger_vibration(ds_device_t* dev, ds_trigger_t side,
                          uint8_t position, uint8_t amplitude, uint8_t freq);
void ds_trigger_raw(ds_device_t* dev, ds_trigger_t side,
                    uint8_t mode, const uint8_t params[10]);

// Rumble
void ds_rumble(ds_device_t* dev, uint8_t left, uint8_t right);

// LEDs
void ds_lightbar(ds_device_t* dev, uint8_t r, uint8_t g, uint8_t b);
void ds_player_leds(ds_device_t* dev, uint8_t mask);

// Send (flushes pending state to controller)
int ds_send(ds_device_t* dev);
```

**Build:** CMake, depends only on libudev (for enumeration). No hidapi dependency — direct hidraw.

### Phase 3: Daemon + CLI

**dualsensed:**
- Listen on `$XDG_RUNTIME_DIR/dualsensed.sock`
- JSON-line protocol: `{"cmd":"trigger","side":"R","mode":"weapon","start":2,"end":7,"strength":8}\n`
- Response: `{"ok":true}\n` or `{"ok":false,"error":"..."}\n`
- Debounce: coalece writes, send at max 250 Hz
- Device hotplug via udev monitor

**dsctl CLI:**
```bash
dsctl trigger right weapon 2 7 8
dsctl trigger left vibration 0 8 40
dsctl rumble 128 64
dsctl lightbar ff0000
dsctl trigger off
```

### Phase 4: Integration

- **udev rules** for hidraw permissions
- **systemd user unit** for dualsensed
- **Python bindings** (ctypes)
- **Per-game configs** (JSON profiles loaded by daemon)
- **Steam integration notes** (when to disable Steam Input)

---

## Device Identification

| Device | VID | PID |
|--------|-----|-----|
| DualSense | 054C | 0CE6 |
| DualSense Edge | 054C | 0DF2 |

## Dependencies

- **Build:** cmake, gcc/clang, libudev-dev
- **Runtime:** libudev, Linux kernel with CONFIG_HID (hidraw support)
- **PoC:** Python 3.8+, no pip packages (raw syscalls)

## Reference Implementations

- [dualsensectl](https://github.com/nowrep/dualsensectl) — C CLI, full BT support
- [pydualsense](https://github.com/flok/pydualsense) — Python, hidapi-based
- [trigger-control](https://github.com/Etaash-mathamsetty/trigger-control) — C++ GUI
- [Nielk1 trigger effects gist](https://gist.github.com/Nielk1/6d54cc2c00d2201ccb8c2720ad7538db) — all trigger modes
- [Game Controller Collective Wiki](https://controllers.fandom.com/wiki/Sony_DualSense/Data_Structures) — HID structures
- [Linux hid-playstation.c](https://github.com/torvalds/linux/blob/master/drivers/hid/hid-playstation.c) — kernel driver reference
