# What I Need From You (Human Tasks)

These are things that require physical hardware or manual steps that I can't do remotely.

---

## Task 1: Basic Validation (all features)

**What:** Plug in DualSense, run through HOW_TO_TEST.md step by step.

**Time:** ~15 min

**You need:**
- DualSense controller (BT or USB)
- Built project (`./build.sh`)
- Udev rules installed

**What to report back:**
- Which steps passed/failed
- `dsctl info` output (USB or Bluetooth?)
- `dmesg | tail -20` after any failure (CRC errors?)
- Screenshot of GUI if it opens

---

## Task 2: BT Audio & Mic Discovery (automated)

**What:** Run `ds-audio-probe` — it automatically tests all audio-related
capabilities of the controller over BT. This replaces manual BT sniffing.

**Time:** ~2 min (fully automated)

**You need:**
- DualSense paired over **Bluetooth** (not USB — audio features are BT-only)
- Built project (`./build.sh`)
- Udev rules installed

### What to run:

```bash
mkdir -p captures
./build/ds-audio-probe 2>&1 | tee captures/audio-probe.txt
```

### What the tool does (4 probes, all automatic):

| Probe | What it tests | What we're looking for |
|-------|--------------|----------------------|
| **1. Baseline** | Reads normal BT input reports | Expect 78 bytes — establishes normal size |
| **2. Audio flags** | Sends 8 different flag combinations in report 0x31 (the control report) | If any flag makes input reports grow beyond 78 bytes → we found the audio enable switch |
| **3. During haptics** | Starts haptics streaming (report 0x32), then reads input reports | If reports grow to 547 bytes → controller entered extended audio mode with mic data |
| **4. Feature reports** | Reads feature reports 0x05, 0x09, 0x20, 0x22, 0x08, 0x06 | Reveals audio config, firmware version, MAC address |

### What to report back:

Send me the file `captures/audio-probe.txt`. Key things to look for:

- **`*** EXTENDED REPORT DETECTED!`** — this means we found audio data! The hex dump that follows contains mic audio.
- **`*** Possible audio data!`** — non-zero bytes after position 78 in input reports = mic PCM
- **`New report size: XXX bytes`** — any number other than 78 is interesting
- Feature report contents (firmware version etc.)

### If extended reports ARE found:

I can then:
1. Parse the audio format from the hex dump
2. Write a mic capture module
3. Expose it as a PipeWire audio source

### If nothing changes (all 78 bytes):

Then the controller needs a different handshake to enter audio mode. Next step would be Task 3 (BT sniffing from Windows).

---

## Task 3: BT Packet Sniffing (only if Task 2 finds nothing)

**What:** Capture raw BT packets from DSX on Windows while it streams audio to the DualSense.

**Why:** If our automated probe can't trigger extended mode, we need to see what DSX sends differently.

**Time:** ~30 min

**You need:**
- Windows PC (real or VM with BT adapter passthrough)
- DualSense paired over BT to Windows
- DSX+ running with BT Audio Haptics enabled
- Wireshark with USBPcap

### Steps (Wireshark + USBPcap):

1. Install Wireshark on Windows (check "Install USBPcap" during setup)
2. Pair DualSense over BT
3. Open Wireshark, select "USBPcap1" interface
4. Start capture
5. Open DSX+, enable BT Audio/Haptics, play audio for 15 seconds
6. Stop capture, save as `captures/dualsense-bt-audio.pcapng`

### Alternative (btmon on Linux host with VM):

```bash
# On Linux host while Windows VM uses BT adapter:
sudo btmon -w captures/dualsense-bt-capture.btsnoop
# In VM: start DSX, enable BT Audio, play 15 sec
# Ctrl+C to stop btmon
```

### Alternative (DSX debug log):

In DSX settings → Debug → enable "Log HID Reports". Copy the log file to `captures/`.

### What to send me:

Any of: `.pcapng`, `.btsnoop`, or DSX debug log file in `captures/`.

---

## Task 4: SAxense Duplex Watch (passive)

**What:** Check https://github.com/egormanga/SAxense periodically for duplex audio release.

**When it drops:**
```bash
git clone https://github.com/egormanga/SAxense reference/SAxense
```

Tell me and I'll port it.

---

## Priority

1. **Task 1** — basic validation, 15 min, validates everything we built
2. **Task 2** — audio probe, 2 min, may unlock mic without Windows
3. **Task 3** — only if Task 2 finds nothing, needs Windows
4. **Task 4** — passive background check

## Where to put results

```bash
mkdir -p captures
# Task 1: captures/test-results.txt
# Task 2: captures/audio-probe.txt   ← just run the command above
# Task 3: captures/dualsense-bt-audio.pcapng
```
