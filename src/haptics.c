/*
 * DualSense BT Audio Haptics — Report 0x32 streaming.
 *
 * Based on SAxense (https://github.com/egormanga/SAxense).
 * Uses POSIX timer + signal handler for precise timing,
 * identical to the proven SAxense architecture.
 */

#include "haptics.h"
#include "crc32.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define RING_SIZE (DS_HAPTICS_SAMPLE_RATE * DS_HAPTICS_CHANNELS)

/* ── Report structure (matches SAxense exactly) ─────────────────── */

typedef struct __attribute__((packed)) {
	uint8_t pid    :6;
	uint8_t unk    :1;
	uint8_t sized  :1;
	uint8_t length;
	uint8_t data[];
} subpacket_t;

struct __attribute__((packed)) haptics_report {
	uint8_t report_id;
	union {
		struct __attribute__((packed)) {
			uint8_t tag :4;
			uint8_t seq :4;
			uint8_t data[];
		};
		struct __attribute__((packed)) {
			uint8_t payload[DS_HAPTICS_REPORT_SIZE - sizeof(uint32_t)];
			uint32_t crc;
		};
	};
};

/* ── Haptics stream state ───────────────────────────────────────── */

struct ds_haptics {
	int fd;
	FILE *fp;              /* FILE* wrapping fd for fwrite_unlocked */
	volatile bool running;
	timer_t timerid;

	/* Pointers into report struct (like SAxense's ii/sample) */
	struct haptics_report *report;
	uint8_t *frame_counter;  /* points into pkt11 data[6] */
	uint8_t *sample_buf;     /* points into pkt12 data */

	/* Audio ring buffer */
	pthread_mutex_t ring_lock;
	uint8_t ring[RING_SIZE];
	size_t ring_head;
	size_t ring_tail;
	size_t ring_avail;
};

/* Global pointer for signal handler (SAxense pattern) */
static ds_haptics_t *g_haptics = NULL;

/* ── Ring buffer ────────────────────────────────────────────────── */

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

/* ── CRC32 (SAxense-compatible) ─────────────────────────────────── */

static uint32_t haptics_crc32(const uint8_t *data, size_t size)
{
	uint32_t crc = ~0xEADA2D49;
	while (size--) {
		crc ^= *data++;
		for (unsigned i = 0; i < 8; i++)
			crc = ((crc >> 1) ^ (0xEDB88320 & -(crc & 1)));
	}
	return ~crc;
}

/* ── Signal handler (called by timer, same pattern as SAxense) ── */

static void haptics_tick(int sig)
{
	(void)sig;
	ds_haptics_t *h = g_haptics;
	if (!h || !h->running) return;

	/* Read audio from ring buffer into sample area */
	size_t got = ring_read(h, h->sample_buf, DS_HAPTICS_SAMPLE_SIZE);
	if (got < DS_HAPTICS_SAMPLE_SIZE)
		memset(h->sample_buf + got, 0x80, DS_HAPTICS_SAMPLE_SIZE - got);

	/* Increment frame counter */
	(*h->frame_counter)++;

	/* Compute CRC */
	h->report->crc = haptics_crc32(
		(void *)h->report, 1 + sizeof(h->report->payload));

	/* Write report (fwrite_unlocked like SAxense) */
	fwrite_unlocked(h->report, sizeof(*h->report), 1, h->fp);
}

/* ── Public API ─────────────────────────────────────────────────── */

ds_haptics_t *ds_haptics_start(int hidraw_fd)
{
	ds_haptics_t *h = calloc(1, sizeof(*h));
	if (!h) return NULL;

	h->fd = hidraw_fd;
	h->running = true;
	pthread_mutex_init(&h->ring_lock, NULL);

	/* Wrap fd in FILE* for fwrite_unlocked */
	h->fp = fdopen(dup(hidraw_fd), "wb");
	if (!h->fp) {
		free(h);
		return NULL;
	}
	setbuf(h->fp, NULL);

	/* Build report structure (SAxense style) */
	static const subpacket_t pkt11_template = {
		.pid = 0x11, .sized = 1, .length = 7,
		.data = {0xFE, 0, 0, 0, 0, 0xFF, 0},
	};
	static const subpacket_t pkt12_template = {
		.pid = 0x12, .sized = 1, .length = DS_HAPTICS_SAMPLE_SIZE,
	};

	h->report = calloc(1, sizeof(*h->report));
	h->report->report_id = DS_HAPTICS_REPORT_ID;
	h->report->tag = 0;

	/* Place sub-packets in report data area */
	size_t pkt11_total = sizeof(pkt11_template) + pkt11_template.length;
	subpacket_t *p11 = (void *)(h->report->data + 0);
	subpacket_t *p12 = (void *)(h->report->data + pkt11_total);

	memcpy(p11, &pkt11_template, pkt11_total);
	memcpy(p12, &pkt12_template, sizeof(pkt12_template));

	/* Set up pointers (same as SAxense's ii and sample) */
	h->frame_counter = &p11->data[6];
	h->sample_buf = p12->data;

	/* Fill sample with silence */
	memset(h->sample_buf, 0x80, DS_HAPTICS_SAMPLE_SIZE);

	mlockall(MCL_CURRENT | MCL_FUTURE);

	/* Set global for signal handler */
	g_haptics = h;

	/* Create POSIX timer with SIGRTMIN (identical to SAxense) */
	struct sigevent se = {0};
	se.sigev_notify = SIGEV_SIGNAL;
	se.sigev_signo = SIGRTMIN;

	signal(SIGRTMIN, haptics_tick);

	if (timer_create(CLOCK_MONOTONIC, &se, &h->timerid) < 0) {
		fclose(h->fp);
		free(h->report);
		free(h);
		g_haptics = NULL;
		return NULL;
	}

	struct itimerspec ts = {0};
	ts.it_interval.tv_nsec = DS_HAPTICS_INTERVAL_NS;
	ts.it_value.tv_nsec = 1;  /* start immediately */

	timer_settime(h->timerid, 0, &ts, NULL);

	return h;
}

void ds_haptics_stop(ds_haptics_t *h)
{
	if (!h) return;

	h->running = false;

	/* Stop timer */
	struct itimerspec ts = {0};
	timer_settime(h->timerid, 0, &ts, NULL);
	timer_delete(h->timerid);

	g_haptics = NULL;
	fclose(h->fp);
	free(h->report);
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
			h->ring_tail = (h->ring_tail + 1) % RING_SIZE;
	}
	pthread_mutex_unlock(&h->ring_lock);
}

bool ds_haptics_running(const ds_haptics_t *h)
{
	return h && h->running;
}
