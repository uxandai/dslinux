/*
 * dualsensed — daemon for DualSense controller management.
 *
 * Listens on a Unix socket for JSON-line commands and applies them
 * to the connected DualSense controller in real-time.
 *
 * Protocol (JSON lines over Unix stream socket):
 *   {"cmd":"trigger","side":"R","mode":"weapon","start":2,"end":7,"strength":8}
 *   {"cmd":"trigger","side":"L","mode":"off"}
 *   {"cmd":"rumble","left":128,"right":64}
 *   {"cmd":"lightbar","r":255,"g":0,"b":0}
 *   {"cmd":"player-leds","mask":21}
 *   {"cmd":"mute-led","mode":"pulse"}
 *   {"cmd":"info"}
 *
 * Response: {"ok":true} or {"ok":false,"error":"..."}
 */

#include "dualsense.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS    8
#define BUF_SIZE       2048
#define MIN_SEND_INTERVAL_NS  4000000L  /* 4ms = 250Hz max */

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

/* ── Minimal JSON parser (just enough for our protocol) ─────────── */

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

/* ── Command handlers ───────────────────────────────────────────── */

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
	write(fd, buf, strlen(buf));
}

static void send_info(int fd, ds_device_t *dev)
{
	char buf[256];
	snprintf(buf, sizeof(buf),
	         "{\"ok\":true,\"connection\":\"%s\"}\n",
	         ds_connection_type(dev) == DS_BT ? "bluetooth" : "usb");
	write(fd, buf, strlen(buf));
}

static void handle_command(ds_device_t *dev, int client_fd, const char *line)
{
	char cmd[32];
	if (!json_find_str(line, "cmd", cmd, sizeof(cmd))) {
		send_response(client_fd, false, "missing cmd");
		return;
	}

	int ret = 0;
	bool need_send = true;

	if (strcasecmp(cmd, "trigger") == 0) {
		ret = handle_trigger(dev, line);
	} else if (strcasecmp(cmd, "rumble") == 0) {
		ret = handle_rumble(dev, line);
	} else if (strcasecmp(cmd, "lightbar") == 0) {
		ret = handle_lightbar(dev, line);
	} else if (strcasecmp(cmd, "player-leds") == 0 || strcasecmp(cmd, "player_leds") == 0) {
		ret = handle_player_leds(dev, line);
	} else if (strcasecmp(cmd, "mute-led") == 0 || strcasecmp(cmd, "mute_led") == 0) {
		ret = handle_mute_led(dev, line);
	} else if (strcasecmp(cmd, "info") == 0) {
		send_info(client_fd, dev);
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
	}

	if (need_send)
		send_response(client_fd, true, NULL);
}

/* ── Socket path ────────────────────────────────────────────────── */

static int get_socket_path(char *buf, size_t len)
{
	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg)
		snprintf(buf, len, "%s/dualsensed.sock", xdg);
	else
		snprintf(buf, len, "/tmp/dualsensed-%d.sock", getuid());
	return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(int argc, char **argv)
{
	const char *devpath = NULL;

	for (int i = 1; i < argc; i++) {
		if ((strcmp(argv[i], "--device") == 0 || strcmp(argv[i], "-d") == 0) && i + 1 < argc)
			devpath = argv[++i];
		else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			fprintf(stderr, "Usage: %s [--device /dev/hidrawN]\n", argv[0]);
			return 0;
		}
	}

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGPIPE, SIG_IGN);

	/* Open controller */
	ds_device_t *dev = ds_open(devpath);
	if (!dev) {
		perror("Failed to open DualSense");
		return 1;
	}

	printf("DualSense connected via %s\n",
	       ds_connection_type(dev) == DS_BT ? "Bluetooth" : "USB");

	/* Create listening socket */
	char sock_path[256];
	get_socket_path(sock_path, sizeof(sock_path));
	unlink(sock_path);

	int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0) {
		perror("socket");
		ds_close(dev);
		return 1;
	}

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		close(listen_fd);
		ds_close(dev);
		return 1;
	}

	chmod(sock_path, 0660);

	if (listen(listen_fd, MAX_CLIENTS) < 0) {
		perror("listen");
		close(listen_fd);
		unlink(sock_path);
		ds_close(dev);
		return 1;
	}

	printf("Listening on %s\n", sock_path);

	/* Poll loop */
	struct pollfd fds[1 + MAX_CLIENTS];
	int nfds = 1;

	fds[0].fd = listen_fd;
	fds[0].events = POLLIN;

	for (int i = 1; i <= MAX_CLIENTS; i++) {
		fds[i].fd = -1;
		fds[i].events = 0;
	}

	while (g_running) {
		int ret = poll(fds, nfds, 1000);
		if (ret < 0) {
			if (errno == EINTR) continue;
			break;
		}

		/* New connection */
		if (fds[0].revents & POLLIN) {
			int cfd = accept(listen_fd, NULL, NULL);
			if (cfd >= 0) {
				bool added = false;
				for (int i = 1; i <= MAX_CLIENTS; i++) {
					if (fds[i].fd == -1) {
						fds[i].fd = cfd;
						fds[i].events = POLLIN;
						if (i >= nfds) nfds = i + 1;
						added = true;
						break;
					}
				}
				if (!added) {
					const char *msg = "{\"ok\":false,\"error\":\"too many clients\"}\n";
					write(cfd, msg, strlen(msg));
					close(cfd);
				}
			}
		}

		/* Client data */
		for (int i = 1; i < nfds; i++) {
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

			/* Process each line */
			char *line = buf;
			char *nl;
			while ((nl = strchr(line, '\n')) != NULL) {
				*nl = '\0';
				if (strlen(line) > 0)
					handle_command(dev, fds[i].fd, line);
				line = nl + 1;
			}
			if (strlen(line) > 0)
				handle_command(dev, fds[i].fd, line);
		}
	}

	/* Cleanup */
	printf("\nShutting down...\n");
	ds_trigger_off(dev, DS_TRIGGER_LEFT);
	ds_trigger_off(dev, DS_TRIGGER_RIGHT);
	ds_rumble(dev, 0, 0);
	ds_send(dev);

	for (int i = 1; i < nfds; i++) {
		if (fds[i].fd >= 0)
			close(fds[i].fd);
	}
	close(listen_fd);
	unlink(sock_path);
	ds_close(dev);

	return 0;
}
