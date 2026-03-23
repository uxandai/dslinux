/*
 * hidraw device enumeration and I/O for DualSense controllers.
 *
 * Scans /sys/class/hidraw/ to find devices matching Sony DualSense VID/PID,
 * then opens /dev/hidrawN.  BT vs USB is detected via the rdesc size
 * (HIDIOCGRDESCSIZE ioctl).
 */

#include "hidraw.h"

#include <stdbool.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/*
 * Parse HID_ID from uevent: "HID_ID=0003:0000054C:00000CE6"
 * Format: bus_type:vendor:product (all hex).
 */
static int parse_hid_id(const char *sysfs_dir, uint16_t *vid, uint16_t *pid, uint16_t *bus)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/device/uevent", sysfs_dir);

	FILE *f = fopen(path, "r");
	if (!f)
		return -errno;

	char line[256];
	int ret = -ENOENT;

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "HID_ID=", 7) == 0) {
			unsigned int b, v, p;
			if (sscanf(line + 7, "%x:%x:%x", &b, &v, &p) == 3) {
				*bus = (uint16_t)b;
				*vid = (uint16_t)v;
				*pid = (uint16_t)p;
				ret = 0;
			}
			break;
		}
	}

	fclose(f);
	return ret;
}

static bool is_dualsense(uint16_t vid, uint16_t pid)
{
	if (vid != DS_VID)
		return false;
	return pid == DS_PID_DUALSENSE || pid == DS_PID_EDGE;
}

static ds_conn_t detect_connection(int fd)
{
	struct hidraw_devinfo info;
	if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0)
		return DS_USB; /* fallback */

	/* BUS_BLUETOOTH = 0x05, BUS_USB = 0x03 */
	return (info.bustype == 0x05) ? DS_BT : DS_USB;
}

static int try_open(const char *devpath, int *fd, ds_conn_t *conn)
{
	int f = open(devpath, O_RDWR | O_NONBLOCK);
	if (f < 0)
		return -errno;

	*fd = f;
	*conn = detect_connection(f);
	return 0;
}

int ds_hidraw_open(const char *path, int *fd, ds_conn_t *conn)
{
	if (path)
		return try_open(path, fd, conn);

	/* Auto-detect: scan /sys/class/hidraw/ */
	DIR *dir = opendir("/sys/class/hidraw");
	if (!dir)
		return -errno;

	struct dirent *entry;
	int ret = -ENODEV;

	while ((entry = readdir(dir)) != NULL) {
		if (strncmp(entry->d_name, "hidraw", 6) != 0)
			continue;

		char sysfs[512];
		snprintf(sysfs, sizeof(sysfs), "/sys/class/hidraw/%s", entry->d_name);

		uint16_t vid, pid, bus;
		if (parse_hid_id(sysfs, &vid, &pid, &bus) < 0)
			continue;

		if (!is_dualsense(vid, pid))
			continue;

		char devpath[512];
		snprintf(devpath, sizeof(devpath), "/dev/%s", entry->d_name);

		ret = try_open(devpath, fd, conn);
		if (ret == 0)
			break;
	}

	closedir(dir);
	return ret;
}

int ds_hidraw_open_all(int *fds, ds_conn_t *conns, int max)
{
	DIR *dir = opendir("/sys/class/hidraw");
	if (!dir)
		return -errno;

	struct dirent *entry;
	int count = 0;

	while ((entry = readdir(dir)) != NULL && count < max) {
		if (strncmp(entry->d_name, "hidraw", 6) != 0)
			continue;

		char sysfs[512];
		snprintf(sysfs, sizeof(sysfs), "/sys/class/hidraw/%s", entry->d_name);

		uint16_t vid, pid, bus;
		if (parse_hid_id(sysfs, &vid, &pid, &bus) < 0)
			continue;
		if (!is_dualsense(vid, pid))
			continue;

		char devpath[512];
		snprintf(devpath, sizeof(devpath), "/dev/%s", entry->d_name);

		int fd;
		ds_conn_t conn;
		if (try_open(devpath, &fd, &conn) == 0) {
			fds[count] = fd;
			conns[count] = conn;
			count++;
		}
	}

	closedir(dir);
	return count;
}

void ds_hidraw_close(int fd)
{
	if (fd >= 0)
		close(fd);
}

bool ds_hidraw_alive(int fd)
{
	struct hidraw_devinfo info;
	return ioctl(fd, HIDIOCGRAWINFO, &info) == 0;
}

/*
 * ── Auto-reconnect (scaffolding, not yet wired into daemon) ────
 *
 * TODO: Integrate into dualsensed main loop:
 *   1. Detect disconnect via ds_send() returning -ENODEV
 *   2. Call ds_hidraw_wait_reconnect() which polls /sys/class/hidraw/
 *   3. When device reappears, reopen fd and resume
 *
 * int ds_hidraw_wait_reconnect(int *fd, ds_conn_t *conn, int timeout_sec)
 * {
 *     for (int elapsed = 0; elapsed < timeout_sec; elapsed++) {
 *         if (ds_hidraw_open(NULL, fd, conn) == 0)
 *             return 0;
 *         sleep(1);
 *     }
 *     return -ETIMEDOUT;
 * }
 *
 * Better approach: use udev_monitor to get hotplug events:
 *
 * #include <libudev.h>
 *
 * int ds_hidraw_monitor_fd(void)
 * {
 *     struct udev *udev = udev_new();
 *     struct udev_monitor *mon = udev_monitor_new_from_netlink(udev, "udev");
 *     udev_monitor_filter_add_match_subsystem_devtype(mon, "hidraw", NULL);
 *     udev_monitor_enable_receiving(mon);
 *     return udev_monitor_get_fd(mon);  // add to poll() loop
 * }
 *
 * When poll() fires on the udev monitor fd:
 *   struct udev_device *dev = udev_monitor_receive_device(mon);
 *   const char *action = udev_device_get_action(dev);  // "add" or "remove"
 *   const char *devnode = udev_device_get_devnode(dev); // "/dev/hidraw3"
 *   // If action=="add" and VID/PID matches → reopen
 */
