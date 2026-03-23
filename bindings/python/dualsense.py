"""
Python bindings for libdualsense — DualSense adaptive triggers & haptics.

Usage:
    from dualsense import DualSense, Trigger

    ds = DualSense()                             # auto-detect
    ds.trigger_weapon(Trigger.RIGHT, 2, 7, 8)    # gun trigger
    ds.rumble(200, 200)                           # vibrate
    ds.lightbar(255, 0, 0)                        # red lightbar
    ds.send()                                     # flush to controller

    input = ds.read_input()                       # read buttons/sticks
    print(f"Battery: {input.battery_level}%")

    ds.close()
"""

import ctypes
import ctypes.util
import enum
import os

# ── Load shared library ─────────────────────────────────────────

_lib = None

def _load_lib():
    global _lib
    if _lib is not None:
        return _lib

    # Try common paths
    paths = [
        os.path.join(os.path.dirname(__file__), '..', '..', 'build', 'libdualsense.so'),
        'libdualsense.so',
        'libdualsense.so.0',
    ]

    # Also try pkg-config / ldconfig
    found = ctypes.util.find_library('dualsense')
    if found:
        paths.insert(0, found)

    for path in paths:
        try:
            _lib = ctypes.CDLL(path)
            return _lib
        except OSError:
            continue

    raise RuntimeError(
        "Cannot find libdualsense.so. Build the project first:\n"
        "  ./build.sh\n"
        "Or install system-wide:\n"
        "  ./install.sh"
    )


# ── Enums ───────────────────────────────────────────────────────

class Trigger(enum.IntEnum):
    LEFT = 0
    RIGHT = 1

class Connection(enum.IntEnum):
    USB = 0
    BT = 1

class MuteLed(enum.IntEnum):
    OFF = 0
    ON = 1
    PULSE = 2


# ── Input state struct ──────────────────────────────────────────

class _TouchPoint(ctypes.Structure):
    _fields_ = [
        ('x', ctypes.c_uint16),
        ('y', ctypes.c_uint16),
        ('id', ctypes.c_uint8),
        ('active', ctypes.c_uint8),
    ]

class InputState(ctypes.Structure):
    _fields_ = [
        ('lx', ctypes.c_uint8), ('ly', ctypes.c_uint8),
        ('rx', ctypes.c_uint8), ('ry', ctypes.c_uint8),
        ('l2', ctypes.c_uint8), ('r2', ctypes.c_uint8),
        ('dpad', ctypes.c_uint8),
        ('battery_level', ctypes.c_uint8),
        ('seq', ctypes.c_uint8),
        ('timestamp', ctypes.c_uint32),
        # Bitfield byte 1
        ('_buttons1', ctypes.c_uint8),
        # Bitfield byte 2
        ('_buttons2', ctypes.c_uint8),
        # Touch
        ('touch', _TouchPoint * 2),
        # IMU
        ('gyro_x', ctypes.c_int16), ('gyro_y', ctypes.c_int16), ('gyro_z', ctypes.c_int16),
        ('accel_x', ctypes.c_int16), ('accel_y', ctypes.c_int16), ('accel_z', ctypes.c_int16),
    ]

    @property
    def square(self): return bool(self._buttons1 & 0x01)
    @property
    def cross(self): return bool(self._buttons1 & 0x02)
    @property
    def circle(self): return bool(self._buttons1 & 0x04)
    @property
    def triangle(self): return bool(self._buttons1 & 0x08)
    @property
    def l1(self): return bool(self._buttons1 & 0x10)
    @property
    def r1(self): return bool(self._buttons1 & 0x20)
    @property
    def l2_btn(self): return bool(self._buttons1 & 0x40)
    @property
    def r2_btn(self): return bool(self._buttons1 & 0x80)
    @property
    def create(self): return bool(self._buttons2 & 0x01)
    @property
    def options(self): return bool(self._buttons2 & 0x02)
    @property
    def ps(self): return bool(self._buttons2 & 0x04)
    @property
    def touchpad_btn(self): return bool(self._buttons2 & 0x08)
    @property
    def mute(self): return bool(self._buttons2 & 0x10)
    @property
    def l3(self): return bool(self._buttons2 & 0x20)
    @property
    def r3(self): return bool(self._buttons2 & 0x40)
    @property
    def battery_charging(self): return bool(self._buttons2 & 0x80)


# ── Main class ──────────────────────────────────────────────────

class DualSense:
    """High-level interface to a DualSense controller."""

    def __init__(self, device_path=None):
        """
        Open a DualSense controller.

        Args:
            device_path: hidraw path like "/dev/hidraw3", or None for auto-detect.
        """
        self._lib = _load_lib()
        self._setup_functions()

        path = device_path.encode() if device_path else None
        self._dev = self._lib.ds_open(path)
        if not self._dev:
            raise RuntimeError(
                "Failed to open DualSense. Is it connected? "
                "Check udev rules and permissions."
            )

    def _setup_functions(self):
        L = self._lib
        L.ds_open.restype = ctypes.c_void_p
        L.ds_open.argtypes = [ctypes.c_char_p]
        L.ds_close.argtypes = [ctypes.c_void_p]
        L.ds_connection_type.restype = ctypes.c_int
        L.ds_connection_type.argtypes = [ctypes.c_void_p]
        L.ds_send.restype = ctypes.c_int
        L.ds_send.argtypes = [ctypes.c_void_p]

        # Triggers (all return int)
        for name in ['ds_trigger_off']:
            fn = getattr(L, name)
            fn.restype = ctypes.c_int
            fn.argtypes = [ctypes.c_void_p, ctypes.c_int]

        L.ds_trigger_feedback.restype = ctypes.c_int
        L.ds_trigger_feedback.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint8, ctypes.c_uint8]

        L.ds_trigger_weapon.restype = ctypes.c_int
        L.ds_trigger_weapon.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8]

        L.ds_trigger_vibration.restype = ctypes.c_int
        L.ds_trigger_vibration.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8]

        L.ds_trigger_bow.restype = ctypes.c_int
        L.ds_trigger_bow.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8]

        L.ds_trigger_galloping.restype = ctypes.c_int
        L.ds_trigger_galloping.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8]

        L.ds_trigger_machine.restype = ctypes.c_int
        L.ds_trigger_machine.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8]

        # Rumble / LEDs
        L.ds_rumble.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8]
        L.ds_lightbar.argtypes = [ctypes.c_void_p, ctypes.c_uint8, ctypes.c_uint8, ctypes.c_uint8]
        L.ds_player_leds.argtypes = [ctypes.c_void_p, ctypes.c_uint8]
        L.ds_mute_led.argtypes = [ctypes.c_void_p, ctypes.c_int]

    def close(self):
        if self._dev:
            self._lib.ds_close(self._dev)
            self._dev = None

    def __del__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    @property
    def connection(self):
        return Connection(self._lib.ds_connection_type(self._dev))

    # ── Triggers ─────────────────────────────────────────────────

    def trigger_off(self, side):
        return self._lib.ds_trigger_off(self._dev, int(side))

    def trigger_feedback(self, side, position, strength):
        return self._lib.ds_trigger_feedback(self._dev, int(side), position, strength)

    def trigger_weapon(self, side, start, end, strength):
        return self._lib.ds_trigger_weapon(self._dev, int(side), start, end, strength)

    def trigger_vibration(self, side, position, amplitude, frequency):
        return self._lib.ds_trigger_vibration(self._dev, int(side), position, amplitude, frequency)

    def trigger_bow(self, side, start, end, strength, snap_force):
        return self._lib.ds_trigger_bow(self._dev, int(side), start, end, strength, snap_force)

    def trigger_galloping(self, side, start, end, first_foot, second_foot, frequency):
        return self._lib.ds_trigger_galloping(self._dev, int(side), start, end, first_foot, second_foot, frequency)

    def trigger_machine(self, side, start, end, amp_a, amp_b, frequency, period):
        return self._lib.ds_trigger_machine(self._dev, int(side), start, end, amp_a, amp_b, frequency, period)

    # ── Rumble / LEDs ────────────────────────────────────────────

    def rumble(self, left, right):
        self._lib.ds_rumble(self._dev, left, right)

    def lightbar(self, r, g, b):
        self._lib.ds_lightbar(self._dev, r, g, b)

    def player_leds(self, mask):
        self._lib.ds_player_leds(self._dev, mask)

    def mute_led(self, mode):
        self._lib.ds_mute_led(self._dev, int(mode))

    # ── Send ─────────────────────────────────────────────────────

    def send(self):
        ret = self._lib.ds_send(self._dev)
        if ret < 0:
            raise OSError(-ret, os.strerror(-ret))
