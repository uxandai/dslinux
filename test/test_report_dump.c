/* Quick test: build a BT output report and dump hex. No device needed. */
#include "dualsense.h"
#include "crc32.h"
#include "triggers.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
	uint8_t report[78];
	memset(report, 0, sizeof(report));

	/* BT header */
	report[0] = 0x31;
	report[1] = (0 << 4) | 0x02;  /* seq=0, flags */

	/* flags0: enable haptics + triggers */
	report[2] = 0x03;
	/* flags1: enable lightbar */
	report[3] = 0x04;

	/* Rumble */
	report[4] = 0xFF;  /* right motor */
	report[5] = 0xFF;  /* left motor */

	/* Right trigger: weapon(2,7,8) */
	uint8_t rt[11];
	ds_effect_weapon(rt, 2, 7, 8);
	memcpy(&report[12], rt, 11);

	/* Left trigger: feedback(0,8) */
	uint8_t lt[11];
	ds_effect_feedback(lt, 0, 8);
	memcpy(&report[23], lt, 11);

	/* Lightbar red */
	report[43] = 0x02;  /* brightness full */
	report[46] = 0xFF;  /* R */
	report[47] = 0x00;  /* G */
	report[48] = 0x00;  /* B */

	/* CRC */
	uint32_t crc = ds_crc32(report, 74);
	report[74] = (uint8_t)(crc);
	report[75] = (uint8_t)(crc >> 8);
	report[76] = (uint8_t)(crc >> 16);
	report[77] = (uint8_t)(crc >> 24);

	printf("BT Output Report (78 bytes):\n");
	for (int i = 0; i < 78; i++) {
		printf("%02X ", report[i]);
		if ((i + 1) % 16 == 0) printf("\n");
	}
	printf("\n\nKey offsets:\n");
	printf("  [0]=0x%02X (report ID)\n", report[0]);
	printf("  [1]=0x%02X (seq|flags)\n", report[1]);
	printf("  [2]=0x%02X (flags0)\n", report[2]);
	printf("  [3]=0x%02X (flags1)\n", report[3]);
	printf("  [4]=0x%02X (right motor)\n", report[4]);
	printf("  [5]=0x%02X (left motor)\n", report[5]);
	printf("  [12]=0x%02X (right trigger mode)\n", report[12]);
	printf("  [23]=0x%02X (left trigger mode)\n", report[23]);
	printf("  [46]=0x%02X (lightbar R)\n", report[46]);
	return 0;
}
