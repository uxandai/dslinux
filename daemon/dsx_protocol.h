#ifndef DAEMON_DSX_PROTOCOL_H
#define DAEMON_DSX_PROTOCOL_H

#include "dualsense.h"
#include <netinet/in.h>

/*
 * Process a DSX UDP JSON packet.
 * Parses Instructions array and applies trigger/LED/rumble commands.
 *
 * devices/ndevices: global controller array.
 * Parameters[0] in each instruction selects the controller index.
 */
void dsx_process_packet(const char *json, ds_device_t **devices, int ndevices);

/*
 * Send a DSX-compatible JSON response back to the UDP client.
 */
void dsx_send_response(int udp_fd, struct sockaddr_in *client_addr,
                       socklen_t addr_len, ds_device_t **devices, int ndevices);

/*
 * Reset all controllers (called on DSX mod timeout).
 */
void dsx_reset_all(ds_device_t **devices, int ndevices);

#endif
