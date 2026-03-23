# Missing Features — Roadmap

## Status table (as of 2026-03-23)

| # | Feature | Status | Difficulty | Description |
|---|---------|--------|------------|-------------|
| 1 | **Input reading** | TODO | Easy | Parse input reports (0x01 USB / 0x31 BT) — buttons, sticks, triggers analog values, touchpad XY + touch state, gyroscope (3-axis), accelerometer (3-axis). Currently we only write output reports. Adding input allows the library to report full controller state. |
| 2 | **Battery API** | TODO | Easy | Battery level (0-100%) and charging status are in the input report at byte 53 (USB) / 54 (BT). Expose via `ds_battery_level()` and `ds_battery_charging()`. GUI has a hacky direct-read, library should own this. |
| 3 | **Auto-reconnect daemon** | TODO | Medium | When the controller disconnects (BT drop, USB unplug), `write()` returns -ENODEV and the daemon currently dies or loops errors. Should detect disconnect, close fd, poll for reconnection via udev_monitor or periodic hidraw rescan, and reopen transparently. |
| 4 | **Multi-controller** | TODO | Medium | Daemon currently opens only the first detected DualSense. Should support N controllers, each with independent state. DSX protocol already uses `Parameters[0]` as controller index — we ignore it and always use controller 0. |
| 5 | **Static game profiles** | TODO | Easy | JSON files in `~/.config/dualsense/profiles/` defining per-game trigger/LED presets. Daemon loads profile when game starts (detect by process name or Steam AppID). Format: `{"name":"RDR2", "process":"RDR2.exe", "right_trigger":{"mode":"weapon","start":2,"end":7,"strength":8}}` |
| 6 | **Python bindings** | TODO | Easy | ctypes wrapper around libdualsense.so. Allows Python scripts to call `ds_open()`, `ds_trigger_weapon()`, `ds_send()` etc. Useful for rapid prototyping, custom per-game scripts, and integration with tools like evdev/pygame. |
| 7 | **pkg-config file** | TODO | Trivial | Generate `dualsense.pc` at build time so users can `pkg-config --cflags --libs dualsense` to link against the library. Standard for C libraries on Linux. |
| 8 | **AUR / packaging** | TODO | Easy | PKGBUILD for Arch Linux (and derivatives like CachyOS). Enables `makepkg -si` one-liner install. Could also add .deb/.rpm spec later. |
| 9 | **DualSense Edge extras** | TODO | Medium | Edge (PID 0x0DF2) has additional features: back buttons (2 paddles), custom profiles (4 switchable), stick dead zone tuning, trigger dead zone tuning. These use bytes [49-59] in the output report and additional input report fields. |
| 10 | **Audio jack / speaker over BT** | Research | Hard | Over USB the controller is a 4-channel audio device (ch 1-2 = speaker/jack, ch 3-4 = haptics). Over BT we cracked haptics (report 0x32, ch 3-4). Speaker/jack audio (ch 1-2) likely uses the same report with different sub-packet config or additional sub-packets. DSX has "Audio + Haptics" combined report types. Needs RE. |
| 11 | **Microphone over BT** | Research | Hard | Built-in mic works over USB as audio input. Over BT needs RE of input-direction audio reports. SAxense author promises "duplex audio coming soon". |
