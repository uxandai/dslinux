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

/*
 * Open ALL connected DualSense controllers.
 * @param devs   Output array of device handles (caller-allocated).
 * @param max    Max number of devices (array size).
 * @return Number of devices opened (0 if none found).
 */
int ds_open_all(ds_device_t **devs, int max);

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

/* ── Input reading ────────────────────────────────────────────────── */

/* Parsed controller input state. */
typedef struct {
	uint8_t lx, ly, rx, ry;      /* stick axes, 0-255, center ~128 */
	uint8_t l2, r2;               /* trigger analog, 0-255 */
	uint8_t dpad;                 /* 0-7 clockwise from N, 8=released */
	uint8_t battery_level;        /* 0-100 % */
	uint8_t seq;                  /* report sequence number */
	uint32_t timestamp;           /* controller timestamp (5.33µs units) */

	/* Buttons */
	uint8_t square   :1, cross  :1, circle :1, triangle :1;
	uint8_t l1       :1, r1     :1, l2_btn :1, r2_btn   :1;
	uint8_t create   :1, options:1, ps     :1, touchpad_btn :1;
	uint8_t mute     :1, l3     :1, r3     :1, battery_charging :1;

	/* Touchpad (2 fingers) */
	struct {
		uint16_t x, y;            /* 0-1919, 0-1079 */
		uint8_t id;               /* tracking ID */
		uint8_t active;           /* 1 if touching */
	} touch[2];

	/* IMU (raw signed 16-bit) */
	int16_t gyro_x, gyro_y, gyro_z;
	int16_t accel_x, accel_y, accel_z;
} ds_input_state_t;

/*
 * Read and parse one input report from the controller.
 * @param dev   Device handle.
 * @param out   Parsed state (filled on success).
 * @return 0 on success, -EAGAIN if no data available, negative errno on error.
 *
 * The hidraw fd is non-blocking.  Call in a loop with poll() or select().
 */
int ds_read_input(ds_device_t *dev, ds_input_state_t *out);

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
