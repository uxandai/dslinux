/*
 * dualsensed — DualSense controller daemon.
 *
 * Interfaces:
 *   1) Unix socket — native JSON-line protocol (dsctl, GUI, scripts)
 *   2) UDP 6969   — DSX-compatible protocol (Proton/Wine game mods)
 */

#include "dualsense.h"
#include "native_protocol.h"
#include "dsx_protocol.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
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

#define MAX_CLIENTS          8
#define MAX_DEVICES          4
#define BUF_SIZE             4096
#define DSX_DEFAULT_PORT     6969
#define DSX_MOD_TIMEOUT_SEC  60

static volatile sig_atomic_t g_running = 1;

static void sig_handler(int sig)
{
	(void)sig;
	g_running = 0;
}

static void get_socket_path(char *buf, size_t len)
{
	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg)
		snprintf(buf, len, "%s/dualsensed.sock", xdg);
	else
		snprintf(buf, len, "/tmp/dualsensed-%d.sock", getuid());
}

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

	/* ── Open controllers ───────────────────────────────────────── */

	ds_device_t *devices[MAX_DEVICES];
	int ndevices;

	if (devpath) {
		devices[0] = ds_open(devpath);
		ndevices = devices[0] ? 1 : 0;
	} else {
		ndevices = ds_open_all(devices, MAX_DEVICES);
	}

	if (ndevices == 0) {
		fprintf(stderr, "No DualSense controllers found\n");
		return 1;
	}

	for (int i = 0; i < ndevices; i++)
		printf("Controller %d: %s\n", i,
		       ds_connection_type(devices[i]) == DS_BT ? "Bluetooth" : "USB");

	/* ── Unix listening socket ──────────────────────────────────── */

	char sock_path[256];
	get_socket_path(sock_path, sizeof(sock_path));
	unlink(sock_path);

	int listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (listen_fd < 0) { perror("socket(unix)"); goto cleanup_devices; }

	struct sockaddr_un unix_addr = { .sun_family = AF_UNIX };
	strncpy(unix_addr.sun_path, sock_path, sizeof(unix_addr.sun_path) - 1);

	if (bind(listen_fd, (struct sockaddr *)&unix_addr, sizeof(unix_addr)) < 0) {
		perror("bind(unix)");
		close(listen_fd);
		goto cleanup_devices;
	}
	chmod(sock_path, 0660);
	listen(listen_fd, MAX_CLIENTS);
	printf("Native socket: %s\n", sock_path);

	/* ── DSX UDP socket ─────────────────────────────────────────── */

	int udp_fd = -1;
	if (!no_dsx) {
		udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (udp_fd >= 0) {
			int reuse = 1;
			setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
			struct sockaddr_in udp_addr = {
				.sin_family = AF_INET,
				.sin_port = htons(dsx_port),
				.sin_addr.s_addr = htonl(INADDR_LOOPBACK),
			};
			if (bind(udp_fd, (struct sockaddr *)&udp_addr, sizeof(udp_addr)) < 0) {
				fprintf(stderr, "Warning: UDP port %d: %s\n", dsx_port, strerror(errno));
				close(udp_fd);
				udp_fd = -1;
			} else {
				printf("DSX-compatible UDP: 127.0.0.1:%d\n", dsx_port);
			}
		}
	}

	/* ── Poll loop ──────────────────────────────────────────────── */

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
	bool dsx_active = false;

	printf("Ready. (%d controller%s)\n", ndevices, ndevices > 1 ? "s" : "");

	while (g_running) {
		int ret = poll(fds, nfds, 1000);
		if (ret < 0) {
			if (errno == EINTR) continue;
			break;
		}

		/* DSX mod timeout */
		if (dsx_active && time(NULL) - last_dsx_msg > DSX_MOD_TIMEOUT_SEC) {
			printf("DSX mod timeout — resetting\n");
			dsx_reset_all(devices, ndevices);
			dsx_active = false;
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
					(void)write(cfd, "{\"ok\":false,\"error\":\"too many clients\"}\n", 39);
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
				dsx_process_packet(buf, devices, ndevices);
				dsx_send_response(udp_fd, &client_addr, addr_len, devices, ndevices);
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

			char *line = buf;
			char *nl;
			while ((nl = strchr(line, '\n')) != NULL) {
				*nl = '\0';
				if (*line)
					native_handle_command(fds[i].fd, line, devices, ndevices);
				line = nl + 1;
			}
			if (*line)
				native_handle_command(fds[i].fd, line, devices, ndevices);
		}
	}

	/* ── Cleanup ────────────────────────────────────────────────── */

	printf("\nShutting down...\n");
	dsx_reset_all(devices, ndevices);

	for (int i = POLL_CLIENTS; i < nfds; i++)
		if (fds[i].fd >= 0) close(fds[i].fd);
	if (udp_fd >= 0) close(udp_fd);
	close(listen_fd);
	unlink(sock_path);

cleanup_devices:
	for (int i = 0; i < ndevices; i++)
		ds_close(devices[i]);

	return 0;
}
