/*
 * DualSense BT Audio Haptics — Report 0x32 streaming.
 *
 * Based on protocol discovered by SAxense (https://github.com/egormanga/SAxense).
 * The DualSense accepts raw 3kHz 8-bit stereo PCM via HID report 0x32,
 * driving the VCM linear resonant actuators for HD haptic feedback.
 *
 * This module runs a real-time thread that sends audio frames at ~94 Hz
 * (one 64-byte frame every ~10.67ms).  Audio is fed via ds_haptics_write()
 * from any source (PipeWire capture, file playback, sine generator, etc).
 */

#include "haptics.h"
#include "crc32.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Ring buffer for audio samples.  ~1 second at 3kHz stereo 8-bit. */
#define RING_SIZE (DS_HAPTICS_SAMPLE_RATE * DS_HAPTICS_CHANNELS)  /* 6000 bytes */

/* ── Sub-packet encoding ────────────────────────────────────────── */

/*
 * Sub-packet header format:
 *   bit 0:    sized (1 = length field present)
 *   bit 1:    unk
 *   bits 2-7: pid (packet type ID)
 */
static inline uint8_t subpacket_header(uint8_t pid, bool sized)
{
	return (uint8_t)((pid << 2) | (sized ? 0x01 : 0x00));
}

/* Sub-packet 0x11: config/flags (9 bytes total: header + length + 7 data) */
#define PKT11_OFFSET  2   /* offset in report */
#define PKT11_SIZE    9
/* Sub-packet 0x12: audio (2 + 64 bytes: header + length + samples) */
#define PKT12_OFFSET  (PKT11_OFFSET + PKT11_SIZE)  /* = 11 */
#define PKT12_SIZE    (2 + DS_HAPTICS_SAMPLE_SIZE)  /* = 66 */

/* CRC covers bytes [0..136], stored at [137..140] */
#define CRC_OFFSET    (DS_HAPTICS_REPORT_SIZE - 4)  /* = 137 */

/* ── Haptics stream state ───────────────────────────────────────── */

struct ds_haptics {
	int fd;                  /* hidraw fd (not owned, do not close) */
	pthread_t thread;
	volatile bool running;

	/* Audio ring buffer */
	pthread_mutex_t ring_lock;
	uint8_t ring[RING_SIZE];
	size_t ring_head;        /* write position */
	size_t ring_tail;        /* read position */
	size_t ring_avail;       /* bytes available to read */

	/* Report state */
	uint8_t seq;             /* sequence counter (upper nibble) */
	uint8_t frame_counter;   /* config sub-packet frame counter */
};

/* ── Ring buffer ops ────────────────────────────────────────────── */

static size_t ring_read(ds_haptics_t *h, uint8_t *out, size_t len)
{
	pthread_mutex_lock(&h->ring_lock);

	size_t to_read = (len < h->ring_avail) ? len : h->ring_avail;
	for (size_t i = 0; i < to_read; i++) {
		out[i] = h->ring[h->ring_tail];
		h->ring_tail = (h->ring_tail + 1) % RING_SIZE;
	}
	h->ring_avail -= to_read;

	pthread_mutex_unlock(&h->ring_lock);
	return to_read;
}

/* ── Build and send one haptics report ──────────────────────────── */

static void send_haptics_report(ds_haptics_t *h)
{
	uint8_t report[DS_HAPTICS_REPORT_SIZE];
	memset(report, 0, sizeof(report));

	/* Header */
	report[0] = DS_HAPTICS_REPORT_ID;  /* 0x32 */
	report[1] = (h->seq & 0x0F) << 4;  /* seq in upper nibble, tag=0 */
	h->seq = (h->seq + 1) & 0x0F;

	/* Sub-packet 0x11: config */
	report[PKT11_OFFSET + 0] = subpacket_header(0x11, true);
	report[PKT11_OFFSET + 1] = 7;  /* length */
	report[PKT11_OFFSET + 2] = 0xFE;  /* flags: enable audio output */
	report[PKT11_OFFSET + 3] = 0;
	report[PKT11_OFFSET + 4] = 0;
	report[PKT11_OFFSET + 5] = 0;
	report[PKT11_OFFSET + 6] = 0;
	report[PKT11_OFFSET + 7] = 0xFF;
	report[PKT11_OFFSET + 8] = h->frame_counter++;

	/* Sub-packet 0x12: audio samples */
	report[PKT12_OFFSET + 0] = subpacket_header(0x12, true);
	report[PKT12_OFFSET + 1] = DS_HAPTICS_SAMPLE_SIZE;

	/* Read audio from ring buffer, or fill with silence (0x80 = zero for u8) */
	uint8_t samples[DS_HAPTICS_SAMPLE_SIZE];
	size_t got = ring_read(h, samples, DS_HAPTICS_SAMPLE_SIZE);
	if (got < DS_HAPTICS_SAMPLE_SIZE) {
		/* Fill remainder with silence (0x80 = center for unsigned 8-bit) */
		memset(samples + got, 0x80, DS_HAPTICS_SAMPLE_SIZE - got);
	}
	memcpy(&report[PKT12_OFFSET + 2], samples, DS_HAPTICS_SAMPLE_SIZE);

	/* CRC32 over bytes [0..136] */
	uint32_t crc = ds_crc32(report, CRC_OFFSET);
	report[CRC_OFFSET + 0] = (uint8_t)(crc);
	report[CRC_OFFSET + 1] = (uint8_t)(crc >> 8);
	report[CRC_OFFSET + 2] = (uint8_t)(crc >> 16);
	report[CRC_OFFSET + 3] = (uint8_t)(crc >> 24);

	/* Write to hidraw */
	ssize_t ret = write(h->fd, report, sizeof(report));
	if (ret < 0 && errno != EAGAIN) {
		perror("haptics write");
		h->running = false;
	}
}

/* ── Real-time streaming thread ─────────────────────────────────── */

static void *haptics_thread(void *arg)
{
	ds_haptics_t *h = arg;

	struct timespec next;
	clock_gettime(CLOCK_MONOTONIC, &next);

	while (h->running) {
		send_haptics_report(h);

		/* Advance to next frame time */
		next.tv_nsec += DS_HAPTICS_INTERVAL_NS;
		while (next.tv_nsec >= 1000000000L) {
			next.tv_nsec -= 1000000000L;
			next.tv_sec++;
		}
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
	}

	return NULL;
}

/* ── Public API ─────────────────────────────────────────────────── */

ds_haptics_t *ds_haptics_start(int hidraw_fd)
{
	ds_haptics_t *h = calloc(1, sizeof(*h));
	if (!h) return NULL;

	h->fd = hidraw_fd;
	h->running = true;
	pthread_mutex_init(&h->ring_lock, NULL);

	/* Fill ring with silence */
	memset(h->ring, 0x80, RING_SIZE);

	if (pthread_create(&h->thread, NULL, haptics_thread, h) != 0) {
		free(h);
		return NULL;
	}

	/* Set thread to real-time priority if possible */
	struct sched_param param = { .sched_priority = 50 };
	pthread_setschedparam(h->thread, SCHED_FIFO, &param);

	return h;
}

void ds_haptics_stop(ds_haptics_t *h)
{
	if (!h) return;

	h->running = false;
	pthread_join(h->thread, NULL);
	pthread_mutex_destroy(&h->ring_lock);
	free(h);
}

void ds_haptics_write(ds_haptics_t *h, const uint8_t *data, size_t len)
{
	if (!h || !data || len == 0) return;

	pthread_mutex_lock(&h->ring_lock);

	for (size_t i = 0; i < len; i++) {
		h->ring[h->ring_head] = data[i];
		h->ring_head = (h->ring_head + 1) % RING_SIZE;
		if (h->ring_avail < RING_SIZE)
			h->ring_avail++;
		else
			h->ring_tail = (h->ring_tail + 1) % RING_SIZE;  /* drop oldest */
	}

	pthread_mutex_unlock(&h->ring_lock);
}

bool ds_haptics_running(const ds_haptics_t *h)
{
	return h && h->running;
}
