/*
 * Trigger effect encoding — direct C port of DSX TriggerEffectGenerator.cs.
 *
 * Each effect writes exactly 11 bytes: [mode_id, param0..param9].
 * Zone-based effects (0x21-0x27) use a 16-bit zone bitmask and 3-bit-per-zone
 * force packing into a 32-bit field.
 */

#include "triggers.h"
#include <string.h>
#include <math.h>

static void clear(uint8_t *dst)
{
	memset(dst, 0, DS_TRIGGER_EFFECT_SIZE);
}

bool ds_effect_off(uint8_t *dst)
{
	clear(dst);
	dst[0] = DS_TRIGGER_MODE_OFF;
	return true;
}

bool ds_effect_feedback(uint8_t *dst, uint8_t position, uint8_t strength)
{
	if (position > 9 || strength > 8)
		return false;
	if (strength == 0)
		return ds_effect_off(dst);

	uint8_t force = (strength - 1) & 7;
	uint32_t force_field = 0;
	uint16_t zone_mask = 0;

	for (int i = position; i < 10; i++) {
		force_field |= (uint32_t)force << (3 * i);
		zone_mask |= (uint16_t)(1 << i);
	}

	clear(dst);
	dst[0] = DS_TRIGGER_MODE_FEEDBACK;
	dst[1] = zone_mask & 0xFF;
	dst[2] = (zone_mask >> 8) & 0xFF;
	dst[3] = force_field & 0xFF;
	dst[4] = (force_field >> 8) & 0xFF;
	dst[5] = (force_field >> 16) & 0xFF;
	dst[6] = (force_field >> 24) & 0xFF;
	return true;
}

bool ds_effect_feedback_multi(uint8_t *dst, const uint8_t strength[10])
{
	bool any = false;
	for (int i = 0; i < 10; i++) {
		if (strength[i] > 8)
			return false;
		if (strength[i] > 0)
			any = true;
	}
	if (!any)
		return ds_effect_off(dst);

	uint32_t force_field = 0;
	uint16_t zone_mask = 0;

	for (int i = 0; i < 10; i++) {
		if (strength[i] > 0) {
			uint8_t f = (strength[i] - 1) & 7;
			force_field |= (uint32_t)f << (3 * i);
			zone_mask |= (uint16_t)(1 << i);
		}
	}

	clear(dst);
	dst[0] = DS_TRIGGER_MODE_FEEDBACK;
	dst[1] = zone_mask & 0xFF;
	dst[2] = (zone_mask >> 8) & 0xFF;
	dst[3] = force_field & 0xFF;
	dst[4] = (force_field >> 8) & 0xFF;
	dst[5] = (force_field >> 16) & 0xFF;
	dst[6] = (force_field >> 24) & 0xFF;
	return true;
}

bool ds_effect_slope_feedback(uint8_t *dst, uint8_t start_pos, uint8_t end_pos,
                              uint8_t start_str, uint8_t end_str)
{
	if (start_pos > 8 || end_pos > 9 || end_pos <= start_pos)
		return false;
	if (start_str < 1 || start_str > 8 || end_str < 1 || end_str > 8)
		return false;

	uint8_t strengths[10] = {0};
	float slope = (float)((int)end_str - (int)start_str) / (float)((int)end_pos - (int)start_pos);

	for (int i = start_pos; i < 10; i++) {
		if (i <= end_pos)
			strengths[i] = (uint8_t)roundf(start_str + slope * (i - start_pos));
		else
			strengths[i] = end_str;
	}

	return ds_effect_feedback_multi(dst, strengths);
}

bool ds_effect_weapon(uint8_t *dst, uint8_t start, uint8_t end, uint8_t strength)
{
	if (start < 2 || start > 7)
		return false;
	if (end > 8 || end <= start)
		return false;
	if (strength > 8)
		return false;
	if (strength == 0)
		return ds_effect_off(dst);

	uint16_t zone_mask = (uint16_t)((1 << start) | (1 << end));

	clear(dst);
	dst[0] = DS_TRIGGER_MODE_WEAPON;
	dst[1] = zone_mask & 0xFF;
	dst[2] = (zone_mask >> 8) & 0xFF;
	dst[3] = strength - 1;
	return true;
}

bool ds_effect_vibration(uint8_t *dst, uint8_t position, uint8_t amplitude, uint8_t frequency)
{
	if (position > 9 || amplitude > 8)
		return false;
	if (amplitude == 0 || frequency == 0)
		return ds_effect_off(dst);

	uint8_t amp = (amplitude - 1) & 7;
	uint32_t amp_field = 0;
	uint16_t zone_mask = 0;

	for (int i = position; i < 10; i++) {
		amp_field |= (uint32_t)amp << (3 * i);
		zone_mask |= (uint16_t)(1 << i);
	}

	clear(dst);
	dst[0] = DS_TRIGGER_MODE_VIBRATION;
	dst[1] = zone_mask & 0xFF;
	dst[2] = (zone_mask >> 8) & 0xFF;
	dst[3] = amp_field & 0xFF;
	dst[4] = (amp_field >> 8) & 0xFF;
	dst[5] = (amp_field >> 16) & 0xFF;
	dst[6] = (amp_field >> 24) & 0xFF;
	dst[9] = frequency;
	return true;
}

bool ds_effect_vibration_multi(uint8_t *dst, uint8_t frequency, const uint8_t amplitude[10])
{
	if (frequency == 0)
		return ds_effect_off(dst);

	bool any = false;
	for (int i = 0; i < 10; i++) {
		if (amplitude[i] > 8)
			return false;
		if (amplitude[i] > 0)
			any = true;
	}
	if (!any)
		return ds_effect_off(dst);

	uint32_t amp_field = 0;
	uint16_t zone_mask = 0;

	for (int i = 0; i < 10; i++) {
		if (amplitude[i] > 0) {
			uint8_t a = (amplitude[i] - 1) & 7;
			amp_field |= (uint32_t)a << (3 * i);
			zone_mask |= (uint16_t)(1 << i);
		}
	}

	clear(dst);
	dst[0] = DS_TRIGGER_MODE_VIBRATION;
	dst[1] = zone_mask & 0xFF;
	dst[2] = (zone_mask >> 8) & 0xFF;
	dst[3] = amp_field & 0xFF;
	dst[4] = (amp_field >> 8) & 0xFF;
	dst[5] = (amp_field >> 16) & 0xFF;
	dst[6] = (amp_field >> 24) & 0xFF;
	dst[9] = frequency;
	return true;
}

bool ds_effect_bow(uint8_t *dst, uint8_t start, uint8_t end,
                   uint8_t strength, uint8_t snap_force)
{
	if (start > 8 || end > 8 || start >= end)
		return false;
	if (strength > 8 || snap_force > 8)
		return false;
	if (end == 0 || strength == 0 || snap_force == 0)
		return ds_effect_off(dst);

	uint16_t zone_mask = (uint16_t)((1 << start) | (1 << end));
	uint32_t forces = ((strength - 1) & 7) | (((snap_force - 1) & 7) << 3);

	clear(dst);
	dst[0] = DS_TRIGGER_MODE_BOW;
	dst[1] = zone_mask & 0xFF;
	dst[2] = (zone_mask >> 8) & 0xFF;
	dst[3] = forces & 0xFF;
	dst[4] = (forces >> 8) & 0xFF;
	return true;
}

bool ds_effect_galloping(uint8_t *dst, uint8_t start, uint8_t end,
                         uint8_t first_foot, uint8_t second_foot,
                         uint8_t frequency)
{
	if (start > 8 || end > 9 || start >= end)
		return false;
	if (first_foot > 6 || second_foot > 7 || first_foot >= second_foot)
		return false;
	if (frequency == 0)
		return ds_effect_off(dst);

	uint16_t zone_mask = (uint16_t)((1 << start) | (1 << end));
	uint32_t feet = (second_foot & 7) | ((first_foot & 7) << 3);

	clear(dst);
	dst[0] = DS_TRIGGER_MODE_GALLOPING;
	dst[1] = zone_mask & 0xFF;
	dst[2] = (zone_mask >> 8) & 0xFF;
	dst[3] = feet & 0xFF;
	dst[4] = frequency;
	return true;
}

bool ds_effect_machine(uint8_t *dst, uint8_t start, uint8_t end,
                       uint8_t amp_a, uint8_t amp_b,
                       uint8_t frequency, uint8_t period)
{
	if (start > 8 || end > 9 || end <= start)
		return false;
	if (amp_a > 7 || amp_b > 7)
		return false;
	if (frequency == 0)
		return ds_effect_off(dst);

	uint16_t zone_mask = (uint16_t)((1 << start) | (1 << end));
	uint32_t amps = (amp_a & 7) | ((amp_b & 7) << 3);

	clear(dst);
	dst[0] = DS_TRIGGER_MODE_MACHINE;
	dst[1] = zone_mask & 0xFF;
	dst[2] = (zone_mask >> 8) & 0xFF;
	dst[3] = amps & 0xFF;
	dst[4] = frequency;
	dst[5] = period;
	return true;
}

void ds_effect_raw(uint8_t *dst, uint8_t mode, const uint8_t params[10])
{
	dst[0] = mode;
	memcpy(dst + 1, params, 10);
}
