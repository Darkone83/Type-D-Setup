#include "detect.h"
#include <lwip/sockets.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <hal/debug.h>
#include <SDL.h>

#define DETECT_DISCOVER_PORT   50501
#define DETECT_ANNOUNCE_PORT   50502
#define DETECT_BROADCAST       "255.255.255.255"
#define DETECT_DISCOVER_MSG    "TYPE_D_DISCOVER?"
#define DETECT_REPLY_PREFIX    "TYPE_D_ID:"
#define DETECT_TIMEOUT_MS      5000

static type_d_unit_t units[TYPE_D_MAX_UNITS];
static int unit_count = 0;
static int running = 0;
static SDL_Thread *detect_thread = NULL;

// Utility: IP to string (host order)
const char *detect_ipstr(uint32_t ip) {
    static char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
        (ip >> 24) & 0xFF,
        (ip >> 16) & 0xFF,
        (ip >> 8) & 0xFF,
        ip & 0xFF
    );
    return buf;
}

static void add_or_update_unit(uint32_t ip, uint8_t id) {
    // Update existing or add new unit
    for (int i = 0; i < unit_count; ++i) {
        if (units[i].ip == ip) {
            units[i].id = id;
            units[i].last_seen = SDL_GetTicks();
            return;
        }
    }
    if (unit_count < TYPE_D_MAX_UNITS) {
        units[unit_count].ip = ip;
        units[unit_count].id = id;
        units[unit_count].last_seen = SDL_GetTicks();
        unit_count++;
    }
}

//static void prune_units(void) {
//    uint32_t now = SDL_GetTicks();
//    int dst = 0;
//    for (int i = 0; i < unit_count; ++i) {
//        if (now - units[i].last_seen <= DETECT_TIMEOUT_MS) {
//            if (dst != i)
//                units[dst] = units[i];
//            dst++;
//        }
//    }
//    unit_count = dst;
//}

static int detect_thread_func(void *data) {
    int sock1 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // 50501 (discovery/reply)
    int sock2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP); // 50502 (announce)
    if (sock1 < 0 || sock2 < 0) return -1;

    int yes = 1;
    setsockopt(sock1, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    setsockopt(sock2, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    // Bind both sockets
    struct sockaddr_in addr1 = {0}, addr2 = {0};
    addr1.sin_family = AF_INET; addr1.sin_port = htons(DETECT_DISCOVER_PORT); addr1.sin_addr.s_addr = INADDR_ANY;
    addr2.sin_family = AF_INET; addr2.sin_port = htons(DETECT_ANNOUNCE_PORT); addr2.sin_addr.s_addr = INADDR_ANY;
    bind(sock1, (struct sockaddr*)&addr1, sizeof(addr1));
    bind(sock2, (struct sockaddr*)&addr2, sizeof(addr2));

    struct sockaddr_in broadcast_addr = {0};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(DETECT_DISCOVER_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr(DETECT_BROADCAST);

    while (running) {
        // Send discover packet
        sendto(sock1, DETECT_DISCOVER_MSG, (int)strlen(DETECT_DISCOVER_MSG), 0,
            (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));

        // Listen for replies/announcements (50501 and 50502) with short timeout
        struct timeval tv = {0, 500 * 1000}; // 500ms
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sock1, &readfds);
        FD_SET(sock2, &readfds);
        int maxfd = (sock1 > sock2) ? sock1 : sock2;

        int rv = select(maxfd+1, &readfds, NULL, NULL, &tv);
        if (rv > 0) {
            for (int s = 0; s < 2; ++s) {
                int sock = (s == 0) ? sock1 : sock2;
                if (FD_ISSET(sock, &readfds)) {
                    struct sockaddr_in from;
                    socklen_t fromlen = sizeof(from);
                    char buf[64];
                    int len = recvfrom(sock, buf, sizeof(buf)-1, 0, (struct sockaddr*)&from, &fromlen);
                    if (len > 0) {
                        buf[len] = 0;
                        if (strncmp(buf, DETECT_REPLY_PREFIX, strlen(DETECT_REPLY_PREFIX)) == 0) {
                            uint8_t id = (uint8_t)atoi(buf + strlen(DETECT_REPLY_PREFIX));
                            add_or_update_unit(ntohl(from.sin_addr.s_addr), id);
                        }
                    }
                }
            }
        }
        //prune_units();
        SDL_Delay(1000); // Repeat every second
    }
    closesocket(sock1);
    closesocket(sock2);
    return 0;
}

void detect_start(void) {
    if (running) return;
    running = 1;
    unit_count = 0;
    detect_thread = SDL_CreateThread(detect_thread_func, "detect_thread", NULL);
}

void detect_stop(void) {
    running = 0;
    if (detect_thread) {
        SDL_WaitThread(detect_thread, NULL);
        detect_thread = NULL;
    }
}

int detect_get_units(type_d_unit_t *out, int max) {
    //prune_units();
    int n = (unit_count < max) ? unit_count : max;
    for (int i = 0; i < n; ++i)
        out[i] = units[i];
    return n;
}
