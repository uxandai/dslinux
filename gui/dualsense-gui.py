#!/usr/bin/env python3
"""
DualSense Controller GUI — GTK4 application for managing DualSense
controllers via the dualsensed daemon or direct hidraw access.

Shows connected controllers, battery status, and provides controls
for adaptive triggers, rumble, lightbar, and LEDs.
"""

import json
import os
import socket
import struct
import sys
import threading
import time

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")
from gi.repository import Adw, Gio, GLib, Gtk

# ── hidraw direct access (no daemon needed for reading input) ────

DS_VID = 0x054C
DS_PID_DUALSENSE = 0x0CE6
DS_PID_EDGE = 0x0DF2


def find_dualsense_hidraw():
    """Scan /sys/class/hidraw/ for DualSense devices."""
    devices = []
    sysfs = "/sys/class/hidraw"
    if not os.path.isdir(sysfs):
        return devices

    for name in sorted(os.listdir(sysfs)):
        if not name.startswith("hidraw"):
            continue
        uevent_path = os.path.join(sysfs, name, "device", "uevent")
        try:
            with open(uevent_path) as f:
                content = f.read()
        except OSError:
            continue

        for line in content.splitlines():
            if line.startswith("HID_ID="):
                parts = line[7:].split(":")
                if len(parts) == 3:
                    vid = int(parts[1], 16)
                    pid = int(parts[2], 16)
                    if vid == DS_VID and pid in (DS_PID_DUALSENSE, DS_PID_EDGE):
                        bus = int(parts[0], 16)
                        conn = "Bluetooth" if bus == 5 else "USB"
                        devpath = f"/dev/{name}"
                        model = "DualSense Edge" if pid == DS_PID_EDGE else "DualSense"
                        devices.append({
                            "path": devpath,
                            "name": name,
                            "model": model,
                            "connection": conn,
                            "vid": vid,
                            "pid": pid,
                        })
    return devices


def read_battery_from_hidraw(devpath):
    """Read input report and extract battery info. Returns (level%, charging)."""
    try:
        fd = os.open(devpath, os.O_RDONLY | os.O_NONBLOCK)
    except OSError:
        return None, None

    try:
        data = os.read(fd, 78)
    except OSError:
        os.close(fd)
        return None, None
    os.close(fd)

    if len(data) < 55:
        return None, None

    # BT extended report (0x31): battery at offset 54
    # USB report (0x01): battery at offset 53
    if data[0] == 0x31 and len(data) >= 55:
        battery_byte = data[54]
    elif data[0] == 0x01 and len(data) >= 54:
        battery_byte = data[53]
    else:
        return None, None

    level = min((battery_byte & 0x0F) * 10 + 5, 100)
    charging = (battery_byte & 0xF0) >= 0x20
    return level, charging


# ── Daemon communication ────────────────────────────────────────

class DaemonClient:
    def __init__(self):
        self.sock_path = self._get_sock_path()

    @staticmethod
    def _get_sock_path():
        xdg = os.environ.get("XDG_RUNTIME_DIR")
        if xdg:
            return os.path.join(xdg, "dualsensed.sock")
        return f"/tmp/dualsensed-{os.getuid()}.sock"

    def is_daemon_running(self):
        return os.path.exists(self.sock_path)

    def send_command(self, cmd_dict):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(self.sock_path)
            s.sendall((json.dumps(cmd_dict) + "\n").encode())
            resp = s.recv(4096).decode().strip()
            s.close()
            return json.loads(resp) if resp else {"ok": False, "error": "no response"}
        except Exception as e:
            return {"ok": False, "error": str(e)}


# ── GTK4 Application ───────────────────────────────────────────

class TriggerControls(Gtk.Box):
    def __init__(self, side, daemon):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        self.side = side
        self.daemon = daemon

        label = Gtk.Label(label=f"{side.upper()} Trigger", css_classes=["title-3"])
        self.append(label)

        # Mode selector
        mode_box = Gtk.Box(spacing=6)
        mode_label = Gtk.Label(label="Mode:")
        self.mode_combo = Gtk.DropDown.new_from_strings([
            "Off", "Feedback", "Weapon", "Vibration", "Bow", "Galloping", "Machine"
        ])
        self.mode_combo.set_selected(0)
        self.mode_combo.connect("notify::selected", self._on_mode_changed)
        mode_box.append(mode_label)
        mode_box.append(self.mode_combo)
        self.append(mode_box)

        # Parameter sliders
        self.params_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=4)
        self.append(self.params_box)

        self.sliders = {}
        self._build_params("Off")

        # Apply button
        apply_btn = Gtk.Button(label="Apply")
        apply_btn.connect("clicked", self._on_apply)
        apply_btn.add_css_class("suggested-action")
        self.append(apply_btn)

    def _add_slider(self, name, label_text, min_val, max_val, default=0):
        box = Gtk.Box(spacing=8)
        label = Gtk.Label(label=f"{label_text}:", width_chars=12, xalign=0)
        adj = Gtk.Adjustment(value=default, lower=min_val, upper=max_val,
                             step_increment=1, page_increment=1)
        scale = Gtk.Scale(orientation=Gtk.Orientation.HORIZONTAL, adjustment=adj,
                          hexpand=True, digits=0, draw_value=True)
        box.append(label)
        box.append(scale)
        self.params_box.append(box)
        self.sliders[name] = adj

    def _clear_params(self):
        while (child := self.params_box.get_first_child()):
            self.params_box.remove(child)
        self.sliders.clear()

    def _build_params(self, mode):
        self._clear_params()
        if mode == "Feedback":
            self._add_slider("position", "Position", 0, 9, 0)
            self._add_slider("strength", "Strength", 1, 8, 4)
        elif mode == "Weapon":
            self._add_slider("start", "Start", 2, 7, 2)
            self._add_slider("end", "End", 3, 8, 7)
            self._add_slider("strength", "Strength", 1, 8, 8)
        elif mode == "Vibration":
            self._add_slider("position", "Position", 0, 9, 0)
            self._add_slider("amplitude", "Amplitude", 1, 8, 8)
            self._add_slider("frequency", "Frequency", 1, 255, 30)
        elif mode == "Bow":
            self._add_slider("start", "Start", 0, 8, 1)
            self._add_slider("end", "End", 1, 8, 5)
            self._add_slider("strength", "Strength", 1, 8, 6)
            self._add_slider("snap", "Snap Force", 1, 8, 4)
        elif mode == "Galloping":
            self._add_slider("start", "Start", 0, 8, 0)
            self._add_slider("end", "End", 1, 9, 8)
            self._add_slider("first_foot", "Foot 1", 0, 6, 2)
            self._add_slider("second_foot", "Foot 2", 1, 7, 5)
            self._add_slider("frequency", "Frequency", 1, 255, 20)
        elif mode == "Machine":
            self._add_slider("start", "Start", 0, 8, 1)
            self._add_slider("end", "End", 1, 9, 8)
            self._add_slider("amp_a", "Amplitude A", 0, 7, 5)
            self._add_slider("amp_b", "Amplitude B", 0, 7, 3)
            self._add_slider("frequency", "Frequency", 1, 255, 25)
            self._add_slider("period", "Period", 1, 255, 5)

    def _on_mode_changed(self, dropdown, _pspec):
        modes = ["Off", "Feedback", "Weapon", "Vibration", "Bow", "Galloping", "Machine"]
        selected = modes[dropdown.get_selected()]
        self._build_params(selected)

    def _get_mode_name(self):
        modes = ["off", "feedback", "weapon", "vibration", "bow", "galloping", "machine"]
        return modes[self.mode_combo.get_selected()]

    def _on_apply(self, _btn):
        mode = self._get_mode_name()
        cmd = {"cmd": "trigger", "side": self.side[0].upper(), "mode": mode}
        for name, adj in self.sliders.items():
            cmd[name] = int(adj.get_value())
        resp = self.daemon.send_command(cmd)
        if not resp.get("ok"):
            print(f"Error: {resp.get('error', 'unknown')}", file=sys.stderr)


class ControllerPanel(Gtk.Box):
    def __init__(self, device_info, daemon):
        super().__init__(orientation=Gtk.Orientation.VERTICAL, spacing=12,
                         margin_top=12, margin_bottom=12, margin_start=12, margin_end=12)
        self.device = device_info
        self.daemon = daemon

        # Header
        header = Gtk.Box(spacing=12)
        icon = Gtk.Image.new_from_icon_name("input-gaming-symbolic")
        icon.set_pixel_size(48)
        header.append(icon)

        info_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=2)
        name_label = Gtk.Label(label=device_info["model"], css_classes=["title-2"], xalign=0)
        conn_label = Gtk.Label(
            label=f"{device_info['connection']} — {device_info['path']}",
            css_classes=["dim-label"], xalign=0
        )
        info_box.append(name_label)
        info_box.append(conn_label)
        header.append(info_box)
        self.append(header)

        # Battery
        self.battery_label = Gtk.Label(label="Battery: reading...", xalign=0)
        self.append(self.battery_label)
        self._update_battery()

        self.append(Gtk.Separator())

        # Triggers
        triggers_box = Gtk.Box(spacing=24, homogeneous=True)
        triggers_box.append(TriggerControls("left", daemon))
        triggers_box.append(TriggerControls("right", daemon))
        self.append(triggers_box)

        self.append(Gtk.Separator())

        # Rumble
        rumble_frame = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        rumble_frame.append(Gtk.Label(label="Rumble", css_classes=["title-3"]))
        rumble_sliders = Gtk.Box(spacing=12, homogeneous=True)

        left_box = Gtk.Box(spacing=8)
        left_box.append(Gtk.Label(label="Left:", width_chars=6))
        self.rumble_left = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 255, 1)
        self.rumble_left.set_hexpand(True)
        self.rumble_left.set_draw_value(True)
        left_box.append(self.rumble_left)
        rumble_sliders.append(left_box)

        right_box = Gtk.Box(spacing=8)
        right_box.append(Gtk.Label(label="Right:", width_chars=6))
        self.rumble_right = Gtk.Scale.new_with_range(Gtk.Orientation.HORIZONTAL, 0, 255, 1)
        self.rumble_right.set_hexpand(True)
        self.rumble_right.set_draw_value(True)
        right_box.append(self.rumble_right)
        rumble_sliders.append(right_box)

        rumble_frame.append(rumble_sliders)

        rumble_btn = Gtk.Button(label="Apply Rumble")
        rumble_btn.connect("clicked", self._on_rumble)
        rumble_frame.append(rumble_btn)
        self.append(rumble_frame)

        self.append(Gtk.Separator())

        # Lightbar
        light_frame = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=6)
        light_frame.append(Gtk.Label(label="Lightbar", css_classes=["title-3"]))

        color_box = Gtk.Box(spacing=12)
        self.color_btn = Gtk.ColorDialogButton(dialog=Gtk.ColorDialog())
        color_box.append(Gtk.Label(label="Color:"))
        color_box.append(self.color_btn)

        light_apply = Gtk.Button(label="Apply Color")
        light_apply.connect("clicked", self._on_lightbar)
        color_box.append(light_apply)

        light_frame.append(color_box)
        self.append(light_frame)

        # Player LEDs
        led_frame = Gtk.Box(spacing=8)
        led_frame.append(Gtk.Label(label="Player LEDs:"))
        self.led_checks = []
        for i in range(5):
            check = Gtk.CheckButton(label=str(i + 1))
            check.connect("toggled", self._on_player_leds)
            led_frame.append(check)
            self.led_checks.append(check)
        self.append(led_frame)

    def _update_battery(self):
        def _read():
            level, charging = read_battery_from_hidraw(self.device["path"])
            if level is not None:
                status = "charging" if charging else "discharging"
                text = f"Battery: {level}% ({status})"
            else:
                text = "Battery: unable to read"
            GLib.idle_add(self.battery_label.set_label, text)

        threading.Thread(target=_read, daemon=True).start()

    def _on_rumble(self, _btn):
        left = int(self.rumble_left.get_value())
        right = int(self.rumble_right.get_value())
        self.daemon.send_command({"cmd": "rumble", "left": left, "right": right})

    def _on_lightbar(self, _btn):
        rgba = self.color_btn.get_rgba()
        r = int(rgba.red * 255)
        g = int(rgba.green * 255)
        b = int(rgba.blue * 255)
        self.daemon.send_command({"cmd": "lightbar", "r": r, "g": g, "b": b})

    def _on_player_leds(self, _check):
        mask = 0
        for i, c in enumerate(self.led_checks):
            if c.get_active():
                mask |= (1 << i)
        self.daemon.send_command({"cmd": "player-leds", "mask": mask})


class DualSenseApp(Adw.Application):
    def __init__(self):
        super().__init__(application_id="dev.dualsense.gui",
                         flags=Gio.ApplicationFlags.DEFAULT_FLAGS)
        self.daemon = DaemonClient()

    def do_activate(self):
        win = Adw.ApplicationWindow(application=self, title="DualSense Control",
                                    default_width=700, default_height=600)

        toolbar_view = Adw.ToolbarView()
        header = Adw.HeaderBar()
        toolbar_view.add_top_bar(header)

        # Refresh button
        refresh_btn = Gtk.Button(icon_name="view-refresh-symbolic", tooltip_text="Refresh devices")
        refresh_btn.connect("clicked", lambda _: self._refresh(content_box))
        header.pack_start(refresh_btn)

        # Daemon status
        self.daemon_label = Gtk.Label()
        header.pack_end(self.daemon_label)

        scroll = Gtk.ScrolledWindow(vexpand=True)
        content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        scroll.set_child(content_box)
        toolbar_view.set_content(scroll)

        win.set_content(toolbar_view)
        self._refresh(content_box)
        win.present()

    def _refresh(self, container):
        while (child := container.get_first_child()):
            container.remove(child)

        daemon_ok = self.daemon.is_daemon_running()
        self.daemon_label.set_label(
            "daemon: running" if daemon_ok else "daemon: not running"
        )

        devices = find_dualsense_hidraw()
        if not devices:
            placeholder = Adw.StatusPage(
                icon_name="input-gaming-symbolic",
                title="No DualSense Found",
                description="Connect a DualSense controller via USB or Bluetooth."
            )
            container.append(placeholder)
            return

        for dev in devices:
            panel = ControllerPanel(dev, self.daemon)
            container.append(panel)

        if not daemon_ok:
            banner = Adw.Banner(
                title="Daemon not running — start dualsensed to control triggers",
                revealed=True
            )
            container.prepend(banner)


def main():
    app = DualSenseApp()
    return app.run(sys.argv)


if __name__ == "__main__":
    sys.exit(main())
