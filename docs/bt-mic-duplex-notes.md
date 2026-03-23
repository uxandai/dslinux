# BT Microphone / Duplex Audio — Research Notes

## Status: NOT YET IMPLEMENTED — waiting for SAxense duplex release

## What we know

- DualSense has a built-in microphone (internal mono mic)
- Over USB: mic is exposed as a standard audio input device (4ch UAC)
- Over BT: mic is NOT exposed — same limitation as audio haptics was before SAxense

## Hypothesis

If haptics output uses report 0x32 (host → controller), mic input likely uses:
- Either report 0x32 in the opposite direction (controller → host via input reports)
- Or a separate report ID (e.g., 0x33?)
- Or the extended 547-byte BT input report contains audio data alongside button/sensor data

## SAxense "Duplex audio coming soon"

The SAxense author (Sdore) has announced duplex audio support.
Track: https://github.com/egormanga/SAxense

When released, we should:
1. Read the implementation
2. Port to our haptics.c module
3. Expose mic as PipeWire source

## What to try with a controller

1. Enable haptics streaming (report 0x32)
2. Read input reports — check if they grow in size or change format
3. Look for audio data in extended input reports (547 bytes?)
4. Try reading report 0x32 as an input report
5. Check if a feature report is needed to enable mic mode

## Kernel hints

Linux 6.18 patches (Collabora) add audio jack detection to hid-playstation.
This suggests Sony's protocol has well-defined audio mode switching:
- `DS_OUTPUT_VALID_FLAG0_AUDIO_CONTROL_ENABLE`
- `DS_OUTPUT_VALID_FLAG0_MIC_VOLUME_ENABLE`
- Headset mic insert/remove events via dedicated input device

The flags may need to be set in the output report (0x31) before mic audio
appears in input reports. This is a likely handshake:
1. Set AUDIO_CONTROL_ENABLE + MIC_VOLUME_ENABLE in output report 0x31
2. Controller switches to extended input mode (547 bytes)
3. Input reports now contain mic audio data

## New findings from DSX RE (2026-03-23)

DSX BTStream has separate handling for audio vs haptics in report 0x32:
- **Haptics** = raw PCM sub-packets (sub-packet 0x12)
- **Audio** (speaker/jack) = Opus-encoded sub-packets (sub-packet 0x13?)
- **Both** can coexist in one report (variant `_AHD`)

DSX config flags relevant to mic:
- `BluetoothHapticsConfigSpeakerHeadsetEnabled`
- `BluetoothHapticsAudioOutputMode`: 0=speaker, 1=headset, 2=both

This suggests mic input may be enabled by setting similar flags in report 0x31
config bytes, then reading audio sub-packets from extended input reports.

The 547-byte input report size matches: 78 bytes standard + ~469 bytes audio data.
At 48kHz/16-bit/mono that's ~4.9ms of audio per report — plausible for low-latency mic.

## Experimental plan (needs controller)

1. Start haptics (report 0x32) to put controller in audio mode
2. Set AUDIO_CONTROL_ENABLE flag in report 0x31 byte [2] (flags0)
3. Read input reports — check if size changes from 78 to 547
4. If 547: parse bytes 78+ for audio sub-packets
5. If audio found: decode (likely PCM or Opus) and pipe to PipeWire source
