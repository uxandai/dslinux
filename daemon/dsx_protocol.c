/*
 * DSX-compatible UDP protocol handler.
 *
 * Parses DSX JSON packets like:
 *   {"Instructions":[{"Type":1,"Parameters":[0,2,22,40,160,8]}]}
 */

#include "dsx_protocol.h"
#include "json_parse.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

static ds_device_t *resolve_device(int idx, ds_device_t **devices, int ndevices)
{
	if (ndevices == 0) return NULL;
	if (idx < 0 || idx >= ndevices) idx = 0;
	return devices[idx];
}

/* Map DSX TriggerType_v2_Legacy mode ID to our trigger API. */
static void apply_trigger(ds_device_t *dev, ds_trigger_t side,
                          int dsx_mode, const int *p, int np)
{
	switch (dsx_mode) {
	case 0:  /* NORMAL */
	case 20: /* OFF */
		ds_trigger_off(dev, side);
		break;
	case 21: /* FEEDBACK */
	case 13: /* RESISTANCE */
		if (np >= 2) ds_trigger_feedback(dev, side, (uint8_t)p[0], (uint8_t)p[1]);
		break;
	case 22: /* WEAPON */
	case 16: /* SEMI_AUTOMATIC_GUN */
		if (np >= 3) ds_trigger_weapon(dev, side, (uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2]);
		break;
	case 23: /* VIBRATION */
	case 17: /* AUTOMATIC_GUN */
		if (np >= 3) ds_trigger_vibration(dev, side, (uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2]);
		break;
	case 8:  /* VIBRATE_TRIGGER */
		ds_trigger_vibration(dev, side, 0, 8, np >= 1 ? (uint8_t)p[0] : 30);
		break;
	case 19: /* VIBRATE_TRIGGER_10 */
		ds_trigger_vibration(dev, side, 0, 8, 10);
		break;
	case 14: /* BOW */
		if (np >= 4) ds_trigger_bow(dev, side, (uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2], (uint8_t)p[3]);
		break;
	case 15: /* GALLOPING */
		if (np >= 5) ds_trigger_galloping(dev, side, (uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2], (uint8_t)p[3], (uint8_t)p[4]);
		break;
	case 18: /* MACHINE */
		if (np >= 6) ds_trigger_machine(dev, side, (uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2], (uint8_t)p[3], (uint8_t)p[4], (uint8_t)p[5]);
		break;
	case 24: /* SLOPE_FEEDBACK */
		if (np >= 4) ds_trigger_slope_feedback(dev, side, (uint8_t)p[0], (uint8_t)p[1], (uint8_t)p[2], (uint8_t)p[3]);
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
	case 12: /* CUSTOM_TRIGGER_VALUE */
		if (np >= 11) {
			uint8_t params[10];
			for (int i = 0; i < 10; i++) params[i] = (uint8_t)p[i + 1];
			ds_trigger_raw(dev, side, (uint8_t)p[0], params);
		}
		break;
	/* Presets */
	case 2:  ds_trigger_feedback(dev, side, 0, 1); break; /* VERY_SOFT */
	case 3:  ds_trigger_feedback(dev, side, 0, 2); break; /* SOFT */
	case 10: ds_trigger_feedback(dev, side, 0, 4); break; /* MEDIUM */
	case 4:  ds_trigger_feedback(dev, side, 0, 6); break; /* HARD */
	case 5:  ds_trigger_feedback(dev, side, 0, 7); break; /* VERY_HARD */
	case 6:  /* HARDEST */
	case 7:  ds_trigger_feedback(dev, side, 0, 8); break; /* RIGID */
	case 9:  /* CHOPPY */ {
		uint8_t raw[10] = {0x02, 0x27, 0x18, 0, 0, 0x26, 0, 0, 0, 0};
		ds_trigger_raw(dev, side, 0x21, raw);
		break;
	}
	default: break;
	}
}

void dsx_process_packet(const char *json, ds_device_t **devices, int ndevices)
{
	const char *instructions = strstr(json, "\"Instructions\"");
	if (!instructions) return;

	const char *pos = instructions;
	bool did_modify = false;

	while (pos && *pos) {
		const char *type_pos = strstr(pos + 1, "\"Type\"");
		if (!type_pos) break;

		const char *tp = type_pos + 6;
		while (*tp == ' ' || *tp == ':') tp++;
		int type = (int)strtol(tp, NULL, 10);

		const char *params_pos = strstr(type_pos, "\"Parameters\"");
		if (!params_pos) { pos = tp; continue; }

		int params[16];
		int nparams = json_parse_int_array(params_pos + 12, params, 16);

		/* Resolve controller from Parameters[0] */
		int ctrl_idx = (nparams >= 1) ? params[0] : 0;
		ds_device_t *dev = resolve_device(ctrl_idx, devices, ndevices);
		if (!dev) { pos = params_pos + 1; continue; }

		switch (type) {
		case 0: break; /* GetDSXStatus */
		case 1: /* TriggerUpdate */
			if (nparams >= 3) {
				ds_trigger_t side = (params[1] == 1) ? DS_TRIGGER_LEFT : DS_TRIGGER_RIGHT;
				int trigger_params[13];
				int ntp = 0;
				for (int i = 3; i < nparams && ntp < 13; i++)
					trigger_params[ntp++] = params[i];
				apply_trigger(dev, side, params[2], trigger_params, ntp);
				did_modify = true;
			}
			break;
		case 2: /* RGBUpdate */
			if (nparams >= 4) {
				ds_lightbar(dev, (uint8_t)params[1], (uint8_t)params[2], (uint8_t)params[3]);
				did_modify = true;
			}
			break;
		case 3: /* PlayerLED */
			if (nparams >= 6) {
				uint8_t mask = 0;
				for (int i = 0; i < 5; i++)
					if (params[1 + i]) mask |= (1 << i);
				ds_player_leds(dev, mask);
				did_modify = true;
			}
			break;
		case 5: /* MicLED */
			if (nparams >= 2) {
				ds_mute_led_t m = DS_MUTE_OFF;
				if (params[1] == 0) m = DS_MUTE_ON;
				else if (params[1] == 1) m = DS_MUTE_PULSE;
				ds_mute_led(dev, m);
				did_modify = true;
			}
			break;
		case 6: /* PlayerLEDNewRevision */ {
			static const uint8_t presets[] = {0x04, 0x0A, 0x15, 0x1B, 0x1F, 0x00};
			if (nparams >= 2 && params[1] >= 0 && params[1] <= 5) {
				ds_player_leds(dev, presets[params[1]]);
				did_modify = true;
			}
			break;
		}
		case 7: /* ResetToUserSettings */
			ds_trigger_off(dev, DS_TRIGGER_LEFT);
			ds_trigger_off(dev, DS_TRIGGER_RIGHT);
			ds_rumble(dev, 0, 0);
			did_modify = true;
			break;
		default: break;
		}

		pos = params_pos + 1;
	}

	if (did_modify) {
		/* Send to all modified devices (simplified: send to all) */
		for (int i = 0; i < ndevices; i++)
			ds_send(devices[i]);
	}
}

void dsx_send_response(int udp_fd, struct sockaddr_in *client_addr,
                       socklen_t addr_len, ds_device_t **devices, int ndevices)
{
	char buf[1024];
	int off = snprintf(buf, sizeof(buf),
		"{\"Status\":\"DSX Received UDP Instructions\","
		"\"TimeReceived\":\"\","
		"\"isControllerConnected\":%s,"
		"\"BatteryLevel\":100,"
		"\"Devices\":[",
		ndevices > 0 ? "true" : "false");

	for (int i = 0; i < ndevices; i++) {
		if (i > 0) off += snprintf(buf + off, sizeof(buf) - off, ",");
		off += snprintf(buf + off, sizeof(buf) - off,
			"{\"Index\":%d,\"MacAddress\":\"00:00:00:00:00:00\","
			"\"DeviceType\":0,\"ConnectionType\":\"%s\","
			"\"BatteryLevel\":100,"
			"\"IsSupportAT\":true,\"IsSupportLightBar\":true,"
			"\"IsSupportPlayerLED\":true,\"IsSupportLegacyPlayerLED\":true,"
			"\"IsSupportMicLED\":true}",
			i, ds_connection_type(devices[i]) == DS_BT ? "Bluetooth" : "USB");
	}
	snprintf(buf + off, sizeof(buf) - off, "]}");

	sendto(udp_fd, buf, strlen(buf), 0,
	       (struct sockaddr *)client_addr, addr_len);
}

void dsx_reset_all(ds_device_t **devices, int ndevices)
{
	for (int i = 0; i < ndevices; i++) {
		ds_trigger_off(devices[i], DS_TRIGGER_LEFT);
		ds_trigger_off(devices[i], DS_TRIGGER_RIGHT);
		ds_rumble(devices[i], 0, 0);
		ds_send(devices[i]);
	}
}
