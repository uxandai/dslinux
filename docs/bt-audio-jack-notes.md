# BT Audio Jack / Speaker — Research Notes

## Key finding: same report 0x32 as haptics

Audio for speaker/headphone jack uses the **same HID report 0x32** as haptics,
but with **different sub-packets and encoding**:

| Sub-packet | Content | Encoding | Rate |
|------------|---------|----------|------|
| 0x11 | Config/flags | N/A | per-report |
| 0x12 | Haptics (VCM actuators) | Raw PCM 8-bit | 3000 Hz stereo |
| 0x13 (unconfirmed) | Audio (speaker/jack) | Opus CBR | 48000 Hz stereo |

## DSX BT audio architecture (from RE)

1. **Capture** game audio (WASAPI loopback)
2. **Resample** to 48kHz stereo
3. **Opus encode**: CBR, 10ms frames, `OPUS_APPLICATION_AUDIO`
4. **Pack** into sub-packet within report 0x32
5. **Interleave** with haptics sub-packets and controller data

## Report variants

DSX has 9+ report variants combining audio/haptics/data:

| Name suffix | Content |
|-------------|---------|
| `_D` | Device data only (triggers, LEDs) |
| `_H` | Haptics only |
| `_A` | Audio only (Opus frames) |
| `_HD` | Haptics + Device |
| `_AH` | Audio + Haptics |
| `_AD` | Audio + Device |
| `_AHD` | Audio + Haptics + Device |

Reports can carry 1 or 2 audio/haptics frames per packet.

## Audio configuration

- Sample rate: 48000 Hz (from OpusDecoder/resampler targets)
- Channels: 2 (stereo, downmixed if needed)
- Opus frame: 10ms = 480 samples
- PCM frame size: 480 * 2ch * 2 bytes = 1920 bytes (before Opus compression)
- Opus output: ~66 bytes per frame (at target bitrate, CBR)
- Max buffer: 200 bytes (for high bitrate Opus frames)

## Audio output modes

```
BluetoothHapticsAudioOutputMode:
  0 = Internal speaker only
  1 = Headset (jack) only — DEFAULT
  2 = Both speaker and headset
```

Separate volume controls:
- `BluetoothHapticsSpeakerVolume`
- `BluetoothHapticsHeadsetVolume`

## What's hidden in obfuscation

The VM bytecode blob (18KB) in DSX contains:
- Actual sub-packet ID for Opus audio (likely 0x13)
- Report length constants per variant
- Report offsets for each sub-packet
- Audio config values (bitrate, exact frame sizes)
- Frame scheduling logic (which variant to use when)

## How to crack it

**Option A: BT sniffing (easiest)**
1. Windows PC/VM with BT adapter + DualSense paired
2. Run DSX with BT haptics + audio enabled
3. Capture with `btmon` (Linux) or Wireshark + USBPcap (Windows)
4. Find report 0x32 packets
5. Compare: haptics-only vs audio+haptics — the diff is the audio sub-packet

**Option B: VM bytecode analysis**
The bytecode is at: `dsx-decompiled/DSX/ppMZHigYRR4nZ56aGI.I2U0fAhIWOSpGr06qA`
It's a custom .NET IL-like interpreter. Could be reverse-engineered by:
1. Studying the dispatcher (`aWIbXv9AnoBV5BGZTU.ai8PRH4QkM`)
2. Mapping dispatch IDs to opcodes
3. Disassembling the bytecode blob

**Option C: Dynamic analysis**
1. Run DSX in Windows VM with BT passthrough
2. Hook HidD_SetOutputReport / WriteFile on hidraw
3. Log all output reports
4. Correlate with audio being played

## For implementation

Once the audio sub-packet format is known:
1. Add libopus dependency to CMakeLists.txt
2. Add `src/audio_stream.c` — Opus encoder + sub-packet builder
3. Extend report 0x32 builder in haptics.c to support combined A+H+D packets
4. Add PipeWire capture for game audio → Opus → report 0x32
5. Expose in daemon: `{"cmd":"audio","source":"loopback"}`
