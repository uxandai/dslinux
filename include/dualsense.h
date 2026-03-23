/*
 * libdualsense — DualSense adaptive triggers & haptics over BT/USB on Linux
 *
 * Usage pattern:
 *   ds_device_t *dev = ds_open(NULL);  // auto-detect controller
 *   ds_trigger_weapon(dev, DS_TRIGGER_RIGHT, 2, 7, 8);
 *   ds_rumble(dev, 128, 64);
 *   ds_send(dev);   // <-- flush all staged state to controller
 *   ds_close(dev);
 *
 * All setter functions (ds_trigger_*, ds_rumble, ds_lightbar, etc.) only
 * stage state locally.  You MUST call ds_send() to actually transmit the
 * output report to the controller.
 *
 * Thread safety: a single ds_device_t must not be used from multiple threads
 * concurrently.  Different devices may be used from different threads.
 */

#ifndef DUALSENSE_H
#define DUALSENSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque device handle */
typedef struct ds_device ds_device_t;

/* Connection type */
typedef enum {
	DS_USB = 0,
	DS_BT  = 1,
} ds_conn_t;

/* Trigger side */
typedef enum {
	DS_TRIGGER_LEFT  = 0,
	DS_TRIGGER_RIGHT = 1,
	DS_TRIGGER_COUNT,
} ds_trigger_t;

/* Mute LED mode */
typedef enum {
	DS_MUTE_OFF   = 0,
	DS_MUTE_ON    = 1,
	DS_MUTE_PULSE = 2,
} ds_mute_led_t;

/* Known device PIDs (VID is always 0x054C / Sony) */
#define DS_VID              0x054C
#define DS_PID_DUALSENSE    0x0CE6
#define DS_PID_EDGE         0x0DF2

/* ── Device lifecycle ─────────────────────────────────────────────── */

/*
 * Open a DualSense controller.
 * @param hidraw_path  Path like "/dev/hidraw3", or NULL for auto-detect.
 * @return Device handle, or NULL on failure (errno is set).
 */
ds_device_t *ds_open(const char *hidraw_path);

/* Close and free device.  Safe to call with NULL. */
void ds_close(ds_device_t *dev);

/* Query connection type.  dev must not be NULL. */
ds_conn_t ds_connection_type(const ds_device_t *dev);

/* ── Adaptive triggers ────────────────────────────────────────────── */

/*
 * All trigger functions return 0 on success, -1 on invalid parameters.
 * On -1, the trigger state is unchanged.
 */

int ds_trigger_off(ds_device_t *dev, ds_trigger_t side);

/* Continuous resistance from position (0-9) with strength (1-8). */
int ds_trigger_feedback(ds_device_t *dev, ds_trigger_t side,
                        uint8_t position, uint8_t strength);

/* Per-zone resistance: strength[10] array, each 0-8. */
int ds_trigger_feedback_multi(ds_device_t *dev, ds_trigger_t side,
                              const uint8_t strength[10]);

/* Slope from start to end position with linearly interpolated strength. */
int ds_trigger_slope_feedback(ds_device_t *dev, ds_trigger_t side,
                              uint8_t start_pos, uint8_t end_pos,
                              uint8_t start_str, uint8_t end_str);

/* Gun trigger: resistance between start (2-7) and end (>start, ≤8). */
int ds_trigger_weapon(ds_device_t *dev, ds_trigger_t side,
                      uint8_t start, uint8_t end, uint8_t strength);

/* Vibration from position (0-9), amplitude (1-8), frequency (1-255 Hz). */
int ds_trigger_vibration(ds_device_t *dev, ds_trigger_t side,
                         uint8_t position, uint8_t amplitude, uint8_t frequency);

/* Per-zone vibration: amplitude[10] array, each 0-8, plus frequency. */
int ds_trigger_vibration_multi(ds_device_t *dev, ds_trigger_t side,
                               const uint8_t amplitude[10], uint8_t frequency);

/* Bow: weapon with snap-back force. */
int ds_trigger_bow(ds_device_t *dev, ds_trigger_t side,
                   uint8_t start, uint8_t end,
                   uint8_t strength, uint8_t snap_force);

/* Galloping: rhythmic two-foot oscillation. */
int ds_trigger_galloping(ds_device_t *dev, ds_trigger_t side,
                         uint8_t start, uint8_t end,
                         uint8_t first_foot, uint8_t second_foot,
                         uint8_t frequency);

/* Machine: dual-amplitude vibration with period. */
int ds_trigger_machine(ds_device_t *dev, ds_trigger_t side,
                       uint8_t start, uint8_t end,
                       uint8_t amp_a, uint8_t amp_b,
                       uint8_t frequency, uint8_t period);

/* Raw: set mode byte + 10 param bytes directly. */
int ds_trigger_raw(ds_device_t *dev, ds_trigger_t side,
                   uint8_t mode, const uint8_t params[10]);

/* ── Rumble motors ────────────────────────────────────────────────── */

/* Set legacy rumble motors (0-255 each). */
void ds_rumble(ds_device_t *dev, uint8_t left, uint8_t right);

/* ── LEDs ─────────────────────────────────────────────────────────── */

/* Set lightbar color. */
void ds_lightbar(ds_device_t *dev, uint8_t r, uint8_t g, uint8_t b);

/* Set player indicator LEDs (5 LEDs, bitmask bits 0-4). */
void ds_player_leds(ds_device_t *dev, uint8_t mask);

/* Set mute button LED. */
void ds_mute_led(ds_device_t *dev, ds_mute_led_t mode);

/* ── Send ─────────────────────────────────────────────────────────── */

/*
 * Flush all pending state to the controller.
 * @return 0 on success, negative errno on failure (-ENODEV if disconnected).
 */
int ds_send(ds_device_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* DUALSENSE_H */
