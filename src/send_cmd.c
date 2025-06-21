#include "send_cmd.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <lwip/sockets.h>
#include <hal/debug.h>  // For debugPrint()

#define TYPE_D_CMD_PORT 8080

// param can be "val=50" or "file=foo" or NULL
bool send_cmd(const char* ip, const char* cmd_code, const char* param) {
    if (!ip || !cmd_code) {
        debugPrint("[send_cmd] Invalid IP or command\n");
        return false;
    }

    char request[512];
    if (param && param[0] != '\0') {
        snprintf(request, sizeof(request),
            "GET /cmd?c=%s&%s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "\r\n",
            cmd_code, param, ip);
    } else {
        snprintf(request, sizeof(request),
            "GET /cmd?c=%s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "\r\n",
            cmd_code, ip);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        debugPrint("[send_cmd] Socket creation failed\n");
        return false;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TYPE_D_CMD_PORT);
    addr.sin_addr.s_addr = inet_addr(ip);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        debugPrint("[send_cmd] Connect failed\n");
        closesocket(sock);
        return false;
    }

    int sent = send(sock, request, strlen(request), 0);
    closesocket(sock);

    bool success = (sent == (int)strlen(request));
    if (!success) {
        debugPrint("[send_cmd] Send failed\n");
    }
    return success;
}
