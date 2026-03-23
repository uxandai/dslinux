#ifndef DS_HIDRAW_H
#define DS_HIDRAW_H

#include <stdbool.h>
#include <stdint.h>
#include "../include/dualsense.h"

/*
 * Scan /dev/hidraw* for DualSense controllers.
 * @param path  If non-NULL, open this specific path. Otherwise auto-detect.
 * @param fd    Output: file descriptor for the hidraw device.
 * @param conn  Output: connection type (USB or BT).
 * @return 0 on success, negative errno on failure.
 */
int ds_hidraw_open(const char *path, int *fd, ds_conn_t *conn);

/*
 * Close a hidraw file descriptor.
 */
void ds_hidraw_close(int fd);

/*
 * Check if a hidraw fd is still valid (device connected).
 * @return true if device is accessible, false if disconnected.
 */
bool ds_hidraw_alive(int fd);

#endif /* DS_HIDRAW_H */
