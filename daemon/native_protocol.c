/*
 * Native Unix socket protocol handler.
 *
 * JSON-line commands like:
 *   {"cmd":"trigger","side":"R","mode":"weapon","start":2,"end":7,"strength":8}
 */

#include "native_protocol.h"
#include "json_parse.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

static ds_device_t *resolve_device(const char *json, ds_device_t **devices, int ndevices)
{
	int idx = 0;
	json_find_int(json, "device", &idx);
	if (ndevices == 0) return NULL;
	if (idx < 0 || idx >= ndevices) idx = 0;
	return devices[idx];
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

void native_handle_command(int client_fd, const char *line,
                           ds_device_t **devices, int ndevices)
{
	char cmd[32];
	if (!json_find_str(line, "cmd", cmd, sizeof(cmd))) {
		send_response(client_fd, false, "missing cmd");
		return;
	}

	ds_device_t *dev = resolve_device(line, devices, ndevices);
	if (!dev) {
		send_response(client_fd, false, "no controller connected");
		return;
	}

	int ret = 0;
	bool need_send = true;

	if (strcasecmp(cmd, "trigger") == 0) {
		ret = handle_trigger(dev, line);
	} else if (strcasecmp(cmd, "rumble") == 0) {
		int left = 0, right = 0;
		json_find_int(line, "left", &left);
		json_find_int(line, "right", &right);
		ds_rumble(dev, (uint8_t)left, (uint8_t)right);
	} else if (strcasecmp(cmd, "lightbar") == 0) {
		int r = 0, g = 0, b = 0;
		json_find_int(line, "r", &r);
		json_find_int(line, "g", &g);
		json_find_int(line, "b", &b);
		ds_lightbar(dev, (uint8_t)r, (uint8_t)g, (uint8_t)b);
	} else if (strcasecmp(cmd, "player-leds") == 0 || strcasecmp(cmd, "player_leds") == 0) {
		int mask = 0;
		json_find_int(line, "mask", &mask);
		ds_player_leds(dev, (uint8_t)mask);
	} else if (strcasecmp(cmd, "mute-led") == 0 || strcasecmp(cmd, "mute_led") == 0) {
		char mode_str[16];
		if (json_find_str(line, "mode", mode_str, sizeof(mode_str))) {
			ds_mute_led_t m = DS_MUTE_OFF;
			if (strcasecmp(mode_str, "on") == 0) m = DS_MUTE_ON;
			else if (strcasecmp(mode_str, "pulse") == 0) m = DS_MUTE_PULSE;
			ds_mute_led(dev, m);
		}
	} else if (strcasecmp(cmd, "info") == 0) {
		char buf[512];
		int off = snprintf(buf, sizeof(buf), "{\"ok\":true,\"devices\":[");
		for (int i = 0; i < ndevices; i++) {
			if (i > 0) off += snprintf(buf + off, sizeof(buf) - off, ",");
			off += snprintf(buf + off, sizeof(buf) - off,
				"{\"index\":%d,\"connection\":\"%s\"}",
				i, ds_connection_type(devices[i]) == DS_BT ? "bluetooth" : "usb");
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
		send_response(client_fd, err == 0, err < 0 ? "send failed" : NULL);
	}
}
