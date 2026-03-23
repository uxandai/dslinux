/*
 * dualsensed — daemon for DualSense controller management.
 *
 * Two interfaces:
 *
 * 1) Unix socket (native protocol) — JSON lines:
 *    {"cmd":"trigger","side":"R","mode":"weapon","start":2,"end":7,"strength":8}
 *
 * 2) UDP port 6969 (DSX-compatible) — game mods send DSX packets:
 *    {"Instructions":[{"Type":1,"Parameters":[0,2,22,40,160,8]}]}
 *
 *    This lets existing DSX mods (running in Proton/Wine) control the
 *    controller without modification.
 */

#include "dualsense.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS          8
#define MAX_DEVICES          4
#define BUF_SIZE             4096
#define DSX_DEFAULT_PORT     6969
#define DSX_MOD_TIMEOUT_SEC  60    /* reset after 60s of no UDP messages */

/* ── Multi-device state ─────────────────────────────────────────── */

static ds_device_t *g_devices[MAX_DEVICES];
static int g_ndevices = 0;

/* Get device by index (0-based), or first device if idx is -1 or 0 */
static ds_device_t *get_device(int idx)
{
	if (g_ndevices == 0) return NULL;
	if (idx < 0 || idx >= g_ndevices) idx = 0;
	return g_devices[idx];
}

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

/* ── Minimal JSON parser ────────────────────────────────────────── */

static const char *json_find_str(const char *json, const char *key, char *out, size_t out_sz)
{
	char needle[128];
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	const char *p = strstr(json, needle);
	if (!p) return NULL;
	p += strlen(needle);
	while (*p == ' ' || *p == ':') p++;
	if (*p != '"') return NULL;
	p++;
	size_t i = 0;
	while (*p && *p != '"' && i < out_sz - 1)
		out[i++] = *p++;
	out[i] = '\0';
	return out;
}

static bool json_find_int(const char *json, const char *key, int *out)
{
	char needle[128];
	snprintf(needle, sizeof(needle), "\"%s\"", key);
	const char *p = strstr(json, needle);
	if (!p) return false;
	p += strlen(needle);
	while (*p == ' ' || *p == ':') p++;
	if (*p != '-' && (*p < '0' || *p > '9')) return false;
	*out = (int)strtol(p, NULL, 10);
	return true;
}

/*
 * Parse a JSON array of ints: [0, 2, 22, 40, 160, 8]
 * Returns number of values parsed.
 */
static int json_parse_int_array(const char *start, int *out, int max_out)
{
	const char *p = start;
	while (*p && *p != '[') p++;
	if (!*p) return 0;
	p++; /* skip '[' */

	int count = 0;
	while (*p && *p != ']' && count < max_out) {
		while (*p == ' ' || *p == ',') p++;
		if (*p == ']') break;
		if (*p == '-' || (*p >= '0' && *p <= '9')) {
			out[count++] = (int)strtol(p, (char **)&p, 10);
		} else {
			/* skip non-numeric (e.g. true/false/string) */
			while (*p && *p != ',' && *p != ']') p++;
		}
	}
	return count;
}

/* ── Native protocol handlers ───────────────────────────────────── */

static int handle_trigger(ds_device_t *dev, const char *json)
{
	char side_str[16], mode_str[32];
	if (!json_find_str(json, "side", side_str, sizeof(side_str)))
		return -1;
	if (!json_find_str(json, "mode", mode_str, sizeof(mode_str)))
		return -1;

	ds_trigger_t side = (side_str[0] == 'L' || side_str[0] == 'l')
	                    ? DS_TRIGGER_LEFT : DS_TRIGGER_RIGHT;

	if (strcasecmp(mode_str, "off") == 0) {
		ds_trigger_off(dev, side);
	} else if (strcasecmp(mode_str, "feedback") == 0) {
		int pos = 0, str = 0;
		json_find_int(json, "position", &pos);
		json_find_int(json, "strength", &str);
		ds_trigger_feedback(dev, side, (uint8_t)pos, (uint8_t)str);
	} else if (strcasecmp(mode_str, "weapon") == 0) {
		int start = 0, end = 0, str = 0;
		json_find_int(json, "start", &start);
		json_find_int(json, "end", &end);
		json_find_int(json, "strength", &str);
		ds_trigger_weapon(dev, side, (uint8_t)start, (uint8_t)end, (uint8_t)str);
	} else if (strcasecmp(mode_str, "vibration") == 0) {
		int pos = 0, amp = 0, freq = 0;
		json_find_int(json, "position", &pos);
		json_find_int(json, "amplitude", &amp);
		json_find_int(json, "frequency", &freq);
		ds_trigger_vibration(dev, side, (uint8_t)pos, (uint8_t)amp, (uint8_t)freq);
	} else if (strcasecmp(mode_str, "bow") == 0) {
		int start = 0, end = 0, str = 0, snap = 0;
		json_find_int(json, "start", &start);
		json_find_int(json, "end", &end);
		json_find_int(json, "strength", &str);
		json_find_int(json, "snap", &snap);
		ds_trigger_bow(dev, side, (uint8_t)start, (uint8_t)end,
		               (uint8_t)str, (uint8_t)snap);
	} else if (strcasecmp(mode_str, "galloping") == 0) {
		int start = 0, end = 0, f1 = 0, f2 = 0, freq = 0;
		json_find_int(json, "start", &start);
		json_find_int(json, "end", &end);
		json_find_int(json, "first_foot", &f1);
		json_find_int(json, "second_foot", &f2);
		json_find_int(json, "frequency", &freq);
		ds_trigger_galloping(dev, side, (uint8_t)start, (uint8_t)end,
		                     (uint8_t)f1, (uint8_t)f2, (uint8_t)freq);
	} else if (strcasecmp(mode_str, "machine") == 0) {
		int start = 0, end = 0, aa = 0, ab = 0, freq = 0, per = 0;
		json_find_int(json, "start", &start);
		json_find_int(json, "end", &end);
		json_find_int(json, "amp_a", &aa);
		json_find_int(json, "amp_b", &ab);
		json_find_int(json, "frequency", &freq);
		json_find_int(json, "period", &per);
		ds_trigger_machine(dev, side, (uint8_t)start, (uint8_t)end,
		                   (uint8_t)aa, (uint8_t)ab, (uint8_t)freq, (uint8_t)per);
	} else {
		return -1;
	}
	return 0;
}

static int handle_rumble(ds_device_t *dev, const char *json)
{
	int left = 0, right = 0;
	json_find_int(json, "left", &left);
	json_find_int(json, "right", &right);
	ds_rumble(dev, (uint8_t)left, (uint8_t)right);
	return 0;
}

static int handle_lightbar(ds_device_t *dev, const char *json)
{
	int r = 0, g = 0, b = 0;
	json_find_int(json, "r", &r);
	json_find_int(json, "g", &g);
	json_find_int(json, "b", &b);
	ds_lightbar(dev, (uint8_t)r, (uint8_t)g, (uint8_t)b);
	return 0;
}

static int handle_player_leds(ds_device_t *dev, const char *json)
{
	int mask = 0;
	json_find_int(json, "mask", &mask);
	ds_player_leds(dev, (uint8_t)mask);
	return 0;
}

static int handle_mute_led(ds_device_t *dev, const char *json)
{
	char mode_str[16];
	if (!json_find_str(json, "mode", mode_str, sizeof(mode_str)))
		return -1;
	ds_mute_led_t mode = DS_MUTE_OFF;
	if (strcasecmp(mode_str, "on") == 0) mode = DS_MUTE_ON;
	else if (strcasecmp(mode_str, "pulse") == 0) mode = DS_MUTE_PULSE;
	ds_mute_led(dev, mode);
	return 0;
}

static void send_response(int fd, bool ok, const char *error)
{
	char buf[256];
	if (ok)
		snprintf(buf, sizeof(buf), "{\"ok\":true}\n");
	else
		snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}\n",
		         error ? error : "unknown");
	(void)write(fd, buf, strlen(buf));
}

static void handle_native_command(int client_fd, const char *line)
{
	char cmd[32];
	if (!json_find_str(line, "cmd", cmd, sizeof(cmd))) {
		send_response(client_fd, false, "missing cmd");
		return;
	}

	/* Optional "device" field selects controller index (default: 0) */
	int dev_idx = 0;
	json_find_int(line, "device", &dev_idx);
	ds_device_t *dev = get_device(dev_idx);
	if (!dev) {
		send_response(client_fd, false, "no controller connected");
		return;
	}

	int ret = 0;
	bool need_send = true;

	if (strcasecmp(cmd, "trigger") == 0)
		ret = handle_trigger(dev, line);
	else if (strcasecmp(cmd, "rumble") == 0)
		ret = handle_rumble(dev, line);
	else if (strcasecmp(cmd, "lightbar") == 0)
		ret = handle_lightbar(dev, line);
	else if (strcasecmp(cmd, "player-leds") == 0 || strcasecmp(cmd, "player_leds") == 0)
		ret = handle_player_leds(dev, line);
	else if (strcasecmp(cmd, "mute-led") == 0 || strcasecmp(cmd, "mute_led") == 0)
		ret = handle_mute_led(dev, line);
	else if (strcasecmp(cmd, "info") == 0) {
		/* Return info for all devices */
		char buf[512];
		int off = snprintf(buf, sizeof(buf), "{\"ok\":true,\"devices\":[");
		for (int i = 0; i < g_ndevices; i++) {
			if (i > 0) off += snprintf(buf + off, sizeof(buf) - off, ",");
			off += snprintf(buf + off, sizeof(buf) - off,
				"{\"index\":%d,\"connection\":\"%s\"}",
				i, ds_connection_type(g_devices[i]) == DS_BT ? "bluetooth" : "usb");
		}
		snprintf(buf + off, sizeof(buf) - off, "]}\n");
		(void)write(client_fd, buf, strlen(buf));
		need_send = false;
	} else {
		send_response(client_fd, false, "unknown command");
		return;
	}

	if (ret < 0) {
		send_response(client_fd, false, "invalid parameters");
		return;
	}

	if (need_send) {
		int err = ds_send(dev);
		if (err < 0) {
			send_response(client_fd, false, strerror(-err));
			return;
		}
		send_response(client_fd, true, NULL);
	}
}

/* ── DSX-compatible UDP protocol ────────────────────────────────── */

/*
 * DSX TriggerType_v2_Legacy → internal trigger mode mapping.
 * Maps DSX mode IDs to our trigger API calls.
 */
static void dsx_apply_trigger(ds_device_t *dev, ds_trigger_t side,
                              int dsx_mode, const int *p, int np)
{
	/* p[0..10] are the trigger params (up to 11 values from Parameters[3..13]) */
	switch (dsx_mode) {
	case 0:  /* NORMAL */
	case 20: /* OFF */
		ds_trigger_off(dev, side);
		break;

	case 21: /* FEEDBACK */
	case 13: /* RESISTANCE (legacy alias) */
		if (np >= 2)
			ds_trigger_feedback(dev, side, (uint8_t)p[0], (uint8_t)p[1]);
		break;

	case 22: /* WEAPON */
	case 16: /* SEMI_AUTOMATIC_GUN (legacy alias) */
		if (np >= 3)
			ds_trigger_weapon(dev, side, (uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2]);
		break;

	case 23: /* VIBRATION */
	case 17: /* AUTOMATIC_GUN (legacy alias) */
		if (np >= 3)
			ds_trigger_vibration(dev, side, (uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2]);
		break;

	case 8:  /* VIBRATE_TRIGGER — position=0, amplitude=8, freq=p[0] */
		ds_trigger_vibration(dev, side, 0, 8, np >= 1 ? (uint8_t)p[0] : 30);
		break;

	case 19: /* VIBRATE_TRIGGER_10 — hardcoded: pos=0, amp=8, freq=10 */
		ds_trigger_vibration(dev, side, 0, 8, 10);
		break;

	case 14: /* BOW */
		if (np >= 4)
			ds_trigger_bow(dev, side, (uint8_t)p[0], (uint8_t)p[1],
			               (uint8_t)p[2], (uint8_t)p[3]);
		break;

	case 15: /* GALLOPING */
		if (np >= 5)
			ds_trigger_galloping(dev, side, (uint8_t)p[0], (uint8_t)p[1],
			                     (uint8_t)p[2], (uint8_t)p[3], (uint8_t)p[4]);
		break;

	case 18: /* MACHINE */
		if (np >= 6)
			ds_trigger_machine(dev, side, (uint8_t)p[0], (uint8_t)p[1],
			                   (uint8_t)p[2], (uint8_t)p[3],
			                   (uint8_t)p[4], (uint8_t)p[5]);
		break;

	case 24: /* SLOPE_FEEDBACK */
		if (np >= 4)
			ds_trigger_slope_feedback(dev, side, (uint8_t)p[0], (uint8_t)p[1],
			                          (uint8_t)p[2], (uint8_t)p[3]);
		break;

	case 25: /* MULTIPLE_POSITION_FEEDBACK */
		if (np >= 10) {
			uint8_t str[10];
			for (int i = 0; i < 10; i++) str[i] = (uint8_t)p[i];
			ds_trigger_feedback_multi(dev, side, str);
		}
		break;

	case 26: /* MULTIPLE_POSITION_VIBRATION */
		if (np >= 11) {
			uint8_t amp[10];
			for (int i = 0; i < 10; i++) amp[i] = (uint8_t)p[i + 1];
			ds_trigger_vibration_multi(dev, side, amp, (uint8_t)p[0]);
		}
		break;

	case 12: /* CUSTOM_TRIGGER_VALUE — raw 11 bytes */
		if (np >= 11) {
			uint8_t params[10];
			for (int i = 0; i < 10; i++) params[i] = (uint8_t)p[i + 1];
			ds_trigger_raw(dev, side, (uint8_t)p[0], params);
		}
		break;

	/* Preset modes */
	case 2:  /* VERY_SOFT */
		ds_trigger_feedback(dev, side, 0, 1);
		break;
	case 3:  /* SOFT */
		ds_trigger_feedback(dev, side, 0, 2);
		break;
	case 10: /* MEDIUM */
		ds_trigger_feedback(dev, side, 0, 4);
		break;
	case 4:  /* HARD */
		ds_trigger_feedback(dev, side, 0, 6);
		break;
	case 5:  /* VERY_HARD */
		ds_trigger_feedback(dev, side, 0, 7);
		break;
	case 6:  /* HARDEST */
		ds_trigger_feedback(dev, side, 0, 8);
		break;
	case 7:  /* RIGID */
		ds_trigger_feedback(dev, side, 0, 8);
		break;
	case 9:  /* CHOPPY */
		{
			uint8_t raw[10] = {0x02, 0x27, 0x18, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00, 0x00};
			ds_trigger_raw(dev, side, 0x21, raw);
		}
		break;

	default:
		/* Unknown mode — ignore */
		break;
	}
}

static void dsx_process_packet(ds_device_t *dev, const char *json)
{
	/* Find "Instructions" array */
	const char *instructions = strstr(json, "\"Instructions\"");
	if (!instructions) return;

	/* Walk through each instruction object by finding "Type" keys */
	const char *pos = instructions;
	bool did_modify = false;

	while (pos && *pos) {
		/* Find next "Type" */
		const char *type_pos = strstr(pos + 1, "\"Type\"");
		if (!type_pos) break;

		/* Parse Type value */
		const char *tp = type_pos + 6; /* skip "Type" */
		while (*tp == ' ' || *tp == ':') tp++;
		int type = (int)strtol(tp, NULL, 10);

		/* Find "Parameters" array for this instruction */
		const char *params_pos = strstr(type_pos, "\"Parameters\"");
		if (!params_pos) { pos = tp; continue; }

		/* Parse the integer array */
		int params[16];
		int nparams = json_parse_int_array(params_pos + 12, params, 16);

		/* Process based on instruction type */
		switch (type) {
		case 0: /* GetDSXStatus — no-op, response is sent regardless */
			break;

		case 1: /* TriggerUpdate */
			if (nparams >= 3) {
				/* params[0]=ctrl, params[1]=side(1=L,2=R), params[2]=mode */
				ds_trigger_t side = (params[1] == 1) ? DS_TRIGGER_LEFT : DS_TRIGGER_RIGHT;
				int mode = params[2];
				/* Trigger-specific params start at params[3] */
				int trigger_params[13];
				int ntp = 0;
				for (int i = 3; i < nparams && ntp < 13; i++)
					trigger_params[ntp++] = params[i];
				dsx_apply_trigger(dev, side, mode, trigger_params, ntp);
				did_modify = true;
			}
			break;

		case 2: /* RGBUpdate */
			if (nparams >= 4) {
				ds_lightbar(dev, (uint8_t)params[1], (uint8_t)params[2], (uint8_t)params[3]);
				did_modify = true;
			}
			break;

		case 3: /* PlayerLED (legacy — 5 bools) */
			if (nparams >= 6) {
				uint8_t mask = 0;
				for (int i = 0; i < 5; i++)
					if (params[1 + i]) mask |= (1 << i);
				ds_player_leds(dev, mask);
				did_modify = true;
			}
			break;

		case 5: /* MicLED: 0=on, 1=pulse, 2=off */
			if (nparams >= 2) {
				ds_mute_led_t m = DS_MUTE_OFF;
				if (params[1] == 0) m = DS_MUTE_ON;
				else if (params[1] == 1) m = DS_MUTE_PULSE;
				ds_mute_led(dev, m);
				did_modify = true;
			}
			break;

		case 6: /* PlayerLEDNewRevision: preset byte */
			if (nparams >= 2) {
				/* 0=One(0x04), 1=Two(0x0A), 2=Three(0x15), 3=Four(0x1B), 4=Five(0x1F), 5=Off(0) */
				static const uint8_t led_presets[] = {0x04, 0x0A, 0x15, 0x1B, 0x1F, 0x00};
				int idx = params[1];
				if (idx >= 0 && idx <= 5)
					ds_player_leds(dev, led_presets[idx]);
				did_modify = true;
			}
			break;

		case 7: /* ResetToUserSettings */
			ds_trigger_off(dev, DS_TRIGGER_LEFT);
			ds_trigger_off(dev, DS_TRIGGER_RIGHT);
			ds_rumble(dev, 0, 0);
			did_modify = true;
			break;

		case 4: /* TriggerThreshold — not applicable to our hardware layer */
		case 8: /* ToMode — not applicable */
		default:
			break;
		}

		pos = params_pos + 1;
	}

	if (did_modify)
		ds_send(dev);
}

static void dsx_send_response(int udp_fd, struct sockaddr_in *client_addr,
                               socklen_t addr_len, ds_device_t *dev)
{
	const char *conn = ds_connection_type(dev) == DS_BT ? "Bluetooth" : "USB";
	char buf[512];
	snprintf(buf, sizeof(buf),
		"{"
		"\"Status\":\"DSX Received UDP Instructions\","
		"\"TimeReceived\":\"\","
		"\"isControllerConnected\":true,"
		"\"BatteryLevel\":100,"
		"\"Devices\":[{"
			"\"Index\":0,"
			"\"MacAddress\":\"00:00:00:00:00:00\","
			"\"DeviceType\":0,"
			"\"ConnectionType\":\"%s\","
			"\"BatteryLevel\":100,"
			"\"IsSupportAT\":true,"
			"\"IsSupportLightBar\":true,"
			"\"IsSupportPlayerLED\":true,"
			"\"IsSupportLegacyPlayerLED\":true,"
			"\"IsSupportMicLED\":true"
		"}]"
		"}", conn);

	sendto(udp_fd, buf, strlen(buf), 0,
	       (struct sockaddr *)client_addr, addr_len);
}

/* ── Socket paths ───────────────────────────────────────────────── */

static void get_socket_path(char *buf, size_t len)
{
	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg)
		snprintf(buf, len, "%s/dualsensed.sock", xdg);
	else
		snprintf(buf, len, "/tmp/dualsensed-%d.sock", getuid());
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	const char *devpath = NULL;
	int dsx_port = DSX_DEFAULT_PORT;
	bool no_dsx = false;

	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc)
			devpath = argv[++i];
		else if (strcmp(argv[i], "--dsx-port") == 0 && i + 1 < argc)
			dsx_port = atoi(argv[++i]);
		else if (strcmp(argv[i], "--no-dsx") == 0)
			no_dsx = true;
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			fprintf(stderr,
				"Usage: %s [options]\n"
				"  -d, --device <path>    hidraw device (auto-detect if omitted)\n"
				"  --dsx-port <port>      DSX UDP port (default: 6969)\n"
				"  --no-dsx               Disable DSX UDP listener\n"
				"  -h, --help             Show this help\n",
				argv[0]);
			return 0;
		}
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	/* Open controller(s) */
	if (devpath) {
		g_devices[0] = ds_open(devpath);
		g_ndevices = g_devices[0] ? 1 : 0;
	} else {
		g_ndevices = ds_open_all(g_devices, MAX_DEVICES);
	}

	if (g_ndevices == 0) {
		fprintf(stderr, "No DualSense controllers found\n");
		return 1;
	}

	for (int i = 0; i < g_ndevices; i++) {
		printf("Controller %d: %s\n", i,
		       ds_connection_type(g_devices[i]) == DS_BT ? "Bluetooth" : "USB");
	}

	/* Create Unix listening socket */
	char sock_path[256];
	get_socket_path(sock_path, sizeof(sock_path));
	unlink(sock_path);

	int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		perror("socket(unix)");
		for (int d = 0; d < g_ndevices; d++) ds_close(g_devices[d]);
		return 1;
	}

	struct sockaddr_un unix_addr = { .sun_family = AF_UNIX };
	strncpy(unix_addr.sun_path, sock_path, sizeof(unix_addr.sun_path) - 1);

	if (bind(listen_fd, (struct sockaddr *)&unix_addr, sizeof(unix_addr)) < 0) {
		perror("bind(unix)");
		close(listen_fd);
		for (int d = 0; d < g_ndevices; d++) ds_close(g_devices[d]);
		return 1;
	}
	chmod(sock_path, 0660);
	listen(listen_fd, MAX_CLIENTS);
	printf("Native socket: %s\n", sock_path);

	/* Create DSX UDP socket */
	int udp_fd = -1;
	if (!no_dsx) {
		udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (udp_fd < 0) {
			perror("socket(udp)");
		} else {
			int reuse = 1;
			setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

			struct sockaddr_in udp_addr = {
				.sin_family = AF_INET,
				.sin_port = htons(dsx_port),
				.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
			};

			if (bind(udp_fd, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
				fprintf(stderr, "Warning: cannot bind UDP port %d: %s\n",
				        dsx_port, strerror(errno));
				close(udp_fd);
				udp_fd = -1;
			} else {
				printf("DSX-compatible UDP: 127.0.0.1:%d\n", dsx_port);
			}
		}
	}

	/* Poll descriptors: [0]=unix_listen, [1]=udp, [2..]=clients */
	enum { POLL_UNIX = 0, POLL_UDP = 1, POLL_CLIENTS = 2 };
	struct pollfd fds[POLL_CLIENTS + MAX_CLIENTS];
	int nfds = POLL_CLIENTS;

	fds[POLL_UNIX].fd = listen_fd;
	fds[POLL_UNIX].events = POLLIN;

	fds[POLL_UDP].fd = udp_fd;
	fds[POLL_UDP].events = (udp_fd >= 0) ? POLLIN : 0;

	for (int i = POLL_CLIENTS; i < POLL_CLIENTS + MAX_CLIENTS; i++) {
		fds[i].fd = -1;
		fds[i].events = 0;
	}

	time_t last_dsx_msg = 0;
	bool dsx_active = false; /* true when a DSX mod is currently controlling */

	printf("Ready.\n");

	while (g_running) {
		int ret = poll(fds, nfds, 1000);
		if (ret < 0) {
			if (errno == EINTR) continue;
			break;
		}

		/* DSX mod timeout: reset after 60s of inactivity */
		if (dsx_active && last_dsx_msg > 0) {
			time_t now = time(NULL);
			if (now - last_dsx_msg > DSX_MOD_TIMEOUT_SEC) {
				printf("DSX mod timeout — resetting triggers\n");
				for (int d = 0; d < g_ndevices; d++) {
					ds_trigger_off(g_devices[d], DS_TRIGGER_LEFT);
					ds_trigger_off(g_devices[d], DS_TRIGGER_RIGHT);
					ds_rumble(g_devices[d], 0, 0);
					ds_send(g_devices[d]);
				}
				dsx_active = false;
			}
		}

		/* New Unix connection */
		if (fds[POLL_UNIX].revents & POLLIN) {
			int cfd = accept(listen_fd, NULL, NULL);
			if (cfd >= 0) {
				bool added = false;
				for (int i = POLL_CLIENTS; i < POLL_CLIENTS + MAX_CLIENTS; i++) {
					if (fds[i].fd == -1) {
						fds[i].fd = cfd;
						fds[i].events = POLLIN;
						if (i + 1 > nfds) nfds = i + 1;
						added = true;
						break;
					}
				}
				if (!added) {
					const char *msg = "{\"ok\":false,\"error\":\"too many clients\"}\n";
					(void)write(cfd, msg, strlen(msg));
					close(cfd);
				}
			}
		}

		/* DSX UDP data */
		if (udp_fd >= 0 && (fds[POLL_UDP].revents & POLLIN)) {
			char buf[BUF_SIZE];
			struct sockaddr_in client_addr;
			socklen_t addr_len = sizeof(client_addr);
			ssize_t n = recvfrom(udp_fd, buf, sizeof(buf) - 1, 0,
			                     (struct sockaddr *)&client_addr, &addr_len);
			if (n > 0) {
				buf[n] = '\0';
				dsx_process_packet(get_device(0), buf);
				dsx_send_response(udp_fd, &client_addr, addr_len, get_device(0));
				last_dsx_msg = time(NULL);
				dsx_active = true;
			}
		}

		/* Unix client data */
		for (int i = POLL_CLIENTS; i < nfds; i++) {
			if (fds[i].fd < 0) continue;
			if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;

			char buf[BUF_SIZE];
			ssize_t n = read(fds[i].fd, buf, sizeof(buf) - 1);
			if (n <= 0) {
				close(fds[i].fd);
				fds[i].fd = -1;
				continue;
			}
			buf[n] = '\0';

			/* Process each newline-delimited command */
			char *line = buf;
			char *nl;
			while ((nl = strchr(line, '\n')) != NULL) {
				*nl = '\0';
				if (strlen(line) > 0)
					handle_native_command(fds[i].fd, line);
				line = nl + 1;
			}
			if (strlen(line) > 0)
				handle_native_command(fds[i].fd, line);
		}
	}

	/* Cleanup */
	printf("\nShutting down...\n");
	for (int d = 0; d < g_ndevices; d++) {
		ds_trigger_off(g_devices[d], DS_TRIGGER_LEFT);
		ds_trigger_off(g_devices[d], DS_TRIGGER_RIGHT);
		ds_rumble(g_devices[d], 0, 0);
		ds_send(g_devices[d]);
		ds_close(g_devices[d]);
	}

	for (int i = POLL_CLIENTS; i < nfds; i++) {
		if (fds[i].fd >= 0)
			close(fds[i].fd);
	}
	if (udp_fd >= 0) close(udp_fd);
	close(listen_fd);
	unlink(sock_path);

	return 0;
}
