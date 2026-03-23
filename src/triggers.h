#ifndef DS_TRIGGERS_H
#define DS_TRIGGERS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Trigger effect mode IDs — from DSX TriggerEffectGenerator.cs and Nielk1 gist.
 * Each effect writes 11 bytes: [mode, param0..param9].
 */
#define DS_TRIGGER_MODE_OFF               0x05
#define DS_TRIGGER_MODE_FEEDBACK          0x21
#define DS_TRIGGER_MODE_WEAPON            0x25
#define DS_TRIGGER_MODE_VIBRATION         0x26
#define DS_TRIGGER_MODE_BOW               0x22
#define DS_TRIGGER_MODE_GALLOPING         0x23
#define DS_TRIGGER_MODE_MACHINE           0x27
#define DS_TRIGGER_MODE_SIMPLE_FEEDBACK   0x01
#define DS_TRIGGER_MODE_SIMPLE_WEAPON     0x02
#define DS_TRIGGER_MODE_SIMPLE_VIBRATION  0x06
#define DS_TRIGGER_MODE_LIMITED_FEEDBACK  0x11
#define DS_TRIGGER_MODE_LIMITED_WEAPON    0x12

/* Size of one trigger effect packet (mode + 10 params) */
#define DS_TRIGGER_EFFECT_SIZE 11

/*
 * All functions write exactly 11 bytes starting at dst.
 * Return true on success, false on invalid parameters (dst untouched on false).
 */

bool ds_effect_off(uint8_t *dst);

bool ds_effect_feedback(uint8_t *dst, uint8_t position, uint8_t strength);
bool ds_effect_feedback_multi(uint8_t *dst, const uint8_t strength[10]);
bool ds_effect_slope_feedback(uint8_t *dst, uint8_t start_pos, uint8_t end_pos,
                              uint8_t start_str, uint8_t end_str);

bool ds_effect_weapon(uint8_t *dst, uint8_t start, uint8_t end, uint8_t strength);

bool ds_effect_vibration(uint8_t *dst, uint8_t position, uint8_t amplitude, uint8_t frequency);
bool ds_effect_vibration_multi(uint8_t *dst, uint8_t frequency, const uint8_t amplitude[10]);

bool ds_effect_bow(uint8_t *dst, uint8_t start, uint8_t end,
                   uint8_t strength, uint8_t snap_force);

bool ds_effect_galloping(uint8_t *dst, uint8_t start, uint8_t end,
                         uint8_t first_foot, uint8_t second_foot,
                         uint8_t frequency);

bool ds_effect_machine(uint8_t *dst, uint8_t start, uint8_t end,
                       uint8_t amp_a, uint8_t amp_b,
                       uint8_t frequency, uint8_t period);

/* Write raw mode + 10 param bytes. */
void ds_effect_raw(uint8_t *dst, uint8_t mode, const uint8_t params[10]);

#endif /* DS_TRIGGERS_H */
