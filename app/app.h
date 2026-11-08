/* SPDX-License-Identifier: GPL-3.0-only */
/* Grus on fm1-base: the app's half of the contract with the platform (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "GRUS"
#define APP_SPLASH_RGB RGB(224, 154, 47)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms */
#define GRUS_SR 44117.647f              /* the FM-1's I2S clock: 24 MHz / 544 */
#define APP_CANVAS_PX (240u * 120u)     /* the screen is drawn in two bands of 120 rows (56 KB) */

/* the store: settings (setup, song, chains, the REC scale), the panel's calibration, and the eight pattern slots, four
   objects each (a pattern is packed: only the steps that hold something; 3840 bytes an object). The patterns share a
   pool of 24 sectors (96 KB) at the bottom of the data area instead of a sector pair each. */
#define PAT_CHUNKS 4
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PAT0, OBJ_NOBJ = OBJ_PAT0 + 8 * PAT_CHUNKS };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u}
#define APP_STORE_POOL
#define APP_STORE_POOL_BASE 0x97000u
#define APP_STORE_POOL_N 24u
#define APP_STORE_POOL_OLD 0u
#define APP_STORE_POOL_OBJ0 OBJ_PAT0
#define APP_STORE_POOL_OBJN (8 * PAT_CHUNKS)

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

#define APP_HOST_EXPECT
#define APP_HOST_REBOOT
void app_host_reboot(void);
int app_expect(const char *what, int *got);
