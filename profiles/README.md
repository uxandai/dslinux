# Game Profiles

Static JSON profiles that define per-game trigger, rumble, and LED settings.

## Installation

Copy profiles to `~/.config/dualsense/profiles/`:

```bash
mkdir -p ~/.config/dualsense/profiles
cp profiles/*.json ~/.config/dualsense/profiles/
```

## Format

```json
{
  "name": "Game Name",
  "process": ["GameProcess.exe", "game_linux"],
  "steam_appid": 1234567,
  "right_trigger": {
    "mode": "weapon",
    "start": 2,
    "end": 7,
    "strength": 8
  },
  "left_trigger": {
    "mode": "feedback",
    "position": 0,
    "strength": 4
  },
  "lightbar": { "r": 255, "g": 0, "b": 0 },
  "player_leds": 21
}
```

## Supported trigger modes

- `off`
- `feedback` — position (0-9), strength (1-8)
- `weapon` — start (2-7), end (3-8), strength (1-8)
- `vibration` — position (0-9), amplitude (1-8), frequency (1-255)
- `bow` — start, end, strength, snap_force
- `galloping` — start, end, first_foot, second_foot, frequency
- `machine` — start, end, amp_a, amp_b, frequency, period

## Usage with daemon

When dualsensed starts, it loads profiles from `~/.config/dualsense/profiles/`.
When it detects a matching process name (via /proc), it applies the profile.
When the process exits, triggers reset to default.

Profiles can also be applied manually:
```bash
dsctl profile load rdr2
dsctl profile list
```
