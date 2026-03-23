/*
 * ds-audio-probe — experimental tool to discover BT audio capabilities.
 *
 * Attempts to enable audio modes on the DualSense over BT and monitors
 * input reports for mic audio data.
 *
 * Theory:
 *   1. Starting haptics (report 0x32) may switch controller to "audio mode"
 *   2. Setting AUDIO_CONTROL flags in report 0x31 may enable mic input
 *   3. Input reports may grow from 78 to 547 bytes with audio data
 *   4. Audio data in extended reports may use sub-packet format
 *
 * Usage:
 *   ds-audio-probe [--device /dev/hidrawN]
 *
 * Runs several probes and logs results. Requires BT connection.
 */

#include "dualsense.h"
#include "haptics.h"
#include "crc32.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

/* Open hidraw device directly (we need raw fd access) */
static int open_hidraw(const char *path)
{
	if (path)
		return open(path, O_RDWR);

	/* Auto-detect */
	for (int i = 0; i < 20; i++) {
		char devpath[64];
		snprintf(devpath, sizeof(devpath), "/dev/hidraw%d", i);
		int fd = open(devpath, O_RDWR);
		if (fd < 0) continue;

		struct hidraw_devinfo info;
		if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0 &&
		    info.vendor == 0x054C &&
		    (info.product == 0x0CE6 || info.product == 0x0DF2)) {
			printf("Found DualSense at %s (bus=%d)\n", devpath, info.bustype);
			if (info.bustype != 5) {
				printf("WARNING: Not Bluetooth (bus=%d). Audio probing needs BT.\n",
				       info.bustype);
			}
			return fd;
		}
		close(fd);
	}
	return -1;
}

/* Read one input report with timeout (ms). Returns bytes read or -1. */
static int read_report(int fd, uint8_t *buf, size_t bufsz, int timeout_ms)
{
	fd_set rfds;
	struct timeval tv;
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	tv.tv_sec = timeout_ms / 1000;
	tv.tv_usec = (timeout_ms % 1000) * 1000;

	int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
	if (ret <= 0) return -1;
	return (int)read(fd, buf, bufsz);
}

/* Hex dump first N bytes of a buffer */
static void hexdump(const uint8_t *data, int len, int max)
{
	int n = (len < max) ? len : max;
	for (int i = 0; i < n; i++) {
		printf("%02X ", data[i]);
		if ((i + 1) % 16 == 0) printf("\n    ");
	}
	if (n < len) printf("... (%d more)", len - n);
	printf("\n");
}

/*
 * Send a raw output report with specific audio flags set.
 * Uses report 0x31 (BT) with various flag combinations.
 */
static void send_flags_report(int fd, uint8_t flags0, uint8_t flags1, uint8_t seq)
{
	uint8_t report[78];
	memset(report, 0, sizeof(report));
	report[0] = 0x31;
	report[1] = ((seq & 0x0F) << 4) | 0x02;
	report[2] = flags0;
	report[3] = flags1;

	uint32_t crc = ds_crc32(report, 74);
	report[74] = (uint8_t)(crc);
	report[75] = (uint8_t)(crc >> 8);
	report[76] = (uint8_t)(crc >> 16);
	report[77] = (uint8_t)(crc >> 24);

	write(fd, report, 78);
}

/* ── Probe functions ────────────────────────────────────────────── */

/*
 * Probe 1: Read baseline input reports (no audio active).
 * Log sizes and report IDs.
 */
static void probe_baseline(int fd)
{
	printf("\n=== Probe 1: Baseline input reports ===\n");
	uint8_t buf[1024];
	int sizes[10] = {0};
	int count = 0;

	for (int i = 0; i < 50 && g_running; i++) {
		int n = read_report(fd, buf, sizeof(buf), 100);
		if (n > 0) {
			if (count < 10) sizes[count] = n;
			count++;
			if (count <= 3) {
				printf("  Report #%d: %d bytes, ID=0x%02X\n", count, n, buf[0]);
				printf("    ");
				hexdump(buf, n, 32);
			}
		}
	}
	printf("  Total: %d reports received\n", count);
	if (count > 0) printf("  Typical size: %d bytes\n", sizes[0]);
}

/*
 * Probe 2: Send various audio-related flags in report 0x31
 * and check if input report size changes.
 *
 * Known flags from kernel hid-playstation.c:
 *   flags0 bit 0: HAPTICS_ENABLE
 *   flags0 bit 1: TRIGGERS_ENABLE
 *   flags0 bit 2: AUDIO_CONTROL_ENABLE (?)
 *   flags0 bit 3: MIC_VOLUME_ENABLE (?)
 *   flags0 bit 4: SPEAKER_VOLUME_ENABLE (?)
 */
static void probe_audio_flags(int fd)
{
	printf("\n=== Probe 2: Audio flag combinations ===\n");

	struct {
		uint8_t flags0;
		uint8_t flags1;
		const char *desc;
	} combos[] = {
		{0x03, 0x00, "baseline (haptics+triggers)"},
		{0x07, 0x00, "flags0 bit 2 (possible audio control)"},
		{0x0F, 0x00, "flags0 bits 2-3 (audio+mic?)"},
		{0x1F, 0x00, "flags0 bits 2-4 (audio+mic+speaker?)"},
		{0x3F, 0x00, "flags0 bits 2-5"},
		{0x7F, 0x00, "flags0 bits 2-6"},
		{0xFF, 0x00, "flags0 all bits"},
		{0xFF, 0xFF, "all flags"},
	};

	uint8_t seq = 0;
	uint8_t buf[1024];

	for (int c = 0; c < (int)(sizeof(combos)/sizeof(combos[0])) && g_running; c++) {
		printf("\n  Testing: %s (flags0=0x%02X, flags1=0x%02X)\n",
		       combos[c].desc, combos[c].flags0, combos[c].flags1);

		/* Send the flags */
		for (int i = 0; i < 5; i++) {
			send_flags_report(fd, combos[c].flags0, combos[c].flags1, seq++);
			usleep(10000);
		}

		/* Read a few input reports and check size */
		int max_size = 0;
		int report_count = 0;
		for (int i = 0; i < 20; i++) {
			int n = read_report(fd, buf, sizeof(buf), 50);
			if (n > 0) {
				if (n > max_size) max_size = n;
				report_count++;
			}
		}
		printf("  → Got %d reports, max size: %d bytes\n", report_count, max_size);
		if (max_size > 78) {
			printf("  *** EXTENDED REPORT DETECTED! Size=%d ***\n", max_size);
			printf("  First 64 bytes:\n    ");
			hexdump(buf, max_size, 64);
		}
	}
}

/*
 * Probe 3: Start haptics streaming (report 0x32), then check if
 * input reports change size or format.
 */
static void probe_during_haptics(int fd)
{
	printf("\n=== Probe 3: Input reports during haptics ===\n");

	/* Start haptics */
	ds_haptics_t *h = ds_haptics_start(fd);
	if (!h) {
		printf("  Failed to start haptics\n");
		return;
	}

	/* Feed silence for 1 second to let controller settle */
	uint8_t silence[64];
	memset(silence, 0x80, sizeof(silence));
	for (int i = 0; i < 94; i++) {
		ds_haptics_write(h, silence, sizeof(silence));
		usleep(10000);
	}

	printf("  Haptics streaming active. Reading input reports...\n");

	uint8_t buf[1024];
	int sizes_seen[8] = {0};
	int n_sizes = 0;

	for (int i = 0; i < 100 && g_running; i++) {
		int n = read_report(fd, buf, sizeof(buf), 50);
		if (n > 0) {
			/* Track unique sizes */
			bool seen = false;
			for (int j = 0; j < n_sizes; j++) {
				if (sizes_seen[j] == n) { seen = true; break; }
			}
			if (!seen && n_sizes < 8) {
				sizes_seen[n_sizes++] = n;
				printf("  New report size: %d bytes (ID=0x%02X)\n", n, buf[0]);
				if (n > 78) {
					printf("  *** EXTENDED! Dumping first 80 bytes:\n    ");
					hexdump(buf, n, 80);
				}
			}
		}
	}

	printf("  Unique report sizes seen: ");
	for (int i = 0; i < n_sizes; i++) printf("%d ", sizes_seen[i]);
	printf("\n");

	/* Check if any reports have audio-like data patterns */
	printf("\n  Checking for audio patterns in last report...\n");
	int n = read_report(fd, buf, sizeof(buf), 200);
	if (n > 78) {
		/* Look for non-zero data after normal input report area */
		int nonzero = 0;
		for (int i = 78; i < n; i++)
			if (buf[i] != 0) nonzero++;
		printf("  Bytes 78-%d: %d non-zero out of %d total\n",
		       n, nonzero, n - 78);
		if (nonzero > 10) {
			printf("  *** Possible audio data! ***\n    ");
			hexdump(buf + 78, n - 78, 64);
		}
	}

	ds_haptics_stop(h);
	printf("  Haptics stopped.\n");
}

/*
 * Probe 4: Try reading feature reports that might reveal audio config.
 */
static void probe_feature_reports(int fd)
{
	printf("\n=== Probe 4: Feature reports ===\n");

	/* Known feature report IDs from kernel + DSX */
	int feature_ids[] = {0x05, 0x09, 0x20, 0x22, 0x08, 0x06};
	const char *names[] = {
		"0x05 (calibration)", "0x09 (MAC address)", "0x20 (firmware version)",
		"0x22 (unknown)", "0x08 (unknown)", "0x06 (unknown)"
	};

	for (int i = 0; i < (int)(sizeof(feature_ids)/sizeof(feature_ids[0])); i++) {
		uint8_t buf[256];
		memset(buf, 0, sizeof(buf));
		buf[0] = (uint8_t)feature_ids[i];

		int ret = ioctl(fd, HIDIOCGFEATURE(sizeof(buf)), buf);
		if (ret > 0) {
			printf("  %s: %d bytes\n    ", names[i], ret);
			hexdump(buf, ret, 32);
		} else {
			printf("  %s: not available (errno=%d)\n", names[i], errno);
		}
	}
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	const char *devpath = NULL;

	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc)
			devpath = argv[++i];
		else if (strcmp(argv[i], "--help") == 0) {
			fprintf(stderr,
				"Usage: %s [--device /dev/hidrawN]\n\n"
				"Probes DualSense BT audio capabilities:\n"
				"  1. Baseline input report sizes\n"
				"  2. Audio flag combinations in report 0x31\n"
				"  3. Input reports during haptics streaming\n"
				"  4. Feature report discovery\n"
				"\nRequires BT connection. Results logged to stdout.\n"
				"Redirect to file: %s 2>&1 | tee captures/audio-probe.txt\n",
				argv[0], argv[0]);
			return 0;
		}
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	int fd = open_hidraw(devpath);
	if (fd < 0) {
		fprintf(stderr, "Failed to open DualSense: %s\n", strerror(errno));
		return 1;
	}

	printf("=== DualSense Audio Probe ===\n");
	printf("Date: ");
	time_t now = time(NULL);
	printf("%s", ctime(&now));

	probe_baseline(fd);
	if (g_running) probe_feature_reports(fd);
	if (g_running) probe_audio_flags(fd);
	if (g_running) probe_during_haptics(fd);

	printf("\n=== Probe complete ===\n");
	printf("Save this output: ds-audio-probe 2>&1 | tee captures/audio-probe.txt\n");

	close(fd);
	return 0;
}
