/*
 * ds-haptics-test — standalone test tool for BT audio haptics.
 *
 * Usage:
 *   ds-haptics-test [--device /dev/hidrawN] [--sine <freq>] [--stdin]
 *
 * Modes:
 *   --sine <freq>   Generate a sine wave at given frequency (default: 150 Hz)
 *   --stdin          Read raw PCM from stdin (u8, 2ch, 3000Hz)
 *                    e.g.: pw-cat -p --format=u8 --rate=3000 --channels=2 | ds-haptics-test --stdin
 *
 * PipeWire loopback example:
 *   pw-loopback --capture-props='media.class=Audio/Sink' \
 *               --playback-props='media.class=Audio/Source' &
 *   pw-cat -r --target=<loopback_sink> --format=u8 --rate=3000 --channels=2 | \
 *     ds-haptics-test --stdin
 */

#include "dualsense.h"
#include "haptics.h"

#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

/* Generate one frame (64 bytes) of sine wave, u8 stereo 3kHz */
static void generate_sine_frame(uint8_t *buf, double freq, uint64_t *sample_idx)
{
	for (int i = 0; i < DS_HAPTICS_SAMPLE_SIZE; i += 2) {
		double t = (double)(*sample_idx) / DS_HAPTICS_SAMPLE_RATE;
		double val = sin(2.0 * M_PI * freq * t) * 100.0;  /* amplitude ~100/255 */
		uint8_t u8val = (uint8_t)(128.0 + val);
		buf[i]     = u8val;  /* left */
		buf[i + 1] = u8val;  /* right */
		(*sample_idx)++;
	}
}

/* Read from stdin and feed to haptics */
static void feed_from_stdin(ds_haptics_t *h)
{
	uint8_t buf[DS_HAPTICS_SAMPLE_SIZE];
	while (g_running) {
		size_t n = fread(buf, 1, sizeof(buf), stdin);
		if (n == 0) break;
		ds_haptics_write(h, buf, n);
		/* Small sleep to avoid spinning when stdin is faster than playback */
		usleep(5000);
	}
}

/* Generate sine wave and feed to haptics */
static void feed_sine(ds_haptics_t *h, double freq)
{
	uint8_t buf[DS_HAPTICS_SAMPLE_SIZE];
	uint64_t sample_idx = 0;

	printf("Generating %g Hz sine wave haptics...\n", freq);

	while (g_running) {
		generate_sine_frame(buf, freq, &sample_idx);
		ds_haptics_write(h, buf, sizeof(buf));
		usleep(DS_HAPTICS_INTERVAL_NS / 1000);  /* ~10.67ms */
	}
}

int main(int argc, char **argv)
{
	const char *devpath = NULL;
	double sine_freq = 150.0;
	bool use_stdin = false;

	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc)
			devpath = argv[++i];
		else if (strcmp(argv[i], "--sine") == 0 && i + 1 < argc)
			sine_freq = atof(argv[++i]);
		else if (strcmp(argv[i], "--stdin") == 0)
			use_stdin = true;
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			fprintf(stderr,
				"Usage: %s [options]\n"
				"  -d, --device <path>   hidraw device (auto-detect)\n"
				"  --sine <freq>         Sine wave frequency in Hz (default: 150)\n"
				"  --stdin               Read PCM from stdin (u8, 2ch, 3000Hz)\n"
				"\n"
				"PipeWire example:\n"
				"  pw-cat -p --format=u8 --rate=3000 --channels=2 somefile.wav | %s --stdin\n",
				argv[0], argv[0]);
			return 0;
		}
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	ds_device_t *dev = ds_open(devpath);
	if (!dev) {
		perror("Failed to open DualSense");
		return 1;
	}

	if (ds_connection_type(dev) != DS_BT) {
		fprintf(stderr, "Warning: Audio haptics use report 0x32 which is BT-only.\n"
		                "Over USB, haptics go through the audio device instead.\n"
		                "Continuing anyway...\n");
	}

	printf("DualSense connected via %s\n",
	       ds_connection_type(dev) == DS_BT ? "Bluetooth" : "USB");

	/* We need the raw fd — extract it by using ds_send once and reusing the handle.
	 * Ugly but works: ds_haptics_start needs the fd directly.
	 * TODO: add ds_get_fd() to public API */
	/* For now, re-open the device directly */
	int fd;
	{
		/* Re-detect hidraw path — we need the fd */
		extern int ds_hidraw_open(const char *path, int *fd, ds_conn_t *conn);
		ds_conn_t conn;
		if (ds_hidraw_open(devpath, &fd, &conn) < 0) {
			perror("Failed to open hidraw for haptics");
			ds_close(dev);
			return 1;
		}
	}

	ds_haptics_t *h = ds_haptics_start(fd);
	if (!h) {
		perror("Failed to start haptics stream");
		ds_close(dev);
		return 1;
	}

	printf("Haptics streaming started (3kHz, u8, stereo)\n");

	if (use_stdin) {
		printf("Reading audio from stdin...\n");
		feed_from_stdin(h);
	} else {
		feed_sine(h, sine_freq);
	}

	printf("\nStopping haptics...\n");
	ds_haptics_stop(h);
	close(fd);
	ds_close(dev);
	return 0;
}
