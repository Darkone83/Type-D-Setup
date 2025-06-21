#pragma once
#include <stdint.h>

#define TYPE_D_MAX_UNITS 6

typedef struct {
    uint32_t ip;         // IPv4 address (network byte order)
    uint8_t  id;         // Device ID
    uint32_t last_seen;  // SDL_GetTicks() or similar timestamp
} type_d_unit_t;

#ifdef __cplusplus
extern "C" {
#endif

void detect_start(void);
void detect_stop(void);
int  detect_get_units(type_d_unit_t *out, int max);
const char *detect_ipstr(uint32_t ip); // For debug/menu display

#ifdef __cplusplus
}
#endif
