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
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Read a hex value from a sysfs uevent file for a given key. */
static int read_uevent_hex(const char *sysfs_dir, const char *key, uint32_t *out)
{
	char path[512];
	snprintf(path, sizeof(path), "%s/device/uevent", sysfs_dir);

	FILE *f = fopen(path, "r");
	if (!f)
		return -errno;

	char line[256];
	size_t klen = strlen(key);
	int ret = -ENOENT;

	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
			*out = (uint32_t)strtoul(line + klen + 1, NULL, 16);
			ret = 0;
			break;
		}
	}

	fclose(f);
	return ret;
}

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

		char sysfs[256];
		snprintf(sysfs, sizeof(sysfs), "/sys/class/hidraw/%s", entry->d_name);

		uint16_t vid, pid, bus;
		if (parse_hid_id(sysfs, &vid, &pid, &bus) < 0)
			continue;

		if (!is_dualsense(vid, pid))
			continue;

		char devpath[64];
		snprintf(devpath, sizeof(devpath), "/dev/%s", entry->d_name);

		ret = try_open(devpath, fd, conn);
		if (ret == 0)
			break;
	}

	closedir(dir);
	return ret;
}

void ds_hidraw_close(int fd)
{
	if (fd >= 0)
		close(fd);
}
