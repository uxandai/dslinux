#ifndef DS_CRC32_H
#define DS_CRC32_H

#include <stddef.h>
#include <stdint.h>

/*
 * Compute CRC32 for a DualSense BT output report.
 *
 * Uses the standard CRC32 polynomial (0xEDB88320) with a pre-computed
 * seed of 0xEB1C1A49, which is CRC32(0xFFFFFFFF, 0xA2).  The 0xA2 byte
 * is the BT HID transaction header for OUTPUT reports.
 *
 * @param data  Report bytes (including report ID, excluding CRC slot).
 * @param len   Number of bytes to hash (typically 74 for a 78-byte report).
 * @return      CRC32 value to store little-endian in the last 4 bytes.
 */
uint32_t ds_crc32(const uint8_t *data, size_t len);

#endif /* DS_CRC32_H */
