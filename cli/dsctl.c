/*
 * dsctl — CLI tool for DualSense adaptive triggers, rumble, and LEDs.
 *
 * Usage:
 *   dsctl [--device /dev/hidrawN] <command> [args...]
 *
 * Commands:
 *   trigger <left|right> off
 *   trigger <left|right> feedback <position:0-9> <strength:1-8>
 *   trigger <left|right> weapon <start:2-7> <end:3-8> <strength:1-8>
 *   trigger <left|right> vibration <position:0-9> <amplitude:1-8> <freq:1-255>
 *   trigger <left|right> bow <start> <end> <strength> <snap>
 *   trigger <left|right> galloping <start> <end> <foot1> <foot2> <freq>
 *   trigger <left|right> machine <start> <end> <ampA> <ampB> <freq> <period>
 *   rumble <left:0-255> <right:0-255>
 *   lightbar <RRGGBB hex>
 *   player-leds <mask:0-31>
 *   mute-led <off|on|pulse>
 *   info
 */

#include "dualsense.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [--device <path>] <command> [args...]\n"
		"\n"
		"Commands:\n"
		"  trigger <left|right> off\n"
		"  trigger <left|right> feedback <pos:0-9> <str:1-8>\n"
		"  trigger <left|right> weapon <start:2-7> <end:3-8> <str:1-8>\n"
		"  trigger <left|right> vibration <pos:0-9> <amp:1-8> <freq:1-255>\n"
		"  trigger <left|right> bow <start> <end> <str> <snap>\n"
		"  trigger <left|right> galloping <start> <end> <foot1> <foot2> <freq>\n"
		"  trigger <left|right> machine <start> <end> <ampA> <ampB> <freq> <period>\n"
		"  rumble <left:0-255> <right:0-255>\n"
		"  lightbar <RRGGBB>\n"
		"  player-leds <mask:0-31>\n"
		"  mute-led <off|on|pulse>\n"
		"  info\n",
		prog);
}

static ds_trigger_t parse_side(const char *s)
{
	if (strcasecmp(s, "left") == 0 || strcasecmp(s, "l") == 0)
		return DS_TRIGGER_LEFT;
	return DS_TRIGGER_RIGHT;
}

static uint8_t parse_u8(const char *s)
{
	return (uint8_t)strtoul(s, NULL, 0);
}

static int cmd_trigger(ds_device_t *dev, int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "trigger: need <side> <mode> [params...]\n");
		return 1;
	}

	ds_trigger_t side = parse_side(argv[0]);
	const char *mode = argv[1];

	if (strcasecmp(mode, "off") == 0) {
		ds_trigger_off(dev, side);
	} else if (strcasecmp(mode, "feedback") == 0) {
		if (argc < 4) { fprintf(stderr, "feedback: need <pos> <str>\n"); return 1; }
		ds_trigger_feedback(dev, side, parse_u8(argv[2]), parse_u8(argv[3]));
	} else if (strcasecmp(mode, "weapon") == 0) {
		if (argc < 5) { fprintf(stderr, "weapon: need <start> <end> <str>\n"); return 1; }
		ds_trigger_weapon(dev, side, parse_u8(argv[2]), parse_u8(argv[3]), parse_u8(argv[4]));
	} else if (strcasecmp(mode, "vibration") == 0) {
		if (argc < 5) { fprintf(stderr, "vibration: need <pos> <amp> <freq>\n"); return 1; }
		ds_trigger_vibration(dev, side, parse_u8(argv[2]), parse_u8(argv[3]), parse_u8(argv[4]));
	} else if (strcasecmp(mode, "bow") == 0) {
		if (argc < 6) { fprintf(stderr, "bow: need <start> <end> <str> <snap>\n"); return 1; }
		ds_trigger_bow(dev, side, parse_u8(argv[2]), parse_u8(argv[3]),
		               parse_u8(argv[4]), parse_u8(argv[5]));
	} else if (strcasecmp(mode, "galloping") == 0) {
		if (argc < 7) { fprintf(stderr, "galloping: need <start> <end> <f1> <f2> <freq>\n"); return 1; }
		ds_trigger_galloping(dev, side, parse_u8(argv[2]), parse_u8(argv[3]),
		                     parse_u8(argv[4]), parse_u8(argv[5]), parse_u8(argv[6]));
	} else if (strcasecmp(mode, "machine") == 0) {
		if (argc < 8) { fprintf(stderr, "machine: need <start> <end> <aA> <aB> <freq> <per>\n"); return 1; }
		ds_trigger_machine(dev, side, parse_u8(argv[2]), parse_u8(argv[3]),
		                   parse_u8(argv[4]), parse_u8(argv[5]),
		                   parse_u8(argv[6]), parse_u8(argv[7]));
	} else {
		fprintf(stderr, "Unknown trigger mode: %s\n", mode);
		return 1;
	}

	return ds_send(dev);
}

static int cmd_rumble(ds_device_t *dev, int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "rumble: need <left:0-255> <right:0-255>\n");
		return 1;
	}
	ds_rumble(dev, parse_u8(argv[0]), parse_u8(argv[1]));
	return ds_send(dev);
}

static int cmd_lightbar(ds_device_t *dev, int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "lightbar: need <RRGGBB hex>\n");
		return 1;
	}
	unsigned int rgb;
	if (sscanf(argv[0], "%x", &rgb) != 1) {
		fprintf(stderr, "lightbar: invalid hex color '%s'\n", argv[0]);
		return 1;
	}
	ds_lightbar(dev, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	return ds_send(dev);
}

static int cmd_player_leds(ds_device_t *dev, int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "player-leds: need <mask:0-31>\n");
		return 1;
	}
	ds_player_leds(dev, parse_u8(argv[0]));
	return ds_send(dev);
}

static int cmd_mute_led(ds_device_t *dev, int argc, char **argv)
{
	if (argc < 1) {
		fprintf(stderr, "mute-led: need <off|on|pulse>\n");
		return 1;
	}
	ds_mute_led_t mode = DS_MUTE_OFF;
	if (strcasecmp(argv[0], "on") == 0) mode = DS_MUTE_ON;
	else if (strcasecmp(argv[0], "pulse") == 0) mode = DS_MUTE_PULSE;
	ds_mute_led(dev, mode);
	return ds_send(dev);
}

static int cmd_info(ds_device_t *dev)
{
	printf("Connection: %s\n", ds_connection_type(dev) == DS_BT ? "Bluetooth" : "USB");
	return 0;
}

int main(int argc, char **argv)
{
	const char *devpath = NULL;
	int argi = 1;

	/* Parse global options */
	while (argi < argc && argv[argi][0] == '-') {
		if (strcmp(argv[argi], "--device") == 0 || strcmp(argv[argi], "-d") == 0) {
			if (argi + 1 >= argc) {
				fprintf(stderr, "--device requires a path\n");
				return 1;
			}
			devpath = argv[++argi];
		} else if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
			usage(argv[0]);
			return 0;
		} else {
			fprintf(stderr, "Unknown option: %s\n", argv[argi]);
			return 1;
		}
		argi++;
	}

	if (argi >= argc) {
		usage(argv[0]);
		return 1;
	}

	const char *cmd = argv[argi++];
	int remaining = argc - argi;
	char **rest = argv + argi;

	ds_device_t *dev = ds_open(devpath);
	if (!dev) {
		perror("Failed to open DualSense controller");
		fprintf(stderr, "Make sure the controller is connected and you have permissions.\n"
			"Try: sudo cp udev/99-dualsense.rules /etc/udev/rules.d/ && sudo udevadm control --reload\n");
		return 1;
	}

	int ret;
	if (strcasecmp(cmd, "trigger") == 0)
		ret = cmd_trigger(dev, remaining, rest);
	else if (strcasecmp(cmd, "rumble") == 0)
		ret = cmd_rumble(dev, remaining, rest);
	else if (strcasecmp(cmd, "lightbar") == 0)
		ret = cmd_lightbar(dev, remaining, rest);
	else if (strcasecmp(cmd, "player-leds") == 0)
		ret = cmd_player_leds(dev, remaining, rest);
	else if (strcasecmp(cmd, "mute-led") == 0)
		ret = cmd_mute_led(dev, remaining, rest);
	else if (strcasecmp(cmd, "info") == 0)
		ret = cmd_info(dev);
	else {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		usage(argv[0]);
		ret = 1;
	}

	if (ret < 0)
		fprintf(stderr, "Error sending report: %s\n", strerror(-ret));

	ds_close(dev);
	return ret ? 1 : 0;
}
