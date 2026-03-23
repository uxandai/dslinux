/*
 * libdualsense — core implementation.
 *
 * Manages device state and builds HID output reports for DualSense
 * controllers over both USB and Bluetooth.
 *
 * BT output report (78 bytes, ID 0x31):
 *   [0]      Report ID
 *   [1]      seq_tag (upper nibble) | 0x02
 *   [2]      Feature flags 0 (enable rumble/triggers)
 *   [3]      Feature flags 1 (enable LEDs)
 *   [4]      Right rumble motor
 *   [5]      Left rumble motor
 *   [10]     Mute LED
 *   [12..22] Right trigger (mode + 10 params)
 *   [23..33] Left trigger (mode + 10 params)
 *   [43]     LED brightness mode
 *   [44]     Player/mute brightness
 *   [45]     Player LEDs
 *   [46..48] Lightbar R, G, B
 *   [74..77] CRC32
 *
 * USB output report (64 bytes, ID 0x02):
 *   Same layout shifted by -1 (no seq_tag byte).
 */

#include "dualsense.h"
#include "crc32.h"
#include "triggers.h"
#include "hidraw.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Report sizes */
#define DS_BT_REPORT_SIZE  78
#define DS_USB_REPORT_SIZE 64

/* Report IDs */
#define DS_BT_REPORT_ID   0x31
#define DS_USB_REPORT_ID  0x02

/* Feature flag bits for byte [2] (BT) / [1] (USB) */
#define DS_FLAG0_HAPTICS       0x01  /* Enable rumble motors */
#define DS_FLAG0_TRIGGERS      0x02  /* Enable adaptive triggers */

/* Feature flag bits for byte [3] (BT) / [2] (USB) */
#define DS_FLAG1_MIC_MUTE_LED  0x01
#define DS_FLAG1_POWER_SAVE    0x02
#define DS_FLAG1_LIGHTBAR      0x04
#define DS_FLAG1_PLAYER_LEDS   0x10
#define DS_FLAG1_ALL_LEDS      (DS_FLAG1_LIGHTBAR | DS_FLAG1_PLAYER_LEDS | DS_FLAG1_MIC_MUTE_LED)

/* BT-specific byte offsets */
#define BT_OFF_REPORT_ID    0
#define BT_OFF_SEQ_TAG      1
#define BT_OFF_FLAGS0       2
#define BT_OFF_FLAGS1       3
#define BT_OFF_MOTOR_RIGHT  4
#define BT_OFF_MOTOR_LEFT   5
#define BT_OFF_MUTE_LED     10
#define BT_OFF_R_TRIGGER    12
#define BT_OFF_L_TRIGGER    23
#define BT_OFF_LED_MODE     43
#define BT_OFF_LED_BRIGHT   44
#define BT_OFF_PLAYER_LEDS  45
#define BT_OFF_LIGHTBAR_R   46
#define BT_OFF_LIGHTBAR_G   47
#define BT_OFF_LIGHTBAR_B   48
#define BT_OFF_CRC          74

/* USB offsets = BT offsets - 1 (no seq_tag byte) */
#define USB_OFF_REPORT_ID   0
#define USB_OFF_FLAGS0      1
#define USB_OFF_FLAGS1      2
#define USB_OFF_MOTOR_RIGHT 3
#define USB_OFF_MOTOR_LEFT  4
#define USB_OFF_MUTE_LED    9
#define USB_OFF_R_TRIGGER   11
#define USB_OFF_L_TRIGGER   22
#define USB_OFF_LED_MODE    42
#define USB_OFF_LED_BRIGHT  43
#define USB_OFF_PLAYER_LEDS 44
#define USB_OFF_LIGHTBAR_R  45
#define USB_OFF_LIGHTBAR_G  46
#define USB_OFF_LIGHTBAR_B  47

/* Internal device state */
struct ds_device {
	int fd;
	ds_conn_t conn;
	uint8_t seq;  /* BT sequence tag, 0-15 */

	/* Pending state */
	uint8_t motor_left;
	uint8_t motor_right;

	uint8_t r_trigger[DS_TRIGGER_EFFECT_SIZE];
	uint8_t l_trigger[DS_TRIGGER_EFFECT_SIZE];

	uint8_t lightbar_r, lightbar_g, lightbar_b;
	uint8_t player_leds;
	uint8_t mute_led;

	/* Dirty flags */
	bool dirty_motors;
	bool dirty_r_trigger;
	bool dirty_l_trigger;
	bool dirty_lightbar;
	bool dirty_player_leds;
	bool dirty_mute_led;
};

/* ── Lifecycle ──────────────────────────────────────────────────── */

ds_device_t *ds_open(const char *hidraw_path)
{
	int fd;
	ds_conn_t conn;

	int err = ds_hidraw_open(hidraw_path, &fd, &conn);
	if (err < 0) {
		errno = -err;
		return NULL;
	}

	ds_device_t *dev = calloc(1, sizeof(*dev));
	if (!dev) {
		ds_hidraw_close(fd);
		return NULL;
	}

	dev->fd = fd;
	dev->conn = conn;
	dev->seq = 0;

	/* Initialize triggers to Off */
	ds_effect_off(dev->r_trigger);
	ds_effect_off(dev->l_trigger);

	return dev;
}

void ds_close(ds_device_t *dev)
{
	if (!dev) return;
	ds_hidraw_close(dev->fd);
	free(dev);
}

ds_conn_t ds_connection_type(const ds_device_t *dev)
{
	return dev->conn;
}

/* ── Triggers ───────────────────────────────────────────────────── */

static uint8_t *trigger_buf(ds_device_t *dev, ds_trigger_t side)
{
	if (side == DS_TRIGGER_RIGHT) {
		dev->dirty_r_trigger = true;
		return dev->r_trigger;
	}
	dev->dirty_l_trigger = true;
	return dev->l_trigger;
}

void ds_trigger_off(ds_device_t *dev, ds_trigger_t side)
{
	ds_effect_off(trigger_buf(dev, side));
}

void ds_trigger_feedback(ds_device_t *dev, ds_trigger_t side,
                         uint8_t position, uint8_t strength)
{
	ds_effect_feedback(trigger_buf(dev, side), position, strength);
}

void ds_trigger_feedback_multi(ds_device_t *dev, ds_trigger_t side,
                               const uint8_t strength[10])
{
	ds_effect_feedback_multi(trigger_buf(dev, side), strength);
}

void ds_trigger_slope_feedback(ds_device_t *dev, ds_trigger_t side,
                               uint8_t start_pos, uint8_t end_pos,
                               uint8_t start_str, uint8_t end_str)
{
	ds_effect_slope_feedback(trigger_buf(dev, side),
	                         start_pos, end_pos, start_str, end_str);
}

void ds_trigger_weapon(ds_device_t *dev, ds_trigger_t side,
                       uint8_t start, uint8_t end, uint8_t strength)
{
	ds_effect_weapon(trigger_buf(dev, side), start, end, strength);
}

void ds_trigger_vibration(ds_device_t *dev, ds_trigger_t side,
                          uint8_t position, uint8_t amplitude, uint8_t frequency)
{
	ds_effect_vibration(trigger_buf(dev, side), position, amplitude, frequency);
}

void ds_trigger_vibration_multi(ds_device_t *dev, ds_trigger_t side,
                                uint8_t frequency, const uint8_t amplitude[10])
{
	ds_effect_vibration_multi(trigger_buf(dev, side), frequency, amplitude);
}

void ds_trigger_bow(ds_device_t *dev, ds_trigger_t side,
                    uint8_t start, uint8_t end,
                    uint8_t strength, uint8_t snap_force)
{
	ds_effect_bow(trigger_buf(dev, side), start, end, strength, snap_force);
}

void ds_trigger_galloping(ds_device_t *dev, ds_trigger_t side,
                          uint8_t start, uint8_t end,
                          uint8_t first_foot, uint8_t second_foot,
                          uint8_t frequency)
{
	ds_effect_galloping(trigger_buf(dev, side), start, end,
	                    first_foot, second_foot, frequency);
}

void ds_trigger_machine(ds_device_t *dev, ds_trigger_t side,
                        uint8_t start, uint8_t end,
                        uint8_t amp_a, uint8_t amp_b,
                        uint8_t frequency, uint8_t period)
{
	ds_effect_machine(trigger_buf(dev, side), start, end,
	                  amp_a, amp_b, frequency, period);
}

void ds_trigger_raw(ds_device_t *dev, ds_trigger_t side,
                    uint8_t mode, const uint8_t params[10])
{
	ds_effect_raw(trigger_buf(dev, side), mode, params);
}

/* ── Rumble ──────────────────────────────────────────────────────── */

void ds_rumble(ds_device_t *dev, uint8_t left, uint8_t right)
{
	dev->motor_left = left;
	dev->motor_right = right;
	dev->dirty_motors = true;
}

/* ── LEDs ────────────────────────────────────────────────────────── */

void ds_lightbar(ds_device_t *dev, uint8_t r, uint8_t g, uint8_t b)
{
	dev->lightbar_r = r;
	dev->lightbar_g = g;
	dev->lightbar_b = b;
	dev->dirty_lightbar = true;
}

void ds_player_leds(ds_device_t *dev, uint8_t mask)
{
	dev->player_leds = mask & 0x1F;
	dev->dirty_player_leds = true;
}

void ds_mute_led(ds_device_t *dev, ds_mute_led_t mode)
{
	dev->mute_led = (uint8_t)mode;
	dev->dirty_mute_led = true;
}

/* ── Send ────────────────────────────────────────────────────────── */

int ds_send(ds_device_t *dev)
{
	uint8_t report[DS_BT_REPORT_SIZE];
	memset(report, 0, sizeof(report));

	/* Compute flag bytes based on what's dirty */
	uint8_t flags0 = 0;
	uint8_t flags1 = 0;

	if (dev->dirty_motors)
		flags0 |= DS_FLAG0_HAPTICS;
	if (dev->dirty_r_trigger || dev->dirty_l_trigger)
		flags0 |= DS_FLAG0_TRIGGERS;
	if (dev->dirty_lightbar)
		flags1 |= DS_FLAG1_LIGHTBAR;
	if (dev->dirty_player_leds)
		flags1 |= DS_FLAG1_PLAYER_LEDS;
	if (dev->dirty_mute_led)
		flags1 |= DS_FLAG1_MIC_MUTE_LED;

	/* If nothing dirty, still send triggers (maintain state) */
	if (flags0 == 0 && flags1 == 0)
		flags0 = DS_FLAG0_HAPTICS | DS_FLAG0_TRIGGERS;

	int report_size;

	if (dev->conn == DS_BT) {
		report_size = DS_BT_REPORT_SIZE;
		report[BT_OFF_REPORT_ID] = DS_BT_REPORT_ID;
		report[BT_OFF_SEQ_TAG] = ((dev->seq & 0x0F) << 4) | 0x02;
		dev->seq = (dev->seq + 1) & 0x0F;

		report[BT_OFF_FLAGS0] = flags0;
		report[BT_OFF_FLAGS1] = flags1;
		report[BT_OFF_MOTOR_RIGHT] = dev->motor_right;
		report[BT_OFF_MOTOR_LEFT] = dev->motor_left;
		report[BT_OFF_MUTE_LED] = dev->mute_led;

		memcpy(&report[BT_OFF_R_TRIGGER], dev->r_trigger, DS_TRIGGER_EFFECT_SIZE);
		memcpy(&report[BT_OFF_L_TRIGGER], dev->l_trigger, DS_TRIGGER_EFFECT_SIZE);

		report[BT_OFF_LED_MODE] = 0x02;  /* full brightness */
		report[BT_OFF_PLAYER_LEDS] = dev->player_leds;
		report[BT_OFF_LIGHTBAR_R] = dev->lightbar_r;
		report[BT_OFF_LIGHTBAR_G] = dev->lightbar_g;
		report[BT_OFF_LIGHTBAR_B] = dev->lightbar_b;

		/* CRC32 over bytes [0..73] */
		uint32_t crc = ds_crc32(report, BT_OFF_CRC);
		report[74] = crc & 0xFF;
		report[75] = (crc >> 8) & 0xFF;
		report[76] = (crc >> 16) & 0xFF;
		report[77] = (crc >> 24) & 0xFF;
	} else {
		report_size = DS_USB_REPORT_SIZE;
		report[USB_OFF_REPORT_ID] = DS_USB_REPORT_ID;

		report[USB_OFF_FLAGS0] = flags0;
		report[USB_OFF_FLAGS1] = flags1;
		report[USB_OFF_MOTOR_RIGHT] = dev->motor_right;
		report[USB_OFF_MOTOR_LEFT] = dev->motor_left;
		report[USB_OFF_MUTE_LED] = dev->mute_led;

		memcpy(&report[USB_OFF_R_TRIGGER], dev->r_trigger, DS_TRIGGER_EFFECT_SIZE);
		memcpy(&report[USB_OFF_L_TRIGGER], dev->l_trigger, DS_TRIGGER_EFFECT_SIZE);

		report[USB_OFF_LED_MODE] = 0x02;
		report[USB_OFF_PLAYER_LEDS] = dev->player_leds;
		report[USB_OFF_LIGHTBAR_R] = dev->lightbar_r;
		report[USB_OFF_LIGHTBAR_G] = dev->lightbar_g;
		report[USB_OFF_LIGHTBAR_B] = dev->lightbar_b;
	}

	ssize_t ret = write(dev->fd, report, report_size);
	if (ret < 0)
		return -errno;
	if (ret != report_size)
		return -EIO;

	/* Clear dirty flags */
	dev->dirty_motors = false;
	dev->dirty_r_trigger = false;
	dev->dirty_l_trigger = false;
	dev->dirty_lightbar = false;
	dev->dirty_player_leds = false;
	dev->dirty_mute_led = false;

	return 0;
}
