#ifndef DS_HAPTICS_H
#define DS_HAPTICS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * DualSense BT Audio Haptics — Report 0x32
 *
 * Protocol discovered by SAxense (Sdore, 2025) via cleanroom RE.
 * Reference: https://github.com/egormanga/SAxense
 *
 * The controller accepts raw PCM audio via HID report 0x32 (141 bytes)
 * at 3000 Hz sample rate, 8-bit, 2 channels.  The audio drives the
 * VCM linear resonant actuators for HD haptic feedback.
 *
 * Report 0x32 structure (141 bytes):
 *   [0]       Report ID = 0x32
 *   [1]       tag(4 bits) | seq(4 bits)
 *   [2..10]   Sub-packet 0x11 (config, 9 bytes total)
 *   [11..76]  Sub-packet 0x12 (audio, 2+64 bytes)
 *   [77..136] Padding (zeros)
 *   [137..140] CRC32 (same seed 0xA2 as report 0x31)
 *
 * Sub-packet format:
 *   byte 0: pid(6 bits) | unk(1 bit) | sized(1 bit)
 *   byte 1: length (if sized=1)
 *   [2..]: data[length]
 *
 * Sub-packet 0x11 (config):
 *   data[0] = 0xFE (flags: enable audio)
 *   data[1..5] = 0
 *   data[6] = frame counter (increments each report)
 *   data[7] = 0 (was: 0xFF in SAxense, testing shows 0 works too)
 *
 * Sub-packet 0x12 (audio samples):
 *   data[0..63] = 64 bytes of raw PCM
 *   Format: 3000 Hz, unsigned 8-bit, 2 channels (interleaved L/R)
 *   Each frame = 32 stereo samples = 64 bytes
 */

#define DS_HAPTICS_REPORT_ID     0x32
#define DS_HAPTICS_REPORT_SIZE   141
#define DS_HAPTICS_SAMPLE_SIZE   64    /* bytes per audio frame */
#define DS_HAPTICS_SAMPLE_RATE   3000  /* Hz */
#define DS_HAPTICS_CHANNELS      2
#define DS_HAPTICS_BITS          8

/* Interval between haptic reports in nanoseconds */
/* 64 bytes / (3000 Hz * 2 channels) = ~10.67ms per frame */
#define DS_HAPTICS_INTERVAL_NS   (1000000000ULL * DS_HAPTICS_SAMPLE_SIZE / (DS_HAPTICS_SAMPLE_RATE * DS_HAPTICS_CHANNELS))

/*
 * Opaque haptics stream handle.
 * Manages the real-time audio → HID report pipeline.
 */
typedef struct ds_haptics ds_haptics_t;

/*
 * Start haptics streaming on a BT-connected DualSense.
 * @param hidraw_fd   File descriptor for the hidraw device.
 * @return Handle, or NULL on failure.
 *
 * Creates a background thread that sends report 0x32 at ~94 Hz.
 * Feed audio with ds_haptics_write().  The stream is silent until
 * audio data is provided.
 */
ds_haptics_t *ds_haptics_start(int hidraw_fd);

/*
 * Stop haptics streaming and free resources.
 */
void ds_haptics_stop(ds_haptics_t *h);

/*
 * Feed audio samples to the haptics stream.
 * @param data  PCM audio: unsigned 8-bit, 2ch interleaved, 3000 Hz.
 * @param len   Number of bytes (should be multiples of 64).
 *
 * Thread-safe.  Data is buffered internally; excess data is dropped.
 * If no data is fed, silence is sent.
 */
void ds_haptics_write(ds_haptics_t *h, const uint8_t *data, size_t len);

/*
 * Check if haptics stream is running.
 */
bool ds_haptics_running(const ds_haptics_t *h);

#endif /* DS_HAPTICS_H */
