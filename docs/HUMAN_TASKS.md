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
- Screenshots of GUI if it opens

---

## Task 2: BT Packet Sniffing (for #18 — Audio Jack over BT)

**What:** Capture raw BT HID packets from DSX on Windows while it sends audio to the DualSense speaker/jack.

**Why:** We know audio uses report 0x32 with Opus-encoded sub-packets, but we don't know the exact sub-packet ID (probably 0x13) or the audio config bytes. One capture session will give us everything.

**Time:** ~30 min

**You need:**
- Windows PC (real or VM with BT passthrough)
- DualSense paired over BT to the Windows PC
- DSX+ installed and running (you have it on Steam)
- Wireshark installed on Windows

### Steps:

#### Option A: Wireshark + USBPcap (easiest if BT adapter is USB)

1. **Install Wireshark** on Windows: https://www.wireshark.org/download.html
   - During install, check "Install USBPcap" — this captures USB traffic including BT HCI

2. **Pair DualSense** to Windows PC over Bluetooth

3. **Start Wireshark**, select "USBPcap1" interface (not Ethernet/Wi-Fi)

4. **Set capture filter** (optional, reduces noise):
   ```
   usb.transfer_type == 0x03
   ```

5. **Start capture** (green shark fin button)

6. **Open DSX+**, connect to the DualSense over BT

7. **Enable BT Audio Haptics** in DSX settings:
   - Go to Haptics/Audio tab
   - Enable "Bluetooth Audio/Haptics"
   - Set audio source to "Loopback" or play a test file
   - Make sure you hear audio from the controller speaker or feel haptics

8. **Let it run for 10-15 seconds** with audio playing

9. **Stop capture** in Wireshark

10. **Save as** `dualsense-bt-audio.pcapng`

11. **Apply display filter** to find our reports:
    ```
    btatt.value[0] == 0x32
    ```
    or
    ```
    bthid.data[0] == 0x32
    ```

12. **Export filtered packets**: File → Export Specified Packets → save as `report_0x32_only.pcapng`

#### Option B: btmon on Linux (if you have BT adapter passthrough to Windows VM)

Actually simpler — run btmon on the Linux host while DSX runs in a Windows VM:

1. Pair DualSense to VM's BT (via USB BT adapter passthrough)
2. On Linux host:
   ```bash
   sudo btmon -w dualsense-bt-capture.btsnoop
   ```
3. In VM: start DSX, enable BT Audio Haptics, play audio for 15 sec
4. Stop btmon (Ctrl+C)
5. Convert to pcap:
   ```bash
   tshark -r dualsense-bt-capture.btsnoop -w dualsense-bt-audio.pcapng
   ```

#### Option C: Android BT HCI snoop log

If you have an Android phone:
1. Enable Developer Options → Enable Bluetooth HCI snoop log
2. Pair DualSense to phone
3. Use an app that sends haptics/audio to controller
4. Pull the log: `adb pull /data/misc/bluetooth/logs/btsnoop_hci.log`

### What I need from the capture:

Send me the .pcapng or .btsnoop file. I will extract:

1. **Report 0x32 packets with audio** — compare with haptics-only packets
2. **Sub-packet ID for Opus audio** — the byte that differs from 0x12
3. **Audio config sub-packet** — what bytes change when audio is enabled vs disabled
4. **Report size** — is it still 141 bytes or longer when audio is included?
5. **Opus frame size** — how many bytes per Opus frame
6. **Interleaving pattern** — how audio and haptics sub-packets alternate

### Alternative: Quick hex dump (no Wireshark needed)

If Wireshark is too complex, you can do this:

1. On Windows with DSX running + BT audio active:
   ```powershell
   # PowerShell - list HID devices
   Get-PnpDevice -Class HIDClass | Where-Object {$_.FriendlyName -like "*DualSense*"}
   ```

2. Or use **HID Logger** tool: https://github.com/todbot/hidapitester
   ```
   hidapitester --vidpid 054C:0CE6 --open --read-feature 0x32 --length 141
   ```

3. **Even simpler**: In DSX, go to Settings → Debug → enable "Log HID Reports". Then find the log file in `%LOCALAPPDATA%/DSX/` and send it to me.

---

## Task 3: Mic / Duplex Audio Testing (for #19)

**What:** Check if the DualSense sends audio data (from its mic) over BT when haptics streaming is active.

**Time:** ~10 min

**You need:**
- DualSense paired over BT on Linux
- Built project with haptics support

### Steps:

1. **Start haptics streaming:**
   ```bash
   ./build/ds-haptics-test --sine 150
   ```

2. **In another terminal, read raw input reports and check size:**
   ```bash
   # Check input report size (normally 78 for BT)
   sudo cat /dev/hidraw3 | xxd | head -5
   ```
   Note the length — if it's bigger than 78 (especially 547), mic data may be present.

3. **Try enabling audio flags in output report 0x31:**
   ```bash
   # Set audio control enable flag (bit in flags0)
   # We'd need a small test tool for this — I can write one
   ```

4. **Monitor with btmon:**
   ```bash
   sudo btmon | grep -i "size\|report\|audio"
   ```
   Check if input report sizes change after haptics is started.

### What to report:
- Input report size before haptics: ___ bytes
- Input report size during haptics: ___ bytes
- Any change in btmon output
- `dmesg` output if anything appears

---

## Task 4: SAxense Duplex Monitoring

**What:** Watch the SAxense repo for duplex audio release.

**URL:** https://github.com/egormanga/SAxense

**When it drops:**
1. Read the new code
2. Tell me about it — or I can read it directly if you pull the repo:
   ```bash
   git clone https://github.com/egormanga/SAxense /home/pp/Praca/AI/dualsenselinux/reference/SAxense
   ```

---

## Priority order

1. **Task 1** (basic validation) — do this first, takes 15 min, validates everything
2. **Task 3** (mic test) — quick, do it right after Task 1 while pad is connected
3. **Task 2** (BT sniff) — needs Windows, takes 30 min, unlocks audio jack feature
4. **Task 4** (SAxense watch) — passive, just check periodically

## Files to send me

After completing tasks, drop files here:
```
/home/pp/Praca/AI/dualsenselinux/captures/
```

Create the dir and put captures there:
```bash
mkdir -p captures
# e.g.:
# captures/dualsense-bt-audio.pcapng   (Task 2)
# captures/input-report-sizes.txt      (Task 3)
# captures/test-results.txt            (Task 1)
```
