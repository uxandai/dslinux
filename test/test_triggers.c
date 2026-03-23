/*
 * Unit tests for trigger effect encoding.
 *
 * Verifies byte-level output of each trigger mode matches
 * DSX TriggerEffectGenerator.cs decompiled logic.
 */

#include "triggers.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_BYTES(name, buf, ...) do { \
	tests_run++; \
	const uint8_t expected[] = { __VA_ARGS__ }; \
	if (memcmp(buf, expected, DS_TRIGGER_EFFECT_SIZE) == 0) { \
		tests_passed++; \
		printf("  PASS: %s\n", name); \
	} else { \
		printf("  FAIL: %s\n    got:    ", name); \
		for (int _i = 0; _i < DS_TRIGGER_EFFECT_SIZE; _i++) printf("%02X ", buf[_i]); \
		printf("\n    expect: "); \
		for (int _i = 0; _i < (int)sizeof(expected); _i++) printf("%02X ", expected[_i]); \
		printf("\n"); \
	} \
} while (0)

#define ASSERT_TRUE(name, cond) do { \
	tests_run++; \
	if (cond) { tests_passed++; printf("  PASS: %s\n", name); } \
	else printf("  FAIL: %s\n", name); \
} while (0)

static void test_off(void)
{
	uint8_t buf[11];
	ds_effect_off(buf);
	ASSERT_BYTES("off", buf, 0x05, 0,0,0,0,0,0,0,0,0,0);
}

static void test_weapon(void)
{
	uint8_t buf[11];

	/* Weapon: start=2, end=7, strength=8 */
	bool ok = ds_effect_weapon(buf, 2, 7, 8);
	ASSERT_TRUE("weapon returns true", ok);

	/* Zone mask: (1<<2)|(1<<7) = 0x0084 → LE: 0x84, 0x00 */
	/* Strength: 8-1 = 7 */
	ASSERT_BYTES("weapon(2,7,8)", buf, 0x25, 0x84, 0x00, 0x07, 0,0,0,0,0,0,0);
}

static void test_weapon_validation(void)
{
	uint8_t buf[11];
	memset(buf, 0xFF, sizeof(buf));

	/* start must be 2-7 */
	ASSERT_TRUE("weapon start=0 invalid", !ds_effect_weapon(buf, 0, 5, 8));
	ASSERT_TRUE("weapon start=1 invalid", !ds_effect_weapon(buf, 1, 5, 8));
	ASSERT_TRUE("weapon start=8 invalid", !ds_effect_weapon(buf, 8, 9, 8));

	/* end must be > start and <= 8 */
	ASSERT_TRUE("weapon end<=start invalid", !ds_effect_weapon(buf, 3, 3, 8));
	ASSERT_TRUE("weapon end=9 invalid", !ds_effect_weapon(buf, 2, 9, 8));

	/* strength 0 → off */
	bool ok = ds_effect_weapon(buf, 2, 7, 0);
	ASSERT_TRUE("weapon str=0 → off", ok);
	ASSERT_TRUE("weapon str=0 → mode 0x05", buf[0] == 0x05);
}

static void test_feedback(void)
{
	uint8_t buf[11];

	/* Feedback: position=3, strength=5 */
	ds_effect_feedback(buf, 3, 5);

	/* Zone mask: bits 3-9 set = 0x03F8 → LE: 0xF8, 0x03 */
	/* Force: (5-1)=4 per zone, packed 3 bits each:
	 *   zones 3-9: 4 at positions 3*3=9, 4*3=12, 5*3=15, 6*3=18, 7*3=21, 8*3=24, 9*3=27
	 *   = 4<<9 | 4<<12 | 4<<15 | 4<<18 | 4<<21 | 4<<24 | 4<<27
	 *   = 0x92492 << 9... let me compute:
	 *   Each zone gets value 4 (0b100) at 3-bit offset:
	 *   zone3: 4<<9  = 0x00000800
	 *   zone4: 4<<12 = 0x00004000
	 *   zone5: 4<<15 = 0x00020000
	 *   zone6: 4<<18 = 0x00100000
	 *   zone7: 4<<21 = 0x00800000
	 *   zone8: 4<<24 = 0x04000000
	 *   zone9: 4<<27 = 0x20000000
	 *   total = 0x24924800
	 *   LE bytes: 0x00, 0x48, 0x92, 0x24
	 */
	ASSERT_BYTES("feedback(3,5)", buf,
		0x21,             /* mode */
		0xF8, 0x03,       /* zone mask LE */
		0x00, 0x48, 0x92, 0x24, /* force field LE */
		0, 0, 0, 0);
}

static void test_feedback_position0_strength8(void)
{
	uint8_t buf[11];
	ds_effect_feedback(buf, 0, 8);

	/* Zone mask: all 10 bits = 0x03FF → LE: 0xFF, 0x03 */
	/* Force: (8-1)=7 per zone, 3 bits each:
	 *   zone0: 7<<0  = 0x00000007
	 *   zone1: 7<<3  = 0x00000038
	 *   zone2: 7<<6  = 0x000001C0
	 *   zone3: 7<<9  = 0x00000E00
	 *   zone4: 7<<12 = 0x00007000
	 *   zone5: 7<<15 = 0x00038000
	 *   zone6: 7<<18 = 0x001C0000
	 *   zone7: 7<<21 = 0x00E00000
	 *   zone8: 7<<24 = 0x07000000
	 *   zone9: 7<<27 = 0x38000000
	 *   total = 0x3FFFFFFF
	 *   LE: 0xFF, 0xFF, 0xFF, 0x3F
	 */
	ASSERT_BYTES("feedback(0,8)", buf,
		0x21,
		0xFF, 0x03,
		0xFF, 0xFF, 0xFF, 0x3F,
		0, 0, 0, 0);
}

static void test_vibration(void)
{
	uint8_t buf[11];

	/* Vibration: position=0, amplitude=8, frequency=30 */
	ds_effect_vibration(buf, 0, 8, 30);

	/* Same zone mask and force as feedback(0,8), plus freq at byte 9 */
	ASSERT_BYTES("vibration(0,8,30)", buf,
		0x26,
		0xFF, 0x03,
		0xFF, 0xFF, 0xFF, 0x3F,
		0, 0, 30, 0);
}

static void test_bow(void)
{
	uint8_t buf[11];
	ds_effect_bow(buf, 1, 5, 6, 4);

	/* Zone mask: (1<<1)|(1<<5) = 0x0022 → LE: 0x22, 0x00 */
	/* Forces: (6-1)&7 = 5, (4-1)&7 = 3 → combined = 5 | (3<<3) = 5|24 = 29 = 0x1D */
	ASSERT_BYTES("bow(1,5,6,4)", buf,
		0x22,
		0x22, 0x00,
		0x1D, 0x00,
		0, 0, 0, 0, 0, 0);
}

static void test_galloping(void)
{
	uint8_t buf[11];
	ds_effect_galloping(buf, 0, 8, 2, 5, 20);

	/* Zone mask: (1<<0)|(1<<8) = 0x0101 → LE: 0x01, 0x01 */
	/* Feet: (5&7) | ((2&7)<<3) = 5|16 = 21 = 0x15 */
	ASSERT_BYTES("galloping(0,8,2,5,20)", buf,
		0x23,
		0x01, 0x01,
		0x15, 20,
		0, 0, 0, 0, 0, 0);
}

static void test_machine(void)
{
	uint8_t buf[11];
	ds_effect_machine(buf, 1, 8, 5, 3, 25, 5);

	/* Zone mask: (1<<1)|(1<<8) = 0x0102 → LE: 0x02, 0x01 */
	/* Amps: (5&7) | ((3&7)<<3) = 5|24 = 29 = 0x1D */
	ASSERT_BYTES("machine(1,8,5,3,25,5)", buf,
		0x27,
		0x02, 0x01,
		0x1D, 25, 5,
		0, 0, 0, 0, 0);
}

static void test_raw(void)
{
	uint8_t buf[11];
	uint8_t params[10] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
	ds_effect_raw(buf, 0xFE, params);
	ASSERT_BYTES("raw(0xFE, ...)", buf,
		0xFE, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A);
}

int main(void)
{
	printf("=== Trigger Encoding Tests ===\n");
	test_off();
	test_weapon();
	test_weapon_validation();
	test_feedback();
	test_feedback_position0_strength8();
	test_vibration();
	test_bow();
	test_galloping();
	test_machine();
	test_raw();
	printf("\n%d/%d tests passed\n", tests_passed, tests_run);
	return tests_passed == tests_run ? 0 : 1;
}
