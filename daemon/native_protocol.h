#ifndef DAEMON_NATIVE_PROTOCOL_H
#define DAEMON_NATIVE_PROTOCOL_H

#include "dualsense.h"

/*
 * Handle one native JSON-line command from a Unix socket client.
 * Resolves the target device from "device" field, applies command, calls ds_send().
 * Sends JSON response back to client_fd.
 *
 * devices/ndevices: global controller array.
 */
void native_handle_command(int client_fd, const char *line,
                           ds_device_t **devices, int ndevices);

#endif
