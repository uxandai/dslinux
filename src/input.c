/*
 * DualSense input report parsing.
 *
 * Input report layout (offsets relative to data start):
 *   USB: report ID 0x01 at byte [0], data starts at [1]
 *   BT:  report ID 0x31 at byte [0], data starts at [2]
 *
 * Data layout (same for USB and BT, just different base offset):
 *   [0]      LX stick
 *   [1]      LY stick
 *   [2]      RX stick
 *   [3]      RY stick
 *   [4]      L2 trigger analog
 *   [5]      R2 trigger analog
 *   [6]      Sequence counter
 *   [7]      Buttons byte 0: dpad(4 bits) | triangle, circle, cross, square
 *   [8]      Buttons byte 1: R3,L3, options, create, R2btn, L2btn, R1, L1
 *   [9]      Buttons byte 2: PS, touchpad, mute (+ counter bits)
 *   [10..13] Timestamp (LE u32, 5.33µs units)
 *   [14..19] Gyro: X, Y, Z (LE s16 each)
 *   [20..25] Accel: X, Y, Z (LE s16 each)
 *   [32]     Touch 0: active(1 bit) | id(7 bits)
 *   [33..35] Touch 0: x(12 bits) | y(12 bits)
 *   [36]     Touch 1: active | id
 *   [37..39] Touch 1: x | y
 *   [52]     Battery: level(4 bits) | status(4 bits)
 *
 * References:
 *   - Linux kernel hid-playstation.c
 *   - nondebug/dualsense
 *   - DSX decompilation
 */

#include "input.h"
#include <string.h>

static uint16_t le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

static void parse_touch(const uint8_t *p, ds_input_t *out, int idx)
{
	/* Byte 0: bit 7 = NOT active (inverted!), bits 6-0 = tracking ID */
	out->touch[idx].active = !(p[0] & 0x80);
	out->touch[idx].id = p[0] & 0x7F;

	/* Bytes 1-3: x (12 bits LE) | y (12 bits LE)
	 * x = p[1] | ((p[2] & 0x0F) << 8)
	 * y = ((p[2] & 0xF0) >> 4) | (p[3] << 4) */
	out->touch[idx].x = (uint16_t)(p[1] | ((p[2] & 0x0F) << 8));
	out->touch[idx].y = (uint16_t)(((p[2] & 0xF0) >> 4) | (p[3] << 4));
}

int ds_input_parse(const uint8_t *report, size_t len, ds_input_t *out)
{
	const uint8_t *d;

	memset(out, 0, sizeof(*out));

	if (len >= 78 && report[0] == 0x31) {
		/* BT extended report: data at offset 2 */
		d = report + 2;
	} else if (len >= 64 && report[0] == 0x01) {
		/* USB report: data at offset 1 */
		d = report + 1;
	} else if (len >= 10 && report[0] == 0x01) {
		/* BT simple report (before enhanced mode): minimal data */
		d = report + 1;
		out->lx = d[0];
		out->ly = d[1];
		out->rx = d[2];
		out->ry = d[3];
		out->dpad = d[4] & 0x0F;
		out->l2 = d[7];
		out->r2 = d[8];
		return 0;
	} else {
		return -1;
	}

	/* Sticks */
	out->lx = d[0];
	out->ly = d[1];
	out->rx = d[2];
	out->ry = d[3];

	/* Triggers analog */
	out->l2 = d[4];
	out->r2 = d[5];

	/* Sequence */
	out->seq = d[6];

	/* Buttons byte 0 */
	out->dpad     = d[7] & 0x0F;
	out->square   = !!(d[7] & 0x10);
	out->cross    = !!(d[7] & 0x20);
	out->circle   = !!(d[7] & 0x40);
	out->triangle = !!(d[7] & 0x80);

	/* Buttons byte 1 */
	out->l1       = !!(d[8] & 0x01);
	out->r1       = !!(d[8] & 0x02);
	out->l2_btn   = !!(d[8] & 0x04);
	out->r2_btn   = !!(d[8] & 0x08);
	out->create   = !!(d[8] & 0x10);
	out->options  = !!(d[8] & 0x20);
	out->l3       = !!(d[8] & 0x40);
	out->r3       = !!(d[8] & 0x80);

	/* Buttons byte 2 */
	out->ps          = !!(d[9] & 0x01);
	out->touchpad_btn = !!(d[9] & 0x02);
	out->mute        = !!(d[9] & 0x04);

	/* Timestamp */
	out->timestamp = le32(&d[10]);

	/* Gyroscope (signed 16-bit LE) */
	out->gyro_x = (int16_t)le16(&d[14]);
	out->gyro_y = (int16_t)le16(&d[16]);
	out->gyro_z = (int16_t)le16(&d[18]);

	/* Accelerometer */
	out->accel_x = (int16_t)le16(&d[20]);
	out->accel_y = (int16_t)le16(&d[22]);
	out->accel_z = (int16_t)le16(&d[24]);

	/* Touchpad */
	parse_touch(&d[32], out, 0);
	parse_touch(&d[36], out, 1);

	/* Battery: byte 52 from data start */
	uint8_t batt = d[52];
	uint8_t level = batt & 0x0F;
	uint8_t status = (batt >> 4) & 0x0F;
	out->battery_level = (level > 10) ? 100 : (level * 10);
	out->battery_charging = (status >= 1 && status <= 2);

	return 0;
}
