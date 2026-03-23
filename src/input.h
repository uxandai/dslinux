#ifndef DS_INPUT_H
#define DS_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * DualSense input report parsing.
 *
 * USB report 0x01 (64 bytes) and BT report 0x31 (78 bytes) contain
 * the same data at different offsets:
 *   BT offset = USB offset + 2  (BT has report ID + extra byte prefix)
 *
 * Button/stick data starts at USB byte 0 / BT byte 2.
 */

/* Parsed input state from one report. */
typedef struct {
	/* Sticks: 0-255, center ~128 */
	uint8_t lx, ly;
	uint8_t rx, ry;

	/* Triggers: 0-255 analog */
	uint8_t l2, r2;

	/* D-pad: 0-7 clockwise from up (0=N, 1=NE, ..., 7=NW), 8=released */
	uint8_t dpad;

	/* Face buttons */
	bool square, cross, circle, triangle;

	/* Shoulder */
	bool l1, r1, l2_btn, r2_btn;

	/* Center */
	bool create, options, ps, touchpad_btn, mute;

	/* Sticks click */
	bool l3, r3;

	/* Touchpad: up to 2 fingers */
	struct {
		bool active;
		uint16_t x;   /* 0-1919 */
		uint16_t y;   /* 0-1079 */
		uint8_t id;   /* touch tracking ID */
	} touch[2];

	/* Motion sensors (raw 16-bit signed) */
	int16_t gyro_x, gyro_y, gyro_z;
	int16_t accel_x, accel_y, accel_z;

	/* Battery */
	uint8_t battery_level;   /* 0-100 % */
	bool battery_charging;

	/* Timestamp from controller (in 5.33us units) */
	uint32_t timestamp;

	/* Report sequence number */
	uint8_t seq;
} ds_input_t;

/*
 * Parse a raw HID input report into a ds_input_t struct.
 * @param report   Raw bytes from hidraw read().
 * @param len      Length of report (64 for USB, 78 for BT).
 * @param out      Parsed result.
 * @return 0 on success, -1 if report format is unrecognized.
 */
int ds_input_parse(const uint8_t *report, size_t len, ds_input_t *out);

#endif /* DS_INPUT_H */
