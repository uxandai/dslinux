# BT Audio Haptics — RE Notes (Phase 2, do later)

## What it is

DualSense "HD haptics" are NOT simple rumble motors. The controller has two Voice Coil Motor
(VCM) linear resonant actuators that are **driven by PCM audio data** — same principle as a speaker.
This gives the PS5 its signature nuanced vibration (rain drops, surface textures, etc).

On PS5 and over USB on PC, this works because the controller appears as a **4-channel audio device**:
- Channels 1-2: Speaker / headphone jack audio
- Channels 3-4: Left / right haptic actuator PCM

Over standard BT on PC, this audio device is NOT exposed — BT HID profile doesn't carry audio.

## How DSX solves it (Windows)

From decompilation of DSX+ v3 Beta (`DSX.dll`, 29MB .NET assembly):

### Architecture
1. **WASAPI loopback capture** — captures game audio from Windows mixer
2. **Resampling** — to 48kHz
3. **4-channel mixing** — separate audio (ch 1-2) and haptics (ch 3-4) buffers
4. **Opus encoding** — `OPUS_APPLICATION_AUDIO` mode, stereo
5. **Packing into HID output reports** — via custom sub-packet structure
6. **Writing to hidraw** — extended BT output reports

### Sub-packet types discovered

DSX uses multiple report types for BT audio streaming:

```
OnTickReportOne_D     — Data only (control, no audio frames)
OnTickReportTwo_HD    — Haptics + Data (1 audio frame)
OnTickReportThree_H   — Haptics only (2 audio frames)
OnTickReportThree_HD  — Haptics + Data (1 audio frame)
OnTickReportFour_A    — Audio (1 frame)
OnTickReportFour_HD   — Haptics + Data (2 audio frames)
OnTickReportFive_AH   — Audio + Haptics (1 frame each)
OnTickReportFive_AD   — Audio + Data (1 frame)
OnTickReportSix_AHD   — Audio + Haptics + Data (1 frame each)
OnTickReportSeven_A   — Audio (2 frames)
OnTickReportEight_AD  — Audio + Data (2 frames)
OnTickReportNine_AH   — Audio + Haptics (2 frames each)
```

Each uses `OutputReportBuilder` to construct packets with composable sub-packets:
- `AddControllerDataSPacket(builder, offset)` — standard controller data (triggers, LEDs etc)
- `AddAudioConfigSPacket(builder, offset, frameType)` — audio configuration
- `AddHapticsSPacket(builder, offset, frameType, pFrame1, pFrame2)` — Opus-encoded haptics
- `WriteHapticsBlock(pcmFrame, builder, offset)` — raw haptics PCM block

### Input report size hint
When BT audio is active, DSX expects **547-byte input reports** (vs normal 78-byte).
This suggests the controller switches to an extended BT report mode.

### Haptics source options
```
Source 0: Sine wave generator
Source 1: System audio loopback capture
Source 2: File playback (WAV/PCM)
Source 3: Virtual DualSense audio device
Source 4: Sine + File combined
```

### PCM format
- 16-bit signed LE PCM internally, converted to float32 for processing
- Scale factor: 1/32768 = 3.0517578E-05f

### Obfuscation barrier

**Critical methods are obfuscated** via a dispatch table:
```csharp
aWIbXv9AnoBV5BGZTU.ai8PRH4QkM(int id, object[] args, object instance)
```

This hides:
- Sub-packet byte layouts (exact offsets within extended reports)
- Packet sequence numbering logic
- Audio config values (frame sizes, buffer counts)
- Report length constants for each packet type

### CRC for extended reports
Same CRC32 algorithm (seed 0xA2 / pre-computed 0xEB1C1A49), but over longer reports.

## What we'd need to replicate on Linux

1. **PipeWire virtual sink** — equivalent of WASAPI loopback, capture game audio
2. **Opus encoder** — libopus, encode 48kHz stereo
3. **Sub-packet builder** — need to reverse the obfuscated dispatch table OR
   sniff BT packets from a Windows machine running DSX (Wireshark + BT sniffer)
4. **Extended report mode** — may need to send a specific feature report to switch
   the controller into extended BT mode (547-byte input reports)

## Possible approaches to break the obfuscation

1. **Dynamic analysis** — run DSX under Wine/Windows VM with BT passthrough,
   intercept HID writes with a hook DLL or LD_PRELOAD shim
2. **BT packet capture** — Wireshark with btmon/hcidump while DSX is running
   on Windows with BT, capture the raw L2CAP HID packets
3. **De-obfuscation** — the dispatch table `aWIbXv9AnoBV5BGZTU` has integer IDs
   mapping to methods. Could reconstruct by tracing all callsites.
4. **de4dot** — .NET deobfuscator, might handle this pattern

## Risk assessment

- Audio haptics over BT is the "holy grail" — no open-source implementation exists on any platform
- DSX is the only known solution and it's Windows-only, closed-source, partially obfuscated
- Even if we crack the packet format, latency over BT may be poor
- Sony could change the protocol in firmware updates
- Legal grey area for RE (personal use / interoperability exception applies in EU)
