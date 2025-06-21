#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Converts ASCII string to uppercase hex string (output buffer must be at least 2x input+1).
 */
void ascii_to_hex(const char* ascii, char* hexbuf, int hexbufsize);

/**
 * Sends an HTTP GET command to a Type D device at the given IP address and port 8080.
 *
 * @param ip      Target IPv4 address as string, e.g. "192.168.1.120"
 * @param cmd_hex Command hex string, e.g. "0001"
 * @param hex_arg Optional argument hex string, or NULL/empty
 * @return        true on success, false on failure
 */
bool send_cmd(const char* ip, const char* cmd_hex, const char* hex_arg);

#ifdef __cplusplus
}
#endif
